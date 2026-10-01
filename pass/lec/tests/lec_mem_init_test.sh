#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Init-contents tie for the cvc5 array encoder. A type==2 array (a runtime-indexed
# comb array / ROM) carries its comptime contents on the Memory `initial` pin. The
# encoder must rebuild the array base from that init (PER DESIGN) so:
#   - a ROM / mut-array PROVES equivalent to a combinational reference, and
#   - a reference with WRONG init data REFUTES.
# The negative case guards SOUNDNESS: the init must be genuinely compared, not
# pinned onto the shared array symbol (which would be a vacuous proof). Before the
# fix the init was captured then dropped for non-whole type==2 arrays, leaving the
# array a free symbol -> reads diverged -> false refute.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecmeminit}"
mkdir -p "$WORK"
fail=0

# ROM: a const array read at a runtime index (type==2, contents on the init pin,
# zero write ports). The .v reference is the equivalent combinational mux.
cat > "$WORK/rom.prp" <<'EOF'
mod rom(rsel:U2) -> (z:U8@[0]) {
  const t:[4]U8 = (10,20,30,40)
  z = t[rsel]
}
EOF
cat > "$WORK/rom_good.v" <<'EOF'
module \rom.rom (input [1:0] rsel, output [7:0] z);
  assign z = (rsel==2'd0)?8'd10:(rsel==2'd1)?8'd20:(rsel==2'd2)?8'd30:8'd40;
endmodule
EOF
# Wrong init: rsel==0 yields 11 instead of 10 -> must REFUTE.
cat > "$WORK/rom_bad.v" <<'EOF'
module \rom.rom (input [1:0] rsel, output [7:0] z);
  assign z = (rsel==2'd0)?8'd11:(rsel==2'd1)?8'd20:(rsel==2'd2)?8'd30:8'd40;
endmodule
EOF

# Combinational mut-array: per-cycle default = init contents, a write overrides
# its entry for the rest of the cycle (reads are combinational, no persistence).
cat > "$WORK/ca.prp" <<'EOF'
mod ca(a:U8, wsel:U2, rsel:U2) -> (z:U8@[0]) {
  mut t:[4]U8 = (1,2,3,4)
  t[wsel] = a
  z = t[rsel]
}
EOF
cat > "$WORK/ca_good.v" <<'EOF'
module \ca.ca (input [7:0] a, input [1:0] wsel, input [1:0] rsel, output [7:0] z);
  wire [7:0] iv = (rsel==2'd0)?8'd1:(rsel==2'd1)?8'd2:(rsel==2'd2)?8'd3:8'd4;
  assign z = (wsel==rsel)?a:iv;
endmodule
EOF

# A PERSISTENT sync ROM (type==1 __memory, no write ports) must also tie its
# initial state to `init`. Crucially this is a SOUNDNESS fix: without it both
# sides share one free array symbol and two ROMs with DIFFERENT init would falsely
# PROVE. Build per-design (prp-vs-prp) ROMs in same-named files / different dirs.
mkdir -p "$WORK/sa" "$WORK/sb" "$WORK/sc"
sync_rom() {  # $1=dir $2=init-tuple
  cat > "$WORK/$1/srom.prp" <<EOF
mod srom(raddr:U2) -> (q:U4@[1]) {
  mut mem = (
    const addr=(raddr), const bits=4, const size=4, const din=(0),
    const enable=(1), const fwd=false, const \`type\`=1, const wensize=1,
    const rdport=(1), const initial=$2,
  )
  mut res = __memory(mem)
  q = res[0]
}
EOF
}
sync_rom sa "(1,2,3,4)"
sync_rom sb "(1,2,3,4)"
sync_rom sc "(1,2,3,9)"  # wrong init

verdict() {  # $1=ref.v $2=impl.prp $3=top -> PROVEN | REFUTED | UNKNOWN
  $LHD lec --ref "verilog:$WORK/$1" --impl "pyrope:$WORK/$2" --top "$3" \
       --set formal.engine=ind --workdir "$WORK/q_${1}_$$" 2>&1 \
    | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN\|INCONCLUSIVE" | head -1
}
verdict_pp() {  # $1=ref.prp $2=impl.prp $3=top -> PROVEN | REFUTED | UNKNOWN
  $LHD lec --ref "pyrope:$WORK/$1" --impl "pyrope:$WORK/$2" --top "$3" \
       --set formal.engine=ind --workdir "$WORK/qpp_$$_${RANDOM}" 2>&1 \
    | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN\|INCONCLUSIVE" | head -1
}

expect() { if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1; else echo "ok: $1 -> $2"; fi; }

# A Verilog memory whose contents come from an `initial` block: slang captures
# the per-entry writes and emits them INLINE as a tuple literal on the declare;
# tolg must honor that inline init (it used to drop it for arrays -> zero-fill).
cat > "$WORK/rom_vinit.v" <<'EOF'
module \rom.rom (input [1:0] rsel, output [7:0] z);
  reg [7:0] data[3:0];
  initial begin data[0]=8'd10; data[1]=8'd20; data[2]=8'd30; data[3]=8'd40; end
  assign z = data[rsel];
endmodule
EOF
cat > "$WORK/rom_vinit_bad.v" <<'EOF'
module \rom.rom (input [1:0] rsel, output [7:0] z);
  reg [7:0] data[3:0];
  initial begin data[0]=8'd99; data[1]=8'd20; data[2]=8'd30; data[3]=8'd40; end
  assign z = data[rsel];
endmodule
EOF

expect "ROM init read"           "$(verdict rom_good.v rom.prp rom.rom)"  "PROVEN equivalent"
expect "verilog initial ROM"     "$(verdict rom_vinit.v rom.prp rom.rom)" "PROVEN equivalent"
expect "verilog init wrong(snd)" "$(verdict rom_vinit_bad.v rom.prp rom.rom)" "REFUTED (not equivalent)"
expect "ROM wrong init (sound)"  "$(verdict rom_bad.v  rom.prp rom.rom)"  "REFUTED (not equivalent)"
expect "comb mut-array init"     "$(verdict ca_good.v  ca.prp  ca.ca)"    "PROVEN equivalent"
expect "sync ROM init match"     "$(verdict_pp sa/srom.prp sb/srom.prp srom.srom)" "PROVEN equivalent"
expect "sync ROM wrong (sound)"  "$(verdict_pp sa/srom.prp sc/srom.prp srom.srom)" "REFUTED (not equivalent)"

# Round-trip the zero-write-port synchronous helper as well. Emission splits
# its read register from the ROM, so use BMC across that state representation
# change. The original memory-to-memory inductive checks above remain unbounded.
verdict_rom_bmc() {  # $1=ref.v $2=impl.prp
  $LHD lec --ref "verilog:$WORK/$1" --impl "pyrope:$WORK/$2" --top srom.srom \
       --set formal.engine=bmc --set formal.bound=6 --set formal.simfail_run=false \
       --workdir "$WORK/rom_bmc_$$_${RANDOM}" 2>&1 \
    | grep -o "PASS(6) equivalent\|REFUTED (not equivalent)\|UNKNOWN\|INCONCLUSIVE" | head -1
}
if $LHD compile "$WORK/sa/srom.prp" --top srom.srom --emit "verilog:$WORK/srom.v" \
     --workdir "$WORK/emit_srom" >"$WORK/emit_srom.log" 2>&1; then
  expect "sync ROM emitted init" "$(verdict_rom_bmc srom.v sb/srom.prp)" "PASS(6) equivalent"
  expect "sync ROM emitted wrong" "$(verdict_rom_bmc srom.v sc/srom.prp)" "REFUTED (not equivalent)"
  sed 's/\.LATENCY_0(1)/.LATENCY_0(0)/' "$WORK/srom.v" > "$WORK/srom_async.v"
  expect "sync ROM wrong latency" "$(verdict_rom_bmc srom_async.v sb/srom.prp)" "REFUTED (not equivalent)"
else
  cat "$WORK/emit_srom.log"
  echo "FAIL: sync ROM Verilog emission"
  fail=1
fi

# A WRITABLE memory whose `initial` block is power-on contents only, with no
# reset (docs 08-memories: `initial=` next to `= nil`). The inductive engine
# shares ONE current-state array between the designs, which says they START
# equal; no reset ever re-establishes power-on contents, so two memories whose
# contents differ give that cut no base. The inductive engine must not claim
# PROVEN there (the default ind|bmc race took its PROVEN first), and BMC,
# which seeds each side's own contents, refutes.
pow_mem() {  # $1=file $2=entry-0 contents
  cat > "$WORK/$1" <<EOF
module romt(input clk, input we, input [1:0] wa, input [7:0] d, input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  initial begin
    mem[0] = 8'd$2; mem[1] = 8'd2; mem[2] = 8'd3; mem[3] = 8'd4;
  end
  always @(posedge clk) if (we) mem[wa] <= d;
  assign q = mem[ra];
endmodule
EOF
}
pow_mem pow_a.v 1
pow_mem pow_b.v 9
cat > "$WORK/pow_a.prp" <<'EOF'
pub mod romt(clk:Clock, we:Bool, wa:U2, d:U8, ra:U2) -> (q:U8@[0]) {
  reg mem:[4]U8:[initial=0x04030201] = nil
  q = mem[ra]
  if we { mem[wa] = d }
}
EOF
verdict_eng() {  # $1=ref $2=impl $3=engine [extra --set...] -> PROVEN | REFUTED | UNKNOWN
  local r="$1" i="$2" e="$3"
  shift 3
  $LHD lec --ref "$WORK/$r" --impl "$WORK/$i" --top romt --set "formal.engine=$e" "$@" \
       --set formal.simfail_run=false --workdir "$WORK/qe_$$_${RANDOM}" 2>&1 \
    | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN\|INCONCLUSIVE" | head -1
}
expect "power-on mem differs (auto)"   "$(verdict_eng pow_a.v pow_b.v auto)" "REFUTED (not equivalent)"
expect "power-on mem differs (ind)"    "$(verdict_eng pow_a.v pow_b.v ind)"  "UNKNOWN"
expect "power-on mem equal (v vs prp)" "$(verdict_eng pow_a.v pow_a.prp auto --set formal.lec.semdiff=none)" \
  "PROVEN equivalent"

# Same power-on hole for a memory written ONLY through the whole-array `update`
# bus (`mem = nx`): it has no per-port write (n_wr == 0) yet is not a ROM, so the
# shared current-state array still assumes both sides start equal.
pow_upd() {  # $1=file $2=entry-0 contents (hex)
  cat > "$WORK/$1" <<EOF
pub mod romt(clk:Clock, we:Bool, d:U8, wa:U2, ra:U2) -> (q:U8@[0]) {
  reg mem:[4]U8:[initial=0x040302$2] = nil
  q = mem[ra]
  mut nx:[4]U8 = (0,0,0,0)
  nx[wa] = d
  if we { mem = nx }
}
EOF
}
mkdir -p "$WORK/ua" "$WORK/ub"
pow_upd ua/romt.prp 01
pow_upd ub/romt.prp 09
expect "power-on update-bus mem (auto)" "$(verdict_eng ua/romt.prp ub/romt.prp auto)" "REFUTED (not equivalent)"
expect "power-on update-bus mem (ind)"  "$(verdict_eng ua/romt.prp ub/romt.prp ind)"  "UNKNOWN"

if [ $fail -ne 0 ]; then echo "lec_mem_init_test: FAILED"; exit 1; fi
echo "lec_mem_init_test: PASSED"
exit 0
