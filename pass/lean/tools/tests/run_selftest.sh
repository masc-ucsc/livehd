#!/usr/bin/env bash
set -euo pipefail
# Self-test of `cert_run` (pass/lean/tools/drivers/cert_run.lean, HDLProof/Run/*): running a
# program on a design. Formats: pass/lean/tools/formats/run.md and bind.md.
#
# Fixtures, through the real flow (slang reader -> LGraph -> DCERT4):
#   busm   a small CPU on a valid/ready bus: 32-bit instructions (load immediate, add, store,
#          load, decrement, jump if not zero), every output registered. Its program prints
#          "ABBB" on the console, writes 0x2a to the register `sig` and exits with code 0.
#   busm_mealy  busm with an output that is `mem_ready` itself (a Mealy output)
#   busm_proto  busm that moves the address while a data request waits (a protocol error)
#   seqm   a sequencer reading an 8-bit RAM at its counter; seqm_init loads the same RAM with
#          $readmemh at power-on
# Checks:
#   load      the program as ELF, as .bin at 0 and as $readmemh .hex: same console, register,
#             exit code and steps.trace
#   latency   zero, fixed:3 and random:7:5 give the same console, register and exit code
#   replay    cert_trace on the run's inputs.stim prints its steps.trace (ir and vc), so the
#             closed-loop run is the open-loop trace of the ticks it applied (runCycles_sound)
#   routes    route:vc, route:ref and route:fp (also with opt:off) give the same steps.trace as ir;
#             lockstep:vc, and route:fp lockstep:ir, run to exit
#   ckpt      a checkpoint at cycle 30 (with the host in its ext section) resumes to the same
#             end: steps.trace from cycle 30, console, register, exit code, final checkpoint;
#             route:fp writes the same checkpoints and resumes the ir route's
#   stops     limit, stop-output, breakpoint, watchpoint, bus-error, rom-write, protocol, mealy,
#             exit with a nonzero code (exit code 1)
#   RTL load  seqm with load:prog8.hex:mem0, and with load:prog8.bin@0:mem0, prints what seqm_init
#             prints; a word past the memory's depth is refused
#   vcd       the VCD names every port and has value changes
#
# CK_NAT=<dir with native cert_run and cert_trace> uses them; otherwise the drivers run
# interpreted against formal/lean/.olean-dev (build it with formal/lean/.build-proj.sh).
# Needs python3 for the program images.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/run_selftest}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
CK_NAT="${CK_NAT:-}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
command -v python3 >/dev/null || { echo "python3 not on PATH" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/busm.v" <<'EOF'
module busm(input clk, input resetn,
            output reg mem_valid, output reg mem_instr, output reg [31:0] mem_addr,
            output reg [31:0] mem_wdata, output reg [3:0] mem_wstrb,
            input mem_ready, input [31:0] mem_rdata,
            output [31:0] acc_o, output [31:0] pc_o);
  reg [31:0] pc;
  reg [31:0] acc;
  reg [31:0] ir;
  reg [1:0] st;
  assign acc_o = acc;
  assign pc_o = pc;
  always @(posedge clk) begin
    if (!resetn) begin
      pc <= 32'd0; acc <= 32'd0; ir <= 32'd0; st <= 2'd0;
      mem_valid <= 1'b0; mem_instr <= 1'b0; mem_addr <= 32'd0; mem_wdata <= 32'd0; mem_wstrb <= 4'd0;
    end else begin
      case (st)
        2'd0: begin mem_valid <= 1'b1; mem_instr <= 1'b1; mem_addr <= pc; mem_wstrb <= 4'd0; st <= 2'd1; end
        2'd1: if (mem_ready) begin
          mem_valid <= 1'b0; mem_instr <= 1'b0; ir <= mem_rdata; pc <= pc + 32'd4;
          case (mem_rdata[31:28])
            4'h0: begin acc <= {4'd0, mem_rdata[27:0]}; st <= 2'd0; end
            4'h1: begin acc <= acc + {4'd0, mem_rdata[27:0]}; st <= 2'd0; end
            4'h2: begin mem_valid <= 1'b1; mem_addr <= {4'd0, mem_rdata[27:0]}; mem_wdata <= acc; mem_wstrb <= 4'hf; st <= 2'd2; end
            4'h3: begin mem_valid <= 1'b1; mem_addr <= {4'd0, mem_rdata[27:0]}; mem_wstrb <= 4'd0; st <= 2'd2; end
            4'h4: begin if (acc != 32'd0) pc <= {4'd0, mem_rdata[27:0]}; st <= 2'd0; end
            4'h5: begin acc <= acc - 32'd1; st <= 2'd0; end
            default: st <= 2'd0;
          endcase
        end
        2'd2: if (mem_ready) begin
          mem_valid <= 1'b0; mem_wstrb <= 4'd0;
          if (ir[31:28] == 4'h3) acc <= mem_rdata;
          st <= 2'd0;
        end
        default: st <= 2'd0;
      endcase
    end
  end
endmodule
EOF
sed -e 's/module busm(/module busm_mealy(/' -e 's/output \[31:0\] acc_o, output \[31:0\] pc_o);/output [31:0] acc_o, output [31:0] pc_o, output rdy_o);\n  assign rdy_o = mem_ready;/' \
  "$OUT/busm.v" > "$OUT/busm_mealy.v"
sed -e 's/module busm(/module busm_proto(/' \
  -e "s/        2'd2: if (mem_ready) begin/        2'd2: if (!mem_ready) mem_addr <= mem_addr + 32'd4; else begin/" \
  "$OUT/busm.v" > "$OUT/busm_proto.v"
grep -q 'rdy_o = mem_ready' "$OUT/busm_mealy.v" && grep -q 'mem_addr + 32.d4; else' "$OUT/busm_proto.v" \
  || { echo "FAIL: the fixture variants were not derived"; exit 1; }
cat > "$OUT/seqm.v" <<'EOF'
module seqm(input clk, input we, input [3:0] wa, input [7:0] wd, output [7:0] q, output [3:0] pc_o);
  reg [3:0] pc = 4'd0;
  reg [7:0] m [0:11];
  always @(posedge clk) begin
    pc <= (pc == 4'd11) ? 4'd0 : pc + 4'd1;
    if (we) m[wa] <= wd;
  end
  assign q = m[pc];
  assign pc_o = pc;
endmodule
EOF
sed -e 's/module seqm(/module seqm_init(/' -e 's|  reg \[7:0\] m \[0:11\];|  reg [7:0] m [0:11];\n  initial $readmemh("prog8.hex", m);|' \
  "$OUT/seqm.v" > "$OUT/seqm_init.v"

python3 -I - "$OUT" <<'EOF'
import struct, sys
out = sys.argv[1]
def ins(op, imm): return (op << 28) | imm
CON, EXIT, SIG = 0x0F000000, 0x0F000004, 0x0F000008
def prog(words): return b''.join(struct.pack('<I', w) for w in words)
main = [ins(0,0x41), ins(2,CON), ins(0,3), ins(2,0x100),
        ins(0,0x42), ins(2,CON), ins(3,0x100), ins(5,0), ins(2,0x100), ins(4,0x10),
        ins(0,0x2A), ins(2,SIG), ins(0,0), ins(2,EXIT)]
data = prog(main)
open(f'{out}/prog.bin', 'wb').write(data)
open(f'{out}/prog.hex', 'w').write('// the same program as $readmemh words\n' +
                                   '\n'.join('%08x' % w for w in main) + '\n')
ehdr = struct.pack('<4sBBBBB7sHHIIIIIHHHHHH', b'\x7fELF', 1, 1, 1, 0, 0, b'\0'*7,
                   2, 243, 1, 0, 52, 0, 0, 52, 32, 1, 0, 0, 0)
phdr = struct.pack('<IIIIIIII', 1, 84, 0, 0, len(data), len(data) + 64, 5, 4)
open(f'{out}/prog.elf', 'wb').write(ehdr + phdr + data)
# a store to an address no region or device names; exit code 7
open(f'{out}/buserr.bin', 'wb').write(prog([ins(0,1), ins(2,0x0E000000), ins(0,0), ins(2,EXIT)]))
open(f'{out}/exit7.bin', 'wb').write(prog([ins(0,7), ins(2,EXIT)]))
p8 = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc]
open(f'{out}/prog8.hex', 'w').write(' '.join('%02x' % v for v in p8) + '\n')
open(f'{out}/prog8.bin', 'wb').write(bytes(p8))
open(f'{out}/prog8_long.hex', 'w').write(' '.join('%02x' % v for v in p8 + [0xdd]) + '\n')
EOF

for d in busm busm_mealy busm_proto seqm seqm_init; do
  (cd "$OUT" && "$LHD" compile "$d.v" --reader slang --top "$d" --emit-dir "lg:${d}_lg" --workdir "${d}_cw" \
     > "$d.compile.log" 2>&1) || { tail -5 "$OUT/$d.compile.log"; echo "FAIL: compile $d"; exit 1; }
  (cd "$OUT" && "$LHD" compile "lg:${d}_lg" --top "$d" --workdir "${d}_lw" --emit-dir "lean:${d}_lean" \
     --set formal.lean.mode=verified_compiler > "$d.lean.log" 2>&1) \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; echo "FAIL: certificate $d"; exit 1; }
done
B="$OUT/busm_lean/busm.dcert"

cat > "$OUT/busm.bind" <<'EOF'
bind v1
# the reset is low for 4 cycles
reset resetn 0 4
bus m valid=mem_valid addr=mem_addr wdata=mem_wdata wstrb=mem_wstrb instr=mem_instr ready=mem_ready rdata=mem_rdata
ram 0x0 0x1000
console 0x0F000000
exit 0x0F000004
word 0x0F000008 sig
EOF
bindv() { # <name> <sed expression>: a variant of busm.bind
  sed -e "$2" "$OUT/busm.bind" > "$OUT/$1.bind"
}
bindv lat3 's/^bus m \(.*\)$/bus m \1 latency=fixed:3/'
bindv latr 's/^bus m \(.*\)$/bus m \1 latency=random:7:5/'
bindv lat2 's/^bus m \(.*\)$/bus m \1 latency=fixed:2/'
bindv rom 's/^ram 0x0 0x1000$/rom 0x0 0x1000/'
bindv brk '$a break 0x10'
bindv wat '$a watch 0x100'
bindv stp '$a stop-output pc_o 0x10'

N=0
drv() { # <native name> <source> args... -> RC, LOG
  local name=$1 src=$2; shift 2
  LOG="$OUT/run_$(( ++N )).log"
  if [[ -n "$CK_NAT" ]]; then
    "$CK_NAT/$name" "$@" > "$LOG" 2>&1 && RC=0 || RC=$?
  else
    (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$src" "$@") > "$LOG" 2>&1 && RC=0 || RC=$?
  fi
}
run() { drv cert_run "$ROOT/pass/lean/tools/drivers/cert_run.lean" "$@"; }
ct() { drv cert_trace "$ROOT/formal/lean/Futamura_Projection_Scripts/cert_trace.lean" "$@"; }
expect() { # <exit code> <status> args... (cert_run)
  local want=$1 st=$2; shift 2
  run "$@"
  if [[ "$RC" != "$want" ]] || ! grep -qx "RESULT: $st" "$LOG"; then
    cat "$LOG"; echo "FAIL: cert_run $* -- expected exit $want and RESULT: $st, got exit $RC"; exit 1
  fi
}
has() { grep -qx -- "$1" "$LOG" || { cat "$LOG"; echo "FAIL: the run lacks the line '$1'"; exit 1; }; }
summary() { grep -E '^(status|cycles|exit_code|console|word) ' "$LOG"; }

# load: ELF, bin, hex
expect 0 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" trace:"$OUT/t_elf" vcd:"$OUT/busm.vcd" max:2000
has "console ABBB"; has "word sig 2a"; has "exit_code 0"
summary > "$OUT/elf.summary"
CYC=$(grep -o '^cycles [0-9]*' "$LOG" | cut -d' ' -f2)
expect 0 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.bin@0" trace:"$OUT/t_bin" max:2000
summary | cmp -s - "$OUT/elf.summary" || { cat "$LOG"; echo "FAIL: bin run differs from elf run"; exit 1; }
expect 0 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.hex" trace:"$OUT/t_hex" max:2000
summary | cmp -s - "$OUT/elf.summary" || { cat "$LOG"; echo "FAIL: hex run differs from elf run"; exit 1; }
for t in t_bin t_hex; do
  cmp -s "$OUT/t_elf/steps.trace" "$OUT/$t/steps.trace" || { echo "FAIL: $t steps.trace differs"; exit 1; }
done

# latency
for b in lat3 latr; do
  expect 0 exit "$B" bind:"$OUT/$b.bind" load:"$OUT/prog.elf" max:5000
  has "console ABBB"; has "word sig 2a"; has "exit_code 0"
  grep -q "^cycles $CYC\$" "$LOG" && { cat "$LOG"; echo "FAIL: $b took as many cycles as latency zero"; exit 1; }
done

# replay: the recorded inputs drive cert_trace open-loop to the same outputs
for rt in ir vc; do
  ct "$B" "$CYC" "$OUT/t_elf/inputs.stim" route:$rt
  [[ $RC == 0 ]] && cmp -s "$LOG" "$OUT/t_elf/steps.trace" \
    || { diff "$LOG" "$OUT/t_elf/steps.trace" | head; echo "FAIL: replay of inputs.stim on $rt"; exit 1; }
done
expect 0 exit "$B" bind:"$OUT/latr.bind" load:"$OUT/prog.elf" trace:"$OUT/t_latr" max:5000
CYCR=$(grep -o '^cycles [0-9]*' "$LOG" | cut -d' ' -f2)
ct "$B" "$CYCR" "$OUT/t_latr/inputs.stim"
cmp -s "$LOG" "$OUT/t_latr/steps.trace" || { echo "FAIL: replay of the random-latency run"; exit 1; }

# routes and lockstep
for rt in vc ref fp; do
  expect 0 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" route:$rt trace:"$OUT/t_$rt" max:2000
  cmp -s "$OUT/t_elf/steps.trace" "$OUT/t_$rt/steps.trace" || { echo "FAIL: route:$rt steps.trace"; exit 1; }
done
expect 0 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" route:fp opt:off trace:"$OUT/t_fp_off" max:2000
cmp -s "$OUT/t_elf/steps.trace" "$OUT/t_fp_off/steps.trace" || { echo "FAIL: route:fp opt:off steps.trace"; exit 1; }
expect 0 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" lockstep:vc max:2000
expect 0 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" route:fp lockstep:ir max:2000

# checkpoint and resume, the host in the ext section
expect 0 exit "$B" bind:"$OUT/lat2.bind" load:"$OUT/prog.elf" trace:"$OUT/t_full" ckpt-at:30:"$OUT/c30.ckpt" ckpt-out:"$OUT/cend.ckpt" max:5000
summary > "$OUT/full.summary"
grep -q '^ext [0-9]* cert_run host v1 ' "$OUT/c30.ckpt" || { echo "FAIL: no host section in the checkpoint"; exit 1; }
expect 0 exit "$B" bind:"$OUT/lat2.bind" ckpt-in:"$OUT/c30.ckpt" trace:"$OUT/t_res" ckpt-out:"$OUT/cres.ckpt" max:5000
summary | cmp -s - "$OUT/full.summary" || { cat "$LOG"; echo "FAIL: the resumed run ends elsewhere"; exit 1; }
cmp -s <(sed -n '31,$p' "$OUT/t_full/steps.trace") "$OUT/t_res/steps.trace" || { echo "FAIL: resumed steps.trace"; exit 1; }
cmp -s "$OUT/cend.ckpt" "$OUT/cres.ckpt" || { echo "FAIL: the resumed run's final checkpoint differs"; exit 1; }
# route:fp: the same checkpoints as ir (its state agrees with ir's below every depth), and it
# resumes ir's checkpoint to the same end
expect 0 exit "$B" bind:"$OUT/lat2.bind" load:"$OUT/prog.elf" route:fp trace:"$OUT/t_fpfull" ckpt-at:30:"$OUT/c30fp.ckpt" ckpt-out:"$OUT/cendfp.ckpt" max:5000
summary | cmp -s - "$OUT/full.summary" || { cat "$LOG"; echo "FAIL: route:fp ends elsewhere"; exit 1; }
cmp -s "$OUT/t_full/steps.trace" "$OUT/t_fpfull/steps.trace" || { echo "FAIL: route:fp steps.trace (lat2)"; exit 1; }
cmp -s "$OUT/c30.ckpt" "$OUT/c30fp.ckpt" && cmp -s "$OUT/cend.ckpt" "$OUT/cendfp.ckpt" \
  || { echo "FAIL: route:fp checkpoints differ from ir's"; exit 1; }
expect 0 exit "$B" bind:"$OUT/lat2.bind" ckpt-in:"$OUT/c30.ckpt" route:fp trace:"$OUT/t_resfp" ckpt-out:"$OUT/cresfp.ckpt" max:5000
cmp -s "$OUT/t_res/steps.trace" "$OUT/t_resfp/steps.trace" && cmp -s "$OUT/cend.ckpt" "$OUT/cresfp.ckpt" \
  || { echo "FAIL: route:fp resumed from ir's checkpoint differs"; exit 1; }

# compaction (compact:<K>) changes no output: the same steps.trace, summary and final checkpoint
for k in 1 7; do
  expect 0 exit "$B" bind:"$OUT/lat2.bind" load:"$OUT/prog.elf" trace:"$OUT/t_c$k" ckpt-out:"$OUT/cc$k.ckpt" max:5000 compact:$k
  grep -q "^compactions [1-9][0-9]* (every $k cycles)$" "$LOG" || { cat "$LOG"; echo "FAIL: compact:$k made no compaction"; exit 1; }
  summary | cmp -s - "$OUT/full.summary" || { cat "$LOG"; echo "FAIL: compact:$k ends elsewhere"; exit 1; }
  cmp -s "$OUT/t_full/steps.trace" "$OUT/t_c$k/steps.trace" || { echo "FAIL: compact:$k steps.trace"; exit 1; }
  cmp -s "$OUT/cend.ckpt" "$OUT/cc$k.ckpt" || { echo "FAIL: compact:$k final checkpoint"; exit 1; }
done
# writes to the design's own memory every few cycles (seqm), on ir, vc and fp, with and without compaction
python3 -I - "$OUT/seqm_lean/seqm_io.json" "$OUT/seqw.stim" <<'PY'
import json, random, sys
ins = {i['name']: i for i in json.load(open(sys.argv[1]))['inputs']}
r = random.Random(7)
lines = ['stim v1']
for k in range(80):
    we = 1 if r.random() < 0.4 else 0
    for n, v in (('we', we), ('wa', r.randrange(12)), ('wd', r.randrange(256))):
        lines.append(f"at {k} in {ins[n]['ordinal']} {ins[n]['width']} {v}")
open(sys.argv[2], 'w').write('\n'.join(lines) + '\n')
PY
for rt in ir vc fp; do
  run "$OUT/seqm_lean/seqm.dcert" stim:"$OUT/seqw.stim" route:$rt trace:"$OUT/t_sw_$rt" ckpt-out:"$OUT/sw_$rt.ckpt" max:80
  [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: seqm writes, $rt"; exit 1; }
  run "$OUT/seqm_lean/seqm.dcert" stim:"$OUT/seqw.stim" route:$rt trace:"$OUT/t_swc_$rt" ckpt-out:"$OUT/swc_$rt.ckpt" max:80 compact:3
  [[ $RC == 0 ]] && has "compactions 26 (every 3 cycles)" || { cat "$LOG"; echo "FAIL: seqm writes, $rt, compact:3"; exit 1; }
  cmp -s "$OUT/t_sw_$rt/steps.trace" "$OUT/t_swc_$rt/steps.trace" || { echo "FAIL: seqm compact:3 steps.trace ($rt)"; exit 1; }
  cmp -s "$OUT/sw_$rt.ckpt" "$OUT/swc_$rt.ckpt" || { echo "FAIL: seqm compact:3 final checkpoint ($rt)"; exit 1; }
done
cmp -s "$OUT/t_sw_ir/steps.trace" "$OUT/t_sw_vc/steps.trace" || { echo "FAIL: seqm writes, ir and vc differ"; exit 1; }
cmp -s "$OUT/t_sw_ir/steps.trace" "$OUT/t_sw_fp/steps.trace" && cmp -s "$OUT/sw_ir.ckpt" "$OUT/sw_fp.ckpt" \
  || { echo "FAIL: seqm writes, ir and fp differ (trace or final checkpoint)"; exit 1; }
[[ $(cut -d' ' -f3 "$OUT/t_sw_ir/steps.trace" | sort -u | wc -l) -gt 3 ]] || { echo "FAIL: seqm writes reach no output"; exit 1; }
# cert_trace compact:<K> (ir, vc incremental, vc inc:off): the same lines as without, and as cert_run's
for mode in "route:ir" "route:vc" "route:vc inc:off"; do
  ct "$OUT/seqm_lean/seqm.dcert" 80 "$OUT/seqw.stim" $mode
  grep '^STEP' "$LOG" > "$OUT/ct_plain.txt"
  ct "$OUT/seqm_lean/seqm.dcert" 80 "$OUT/seqw.stim" $mode compact:3
  [[ $RC == 0 ]] && has "compactions 26 (every 3 cycles)" || { cat "$LOG"; echo "FAIL: cert_trace $mode compact:3"; exit 1; }
  cmp -s "$OUT/ct_plain.txt" <(grep '^STEP' "$LOG") || { echo "FAIL: cert_trace $mode compact:3 changed the trace"; exit 1; }
  cmp -s "$OUT/ct_plain.txt" "$OUT/t_sw_ir/steps.trace" || { echo "FAIL: cert_trace $mode differs from cert_run"; exit 1; }
done
# automatic compaction (the default past 256 cycles): 600 cycles of writes compact at 256 and
# 512 and print what compact:0 prints, on both drivers (cert_run: ir, vc and fp)
python3 -I - "$OUT/seqm_lean/seqm_io.json" "$OUT/seqw600.stim" <<'PY'
import json, random, sys
ins = {i['name']: i for i in json.load(open(sys.argv[1]))['inputs']}
r = random.Random(11)
lines = ['stim v1']
for k in range(600):
    we = 1 if r.random() < 0.4 else 0
    for n, v in (('we', we), ('wa', r.randrange(12)), ('wd', r.randrange(256))):
        lines.append(f"at {k} in {ins[n]['ordinal']} {ins[n]['width']} {v}")
open(sys.argv[2], 'w').write('\n'.join(lines) + '\n')
PY
for rt in ir vc fp; do
  run "$OUT/seqm_lean/seqm.dcert" stim:"$OUT/seqw600.stim" route:$rt trace:"$OUT/t_sa_$rt" ckpt-out:"$OUT/sa_$rt.ckpt" max:600
  [[ $RC == 0 ]] && has "compactions 2 (every 256 cycles, automatic)" || { cat "$LOG"; echo "FAIL: seqm automatic compaction, $rt"; exit 1; }
  run "$OUT/seqm_lean/seqm.dcert" stim:"$OUT/seqw600.stim" route:$rt trace:"$OUT/t_s0_$rt" ckpt-out:"$OUT/s0_$rt.ckpt" max:600 compact:0
  [[ $RC == 0 ]] && ! grep -q '^compactions' "$LOG" || { cat "$LOG"; echo "FAIL: seqm compact:0 compacted, $rt"; exit 1; }
  cmp -s "$OUT/t_sa_$rt/steps.trace" "$OUT/t_s0_$rt/steps.trace" || { echo "FAIL: seqm automatic compaction steps.trace ($rt)"; exit 1; }
  cmp -s "$OUT/sa_$rt.ckpt" "$OUT/s0_$rt.ckpt" || { echo "FAIL: seqm automatic compaction final checkpoint ($rt)"; exit 1; }
done
for mode in "route:ir" "route:vc"; do
  ct "$OUT/seqm_lean/seqm.dcert" 600 "$OUT/seqw600.stim" $mode
  [[ $RC == 0 ]] && has "compactions 2 (every 256 cycles, automatic)" || { cat "$LOG"; echo "FAIL: cert_trace $mode automatic compaction"; exit 1; }
  grep '^STEP' "$LOG" > "$OUT/ct_auto.txt"
  ct "$OUT/seqm_lean/seqm.dcert" 600 "$OUT/seqw600.stim" $mode compact:0
  [[ $RC == 0 ]] && ! grep -q '^compactions' "$LOG" || { cat "$LOG"; echo "FAIL: cert_trace $mode compact:0 compacted"; exit 1; }
  cmp -s "$OUT/ct_auto.txt" <(grep '^STEP' "$LOG") || { echo "FAIL: cert_trace $mode automatic compaction changed the trace"; exit 1; }
  cmp -s "$OUT/ct_auto.txt" "$OUT/t_sa_ir/steps.trace" || { echo "FAIL: cert_trace $mode differs from cert_run (600 cycles)"; exit 1; }
done

# stops
expect 0 limit "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" max:20
has "cycles 20"
expect 0 stop-output "$B" bind:"$OUT/stp.bind" load:"$OUT/prog.elf" max:2000
expect 0 stop-output "$B" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" stop:mem_wstrb=15 max:2000
expect 0 breakpoint "$B" bind:"$OUT/brk.bind" load:"$OUT/prog.elf" max:2000
expect 0 watchpoint "$B" bind:"$OUT/wat.bind" load:"$OUT/prog.elf" max:2000
expect 1 bus-error "$B" bind:"$OUT/busm.bind" load:"$OUT/buserr.bin@0" max:2000
expect 1 rom-write "$B" bind:"$OUT/rom.bind" load:"$OUT/prog.elf" max:2000
expect 1 exit "$B" bind:"$OUT/busm.bind" load:"$OUT/exit7.bin@0" max:2000
has "exit_code 7"
sed 's/^bus m \(.*\)$/bus m \1 latency=fixed:2/' "$OUT/busm.bind" > "$OUT/proto.bind"
expect 1 protocol "$OUT/busm_proto_lean/busm_proto.dcert" bind:"$OUT/proto.bind" load:"$OUT/prog.elf" max:2000
expect 1 mealy "$OUT/busm_mealy_lean/busm_mealy.dcert" bind:"$OUT/busm.bind" load:"$OUT/prog.elf" max:2000
# usage and refusals
run "$B" bind:"$OUT/busm.bind" frob
[[ $RC == 2 ]] || { cat "$LOG"; echo "FAIL: an unknown option"; exit 1; }
printf 'bind v1\nbus m valid=nosuch addr=mem_addr ready=mem_ready rdata=mem_rdata\n' > "$OUT/bad.bind"
run "$B" bind:"$OUT/bad.bind"
[[ $RC == 2 ]] && grep -q 'no output `nosuch`' "$LOG" || { cat "$LOG"; echo "FAIL: a bind file naming no port"; exit 1; }

# a program in the design's own memory
run "$OUT/seqm_init_lean/seqm_init.dcert" trace:"$OUT/t_init" max:30
[[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: seqm_init"; exit 1; }
for l in "prog8.hex:mem0" "prog8.bin@0:mem0"; do
  run "$OUT/seqm_lean/seqm.dcert" load:"$OUT/$l" trace:"$OUT/t_mem" max:30
  [[ $RC == 0 ]] && grep -q '^loaded .*: 12 words into memory 0' "$LOG" || { cat "$LOG"; echo "FAIL: seqm load:$l"; exit 1; }
  cmp -s "$OUT/t_init/steps.trace" "$OUT/t_mem/steps.trace" \
    || { diff "$OUT/t_init/steps.trace" "$OUT/t_mem/steps.trace" | head; echo "FAIL: load:$l differs from \$readmemh"; exit 1; }
done
grep -q '^STEP 3 3 68$' "$OUT/t_mem/steps.trace" || { head -5 "$OUT/t_mem/steps.trace"; echo "FAIL: seqm reads"; exit 1; }
run "$OUT/seqm_lean/seqm.dcert" load:"$OUT/prog8_long.hex:mem0" max:5
[[ $RC == 3 ]] && grep -q "past memory 0's depth 12" "$LOG" || { cat "$LOG"; echo "FAIL: a word past depth was loaded"; exit 1; }

# vcd
grep -q '^\$var wire 1 .* mem_valid \$end$' "$OUT/busm.vcd" && grep -q '^\$var wire 32 .* mem_rdata \$end$' "$OUT/busm.vcd" \
  && [[ $(grep -c '^#' "$OUT/busm.vcd") -gt 10 ]] || { head -20 "$OUT/busm.vcd"; echo "FAIL: vcd"; exit 1; }

echo "PASS run_selftest: $N driver runs"
