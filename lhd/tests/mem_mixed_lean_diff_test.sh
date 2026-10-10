#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# BEHAVIOURAL evidence for mixed-timing memory handling in pass.single_edge and
# pass.lean: the source RTL under iverilog against the NORMALIZED Lean
# certificate, compared at P=2 period boundaries.
#
# This is the gate the other memory tests cannot provide. checkDesign plus
# zero-input cycles writes nothing and so cannot see a wrong write; the
# classification test reports the pass's intent BEFORE the rewrite. Running
# this for the first time found a real defect: a slot-gated memory wrote only
# bit 0 of its data, because the enable rewrite AND-folded the 1-bit slot
# predicate into the port's per-bit write-lane mask and forced the result to
# one bit.
#
# WHY THE WRITE DATA IS REGISTERED. With `wdata` driven straight from a port it
# is stable across both microsteps of a period, so a write committing in BOTH
# slots stores the same value twice and an ungated memory is indistinguishable
# from a gated one -- measured: the gate-bypass negative still PASSED. `wq`
# advances at the rise, so an ungated write also commits at the fall and stores
# the NEW value. That is what makes the gate observable.
#
# Two evidence layers, kept apart on purpose:
#   STRUCTURAL   may pass without iverilog.
#   BEHAVIORAL_DIFF is PASS only when iverilog AND Lean both ran and matched.
#   ONLY A MISSING iverilog skips. An installed iverilog that cannot
#   elaborate, a simulation that exits nonzero, or an unreachable Lean
#   toolchain are FAILURES -- reporting those as "skipped" would let the
#   wrapper claim a structural pass while the behavioural gate had collapsed.
#
# NOT A BAZEL TARGET. It needs the Lean toolchain and the multi-GB .lake tree,
# which cannot be runfiles; as an sh_test it failed immediately in every
# sandbox. Run it directly:   bash lhd/tests/mem_mixed_lean_diff_test.sh
#
# OBSERVATION PHASE: directStepRaw returns PRE-transition outputs, so at P=2
# the sample is the state after slot 0 and before slot 1, and the RTL is
# sampled after the posedge and before the negedge. A slot-1 effect (the
# negedge element) is therefore seen on the NEXT sample.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; }
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RTL="$HERE/mem_mixed_rdclk_diff.v"
DIFF="$ROOT/scripts/mem_lean_diffsim.py"
[ -r "$RTL" ] || { echo "FAIL: missing fixture $RTL"; exit 1; }
[ -r "$DIFF" ] || { echo "FAIL: missing $DIFF"; exit 1; }
TOP=mem_mixed_rdclk_diff

T="${TEST_TMPDIR:-$ROOT/generated/tests}/mem_mixed_lean_diff"
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1
fails=0

# ---- STRUCTURAL ----------------------------------------------------------
"$LHD" compile verilog "$RTL" --top "$TOP" --reader yosys-slang \
  --workdir "$T/w" --emit-dir "lg:$T/lg" > "$T/compile.log" 2>&1 \
  || { echo "FAIL: the fixture did not import"; tail -3 "$T/compile.log"; exit 1; }
"$LHD" pass single_edge --top "$TOP" "lg:$T/lg" --emit-dir "lg:$T/lgn" \
  --set multi_clock=true --workdir "$T/se" > "$T/se.log" 2>&1 \
  || { echo "FAIL: single_edge refused the fixture"; exit 1; }
grep -q "P=2 slots" "$T/se.log" \
  && echo "STRUCTURAL ok: edge normalization ran with P=2" \
  || { echo "FAIL: expected P=2 slots"; fails=$((fails+1)); }
"$LHD" compile "lg:$T/lgn" --top "$TOP" --workdir "$T/lw" --emit-dir "lean:$T/lean" \
  --set formal.lean.mode=verified_compiler --set formal.lean.strict=true \
  > "$T/lean.log" 2>&1 \
  || { echo "FAIL: no certificate from the normalized graph"; tail -3 "$T/lean.log"; exit 1; }
echo "STRUCTURAL ok: certificate emitted from the normalized graph"
CERT="$T/lean/${TOP}_Lgraph.lean"
SIDE="$T/lean/${TOP}_io.json"

cat > "$T/init.v" <<'EOF'
    begin : zs
      integer k;
      for (k = 0; k < 16; k = k + 1) dut.mem[k] = 8'h00;
      dut.q3 = 8'h00; dut.q4 = 8'h00; dut.nq = 8'h00; dut.wq = 8'h00;
    end
