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
# arms differ on purpose: identical arms collapse in cprop (see the ruling
# below), which would take the obligation with them.
cat > "$W/overlap.prp" <<'PRP'
pub comb overlap(x:u2, a:u8, b:u8) -> (res:u8) {
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
pub comb overlap_match(x:u2, a:u8, b:u8, c:u8) -> (res:u8) {
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

# RULING: identical arms collapse, for a Hotmux exactly as for a Mux. An overlap
# that CANNOT change the result is not worth a decode cone plus its own ABC
# region, so cprop drops the cell and the one-hot obligation goes with it. This
# is the same design as overlap.prp above with one value instead of two: it must
# compile clean, report nothing, and leave no hotmux behind.
cat > "$W/identical.prp" <<'PRP'
pub comb identical(x:u2, a:u8) -> (res:u8) {
  mut res = a
  unique if x == 0 { res = a }
  elif x < 2 { res = a }
}
PRP
"$LHD" compile "$W/identical.prp" --workdir "$W/identical" \
    --emit "diagnostics:$W/identical.jsonl" --emit-dir "lg:$W/identical-lg"
! grep -q 'onehot' "$W/identical.jsonl"
"$LHD" tool cat "lg:$W/identical-lg" --diag-fmt pretty > "$W/identical.txt"
! grep -q 'hotmux' "$W/identical.txt"

# Exercise the same value/default selection in both generated simulators.
sed 's/hotmux_unique.dec/pairs.dec/' inou/prp/tests/equiv/hotmux_unique_tb.prp > "$W/pairs_tb.prp"
for backend in slop llvm; do
  "$LHD" sim "$W/pairs_tb.prp" --set "sim.backend=$backend" --workdir "$W/sim-$backend"
done
