#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Incremental compile + `lhd sim`: a default generic specialization (named after its template) survives a partial graph
# restore, and the regenerated simulator executes the edited sibling (checked against an lhd.incremental=false oracle).
# (The generated-C++ rewrite checks this test used to carry went with the C++ driver: `lhd sim` writes no sources now.)

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_incr_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ======================== part `identity`: step 8 ==========================
# ---- 8. default generic specialization: partial graph restore + sim regen ----
# The compile-level twin lives in lhd_compile_cache_test.sh (identity block);
# this step adds the `lhd sim` regenerate-and-execute obligation.
# A default generic specialization shares its template's name. A partial
# graph restore must retain both lowered trees, then regenerate and execute
# the root using a freshly changed sibling. This is Minion's one-module edit
# shape, reduced to an observable arithmetic result.
IW="$W/identity"
mkdir -p "$IW"
cat > "$IW/g.prp" <<'EOF'
pub mod madd<W=8>(a:U8, b:U8) -> (r:U8@[0]) { r = (a ^ b) & ((1 << W) - 1) }
EOF
cat > "$IW/leaf.prp" <<'EOF'
pub comb bump(a:U8) -> (r:U8) { wrap r = a + 1 }
EOF
cat > "$IW/tb.prp" <<'EOF'
const madd = import("g.madd")
const leaf = import("leaf")
pub mod top(x:U8) -> (y:U8@[0]) {
  y = madd(a=leaf.bump(a=x), b=x)
}
test top.identity(expected:U8=3) {
  mut dut = top
  tick 2 {
    dut.x = 1
    step
    assert(dut.y == expected, "default specialization must use the edited sibling")
  }
}
EOF
identity_run() {
  local tag=$1 wd=$2 expected=$3
  shift 3
  "$LHD" sim "$IW/tb.prp" --workdir "$wd" "+expected=$expected" \
    --set lhd.jobs=4 --result-json "$IW/$tag.json" "$@" >"$IW/$tag.log" 2>&1 \
    || { cat "$IW/$tag.log" >&2; fail "default-specialization simulation failed ($tag)"; }
}
identity_run cold "$IW/w" 3
sed 's/a + 1/a + 2/' "$IW/leaf.prp" > "$IW/leaf.new" && mv "$IW/leaf.new" "$IW/leaf.prp"
identity_run edit "$IW/w" 2
python3 - "$IW/edit.json" <<'PY' || fail "default-specialization edit did not exercise partial graph reuse"
import json, sys
cache = json.load(open(sys.argv[1]))['incremental']['compile']
assert cache['hits'] > 0 and cache['misses'] > 0 and cache['refused'] == 0, cache
PY
identity_run oracle "$IW/oracle" 2 --set lhd.incremental=false

echo "PASS (step 8): default specialization uses the edited sibling after partial graph reuse"

# The testbench object's verified content key survives an unchanged setup. A
# damaged object or a testbench-only source change must each force regeneration.
object="$IW/w/sim/drv.tb.o"
touch -t 200001010101 "$object"
identity_run warm "$IW/w" 2 --set sim.dump_bitcode=true --set sim.vcd=true
python3 - "$IW/w/sim" <<'PYBC' || fail "missing or invalid LLVM bitcode"
import pathlib, sys
objects = list(pathlib.Path(sys.argv[1]).glob("*.o"))
assert any(p.name.endswith(".llvm.o") for p in objects)
assert {"drv.tb.o", "drv.tb.trace.o"}.issubset(p.name for p in objects)
for path in objects:
    assert path.with_suffix(".bc").read_bytes()[:4] in (b"BC\xc0\xde", b"\xde\xc0\x17\x0b"), path
PYBC
[ "$object" -ot "$IW/warm.json" ] || fail "unchanged object was republished"
python3 - "$object" <<'PY' || fail "testbench object cache missed"
import os, sys
assert os.stat(sys.argv[1]).st_mtime < 1000000000
PY
cp "$object" "$IW/clean.o"
echo damaged > "$object"
identity_run repair "$IW/w" 2
compgen -G "$IW/w/sim/*.bc" >/dev/null && fail "obsolete bitcode survived setup"
cmp "$object" "$IW/clean.o" || fail "did not restore the correct native object"
sed 's/dut.y == expected/dut.y + 1 == expected/' "$IW/tb.prp" > "$IW/tb.new" && mv "$IW/tb.new" "$IW/tb.prp"
identity_run tbedit "$IW/w" 3
cmp -s "$object" "$IW/clean.o" && fail "testbench-only edit reused stale code"
exit 0