EOF
python3 - "$T/s.json" <<'PYS'
import json, sys
# writes with a CHANGING wdata (so the registered value differs period to
# period), then async address sweeps, sync capture/hold, and a same-address
# write/read collision.
p = [{"we": 1, "waddr": a, "wdata": d, "nd": 0x10 + i}
     for i, (d, a) in enumerate([(0xA1,1),(0xB2,2),(0xC3,3),(0xD4,4),(0x5F,5)])]
p += [{"ra0":1,"ra1":2,"ra2":3,"nd":0x50},
      {"ra0":4,"ra1":5,"ra2":1,"nd":0x51},
      {"ra3":1,"ra4":2,"ra0":2,"nd":0x52},
      {"ra3":3,"ra4":4,"ra0":3,"nd":0x53},
      {"ra3":0,"ra4":0,"ra0":4,"nd":0x54},
      {"we":1,"waddr":7,"wdata":0x77,"ra0":7,"ra3":7,"nd":0x55},
      {"ra0":7,"ra1":7,"ra2":7,"ra3":7,"ra4":7,"nd":0x56}]
json.dump(p, open(sys.argv[1], "w"))
PYS

run_diff() {  # <cert> <sidecar> <outdir> <P>
  python3 "$DIFF" --cert "$1" --sidecar "$2" --rtl "$RTL" --top "$TOP" \
    --out "$3" --scenario "$T/s.json" --p "$4" --rtl-init "$T/init.v" \
    --lean-dir "$ROOT/formal/lean" 2>&1
}

# ---- BEHAVIORAL ----------------------------------------------------------
out="$(run_diff "$CERT" "$SIDE" "$T/pos" 2)"; prc=$?
echo "$out" | sed 's/^/    /'
if echo "$out" | grep -q "BEHAVIORAL_DIFF_SKIPPED"; then
  echo "BEHAVIORAL_DIFF_SKIPPED  (iverilog unavailable -- this run provides NO"
  echo "                          behavioural evidence; structural checks only)"
  [ "$fails" -eq 0 ] || exit 1
  echo "PASS: mem_mixed_lean_diff_test (STRUCTURAL only)"
  exit 0
fi
if [ "$prc" -ne 0 ] || ! echo "$out" | grep -q "BEHAVIORAL_DIFF_PASS"; then
  echo "FAIL: the normalized certificate does not match the RTL"; exit 1
fi

# ---- NEGATIVE CONTROLS ---------------------------------------------------
# Both must MISMATCH, or the positive above proves nothing.
neg="$(run_diff "$CERT" "$SIDE" "$T/neg_p1" 1)"
echo "$neg" | grep -q "BEHAVIORAL_DIFF_FAIL" \
  && echo "ok: NEGATIVE P=1 on a P=2 certificate mismatches" \
  || { echo "FAIL: running a P=2 certificate at P=1 still matched, so the"
       echo "      microstep alignment is not being tested"; fails=$((fails+1)); }

# Bypass the INSERTED slot gate, in the certificate (test side), not via a
# production switch: make both arms of the write-lane mux the real mask.
mkdir -p "$T/neg_gate"
python3 - "$CERT" "$T/neg_gate/cert.lean" <<'PYG'
import re, sys
src = open(sys.argv[1]).read()
wide = [m for m in re.findall(
    r'\{ op := LGraphOp\.Op_MuxN, width := (\d+), deps := #\[([^\]]*)\], origin := \d+ \}', src)
    if int(m[0]) > 1]
if len(wide) != 1:
    print(f"MUTATION-UNAVAILABLE expected one wide MuxN, found {len(wide)}"); sys.exit(2)
w, deps = wide[0]
a = [x.strip() for x in deps.split(",")]
old = f'{{ op := LGraphOp.Op_MuxN, width := {w}, deps := #[{deps}]'
new = f'{{ op := LGraphOp.Op_MuxN, width := {w}, deps := #[{a[0]}, {a[2]}, {a[2]}]'
assert src.count(old) == 1
open(sys.argv[2], "w").write(src.replace(old, new))
PYG
if [ $? -ne 0 ]; then
  echo "FAIL: could not build the gate-bypass mutant, so that control is untested"
  fails=$((fails+1))
else
  cp "$SIDE" "$T/neg_gate/"
  neg2="$(run_diff "$T/neg_gate/cert.lean" "$T/neg_gate/$(basename "$SIDE")" "$T/neg_gate/run" 2)"
  echo "$neg2" | grep -q "BEHAVIORAL_DIFF_FAIL" \
    && echo "ok: NEGATIVE bypassing the inserted memory slot gate mismatches" \
    || { echo "FAIL: bypassing the slot gate still matched, so the gate is not"
         echo "      observable in this scenario and the positive proves little"
         fails=$((fails+1)); }
fi

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: mem_mixed_lean_diff_test (STRUCTURAL + BEHAVIORAL_DIFF)"
