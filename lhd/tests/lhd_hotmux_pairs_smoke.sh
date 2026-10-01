#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_hotmux_pairs_$$}"
mkdir -p "$W"
# The `unique if` design under test is the existing equiv fixture, not a second
# copy of it: this script already drives that fixture's testbench below, so a
# private transcription would only be able to drift away from it. Copying it to
# `pairs.prp` renames the module to `pairs.dec` (module names are `<file>.<mod>`).
cp inou/prp/tests/equiv/hotmux_unique.prp "$W/pairs.prp"
"$LHD" compile "$W/pairs.prp" --workdir "$W/compile" --emit-dir "lg:$W/lg" --emit "verilog:$W/out.v"
"$LHD" tool cat "lg:$W/lg" --diag-fmt pretty > "$W/graph.txt"
python3 - "$W/graph.txt" <<'PY'
import re
import sys
text = open(sys.argv[1]).read()
ops = re.findall(r'^  ([a-z_]+)_\d+', text, re.M)
assert sorted(ops) == ['eq', 'eq', 'eq', 'hotmux'], text
hot = text[text.index('  hotmux_'):]
for pid, value in [(1, 'a'), (3, 'b'), (5, 'c'), (6, 'd')]:
    assert re.search(rf'\.p{pid}\s+bits=8\s+<- \${value}\b', hot), hot
for pid in [0, 2, 4]:
    assert re.search(rf'\.p{pid}\s+bits=1\s+<- eq_', hot), hot
PY
cat > "$W/ref.v" <<'VERILOG'
module dec(input [1:0] x, input [7:0] a,b,c,d, output [7:0] res);
assign res = x==0 ? a : x==1 ? b : x==2 ? c : d;
endmodule
VERILOG
"$LHD" lec --impl "lg:$W/lg" --ref "verilog:$W/ref.v" --impl-top pairs.dec --ref-top dec --workdir "$W/lec"
# Overlapping controls that CAN change the result must reach pass.formal. The
# arms differ here; identical arms keep the obligation too (see the ruling
# below).
cat > "$W/overlap.prp" <<'PRP'
pub comb overlap(x:U2, a:U8, b:U8) -> (res:U8) {
  mut res = a
  unique if x == 0 { res = a }
  elif x < 2 { res = b }
}
PRP
if "$LHD" compile "$W/overlap.prp" --workdir "$W/overlap" --emit "diagnostics:$W/overlap.jsonl" --emit-dir "lg:$W/overlap-lg"; then
  echo 'overlapping controls unexpectedly compiled' >&2
  exit 1
fi
grep -q 'onehot-violated' "$W/overlap.jsonl"

# The same exclusivity contract applies to match.
cat > "$W/overlap_match.prp" <<'PRP'
pub comb overlap_match(x:U2, a:U8, b:U8, c:U8) -> (res:U8) {
  mut res = a
  match x {
    == 1 { res = a }
    < 2 { res = b }
    else { res = c }
  }
}
PRP
if "$LHD" compile "$W/overlap_match.prp" --workdir "$W/overlap-match" \
    --emit "diagnostics:$W/overlap-match.jsonl" --emit-dir "lg:$W/overlap-match-lg"; then
  echo 'overlapping match controls unexpectedly compiled' >&2
  exit 1
fi
grep -q 'onehot-violated' "$W/overlap-match.jsonl"

# RULING (muxopt.md, pass/cprop/README.md): equal data alone is not a proof of
# exclusivity, so identical arms do NOT erase an unproven `unique if` overlap.
# This is overlap.prp above with one value instead of two: the one-hot
# obligation survives cprop and the compile must still report the overlap.
cat > "$W/identical.prp" <<'PRP'
pub comb identical(x:U2, a:U8) -> (res:U8) {
  mut res = a
  unique if x == 0 { res = a }
  elif x < 2 { res = a }
}
PRP
if "$LHD" compile "$W/identical.prp" --workdir "$W/identical" \
    --emit "diagnostics:$W/identical.jsonl" --emit-dir "lg:$W/identical-lg"; then
  echo 'overlapping identical-arm controls unexpectedly compiled' >&2
  exit 1
fi
grep -q 'onehot-violated' "$W/identical.jsonl"

# Exercise the same value/default selection in both generated simulators.
sed 's/hotmux_unique.dec/pairs.dec/' inou/prp/tests/equiv/hotmux_unique_tb.prp > "$W/pairs_tb.prp"
# Each backend is a ~7s host build in its own workdir and they share nothing, so
# build them side by side and collect the two exit codes afterwards.
for backend in slop llvm; do
  "$LHD" sim "$W/pairs_tb.prp" --set "sim.tune.backend=$backend" --workdir "$W/sim-$backend" &
  eval "sim_${backend}_pid=$!"
done
for backend in slop llvm; do
  eval "wait \"\${sim_${backend}_pid}\"" || { echo "FAIL: $backend simulation failed" >&2; exit 1; }
done
