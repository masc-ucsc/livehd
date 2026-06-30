#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Init-contents tie for the cvc5 array encoder. A type==2 array (a runtime-indexed
# comb array / ROM) carries its comptime contents on the Memory `init` pin. The
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
mod rom(rsel:u2) -> (z:u8@[0]) {
  const t:[4]u8 = (10,20,30,40)
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
mod ca(a:u8, wsel:u2, rsel:u2) -> (z:u8@[0]) {
  mut t:[4]u8 = (1,2,3,4)
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
mod srom(raddr:u2) -> (q:u4@[1]) {
  mut mem = (
    const addr=(raddr), const bits=4, const size=4, const din=(0),
    const enable=(1), const fwd=false, const type=1, const wensize=1,
    const rdport=(1), const init=$2,
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
       --set lec.engine=ind --workdir "$WORK/q_${1}_$$" 2>&1 \
    | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN\|INCONCLUSIVE" | head -1
}
verdict_pp() {  # $1=ref.prp $2=impl.prp $3=top -> PROVEN | REFUTED | UNKNOWN
  $LHD lec --ref "pyrope:$WORK/$1" --impl "pyrope:$WORK/$2" --top "$3" \
       --set lec.engine=ind --workdir "$WORK/qpp_$$_${RANDOM}" 2>&1 \
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

if [ $fail -ne 0 ]; then echo "lec_mem_init_test: FAILED"; exit 1; fi
echo "lec_mem_init_test: PASSED"
exit 0
