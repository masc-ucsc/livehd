#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for `pass.color reduce`: repeated combinational cones are
# extracted into ONE shared def instantiated at every site, in place -- fewer
# total nodes, one Verilog module where there were N copies.
#
#   prp -> lg
#   reference Verilog from the untouched library
#   lhd pass color reduce       (min_count defaults to 3)
#   Verilog from the reduced library: a pat_* module, instantiated 3x
#   lhd lec (reduced vs original): must be LEC-equivalent
#
# reduce REWRITES the library (like absorb), so the property a unit test cannot
# give is the one that matters: the design still computes exactly what it did.
set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_color_reduce_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

# Three copies of the same 6-node cone on distinct inputs. The cone is bigger
# than the min_nodes floor (4) and repeats exactly min_count (3) times.
cat > "$W/red3.prp" <<'EOF'
mod top(a:u8, b:u8, c:u8, d:u8, e:u8, f:u8) -> (x:u8@[0], y:u8@[0], z:u8@[0]) {
  wrap x = ((a ^ b) + (a & b)) ^ (a | b)
  wrap y = ((c ^ d) + (c & d)) ^ (c | d)
  wrap z = ((e ^ f) + (e & f)) ^ (e | f)
}
EOF
TOP=red3.top

D="$W/main"
mkdir -p "$D"
run compile "$W/red3.prp" --top "$TOP" --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/ref.v" --workdir "$D/w2"

grep -q "^module pat_" "$D/ref.v" && fail "reference Verilog already holds a pat_ module"

run pass color reduce --top "$TOP" --stats lg:"$D/lg" --workdir "$D/w3"
run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/post.v" --workdir "$D/w4"

# One shared module, instantiated at all three sites.
[ "$(grep -c '^module pat_' "$D/post.v")" = "1" ] || fail "expected exactly one pat_ module"
[ "$(grep -c '^pat_' "$D/post.v")" = "3" ] || fail "expected the pattern instantiated 3x"

# ... and the design still computes the same function. This is the whole point.
run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top top --workdir "$D/c"
echo "PASS: repeated cones extracted to one shared def, LEC-equivalent"

# A second run is a no-op: everything extractable is already an instance.
run pass color reduce --top "$TOP" --stats lg:"$D/lg" --workdir "$D/w5"
run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/post2.v" --workdir "$D/w6"
cmp -s "$D/post.v" "$D/post2.v" || fail "second reduce run changed the design"
echo "PASS: reduce is idempotent"

# Constant PARAMETERIZATION (the unrolled-loop shape): three cones identical
# except for one literal collapse to one def with a const input port, each site
# feeding its own value -- and the function is untouched.
cat > "$W/redc.prp" <<'EOF'
mod top(a:u8, b:u8, c:u8, d:u8, e:u8, f:u8) -> (x:u8@[0], y:u8@[0], z:u8@[0]) {
  wrap x = ((a ^ b) + (a & b)) ^ (a | 12)
  wrap y = ((c ^ d) + (c & d)) ^ (c | 13)
  wrap z = ((e ^ f) + (e & f)) ^ (e | 14)
}
EOF
D="$W/constparam"
mkdir -p "$D"
run compile "$W/redc.prp" --top redc.top --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
run compile lg:"$D/lg" --top redc.top --recipe O0 --emit verilog:"$D/ref.v" --workdir "$D/w2"
run pass color reduce --top redc.top --stats lg:"$D/lg" --workdir "$D/w3"
run compile lg:"$D/lg" --top redc.top --recipe O0 --emit verilog:"$D/post.v" --workdir "$D/w4"
[ "$(grep -c '^module pat_' "$D/post.v")" = "1" ] || fail "const-divergent cones must share ONE def"
[ "$(grep -c '^pat_' "$D/post.v")" = "3" ] || fail "expected the const pattern instantiated 3x"
grep -q '\.c0(' "$D/post.v" || fail "expected a promoted const port (.c0) on the instances"
run lec --set formal.solver=lgyosys --impl verilog:"$D/post.v" --ref verilog:"$D/ref.v" --top top --workdir "$D/c"
echo "PASS: const-parameterized pattern extracted and LEC-equivalent"

# min_count above the occurrence count leaves the library untouched.
D="$W/under"
mkdir -p "$D"
run compile "$W/red3.prp" --top "$TOP" --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
run pass color reduce --top "$TOP" --set color.min_count=4 lg:"$D/lg" --workdir "$D/w2"
run compile lg:"$D/lg" --top "$TOP" --recipe O0 --emit verilog:"$D/off.v" --workdir "$D/w3"
grep -q "^module pat_" "$D/off.v" && fail "min_count=4 still extracted a 3-site pattern"
echo "PASS: min_count gates extraction"

# reduce rewrites in place: --emit-dir lg: must be refused, not half-obeyed.
D="$W/emitdir"
mkdir -p "$D"
run compile "$W/red3.prp" --top "$TOP" --recipe O1 --emit-dir lg:"$D/lg" --workdir "$D/w1"
if "$LHD" pass color reduce --top "$TOP" lg:"$D/lg" --emit-dir lg:"$D/out" --workdir "$D/w2" -q --result-json "$W/r.json" 2>/dev/null; then
  fail "reduce with --emit-dir lg: must be a usage error"
fi
echo "PASS: reduce refuses --emit-dir lg:"

echo "PASS: all pass.color reduce flows"
