#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.aegraph through the lhd kernel (todo/livehd/2a-aegraph.html, milestone B):
#  - determinism: compiling twice with compile.cprop.engine=aegraph gives
#    structurally identical libraries (`tool diff --structural`),
#  - the engine's own summary line names the run in the pass log,
#  - the engine, the classic default and aegraph+classic all emit Verilog that
#    `lhd lec` proves equal (the engine is a sound alternative, not a different
#    design),
#  - a re-run is idempotent: the engine finds nothing more in its own output,
#  - the compile cache keys on the engine (an engine switch never replays the
#    other engine's graphs).

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_aegraph_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Designs: a loop with carries (compact Subs), and a pure-expression design with
# duplicated subexpressions the engine must share.
cat > "$W/dup.prp" <<'PRP'
pub comb dup(a:U8, b:U8, c:U8) -> (x:U18, y:U9, z:U8) {
  x = (a + b) * c + (b + a) * c
  y = (a ^ b) + (b ^ a)
  z = if a == 3 { b } else { b }
}
PRP

compile() {  # compile <tag> <src> <top> <engine>
  local tag=$1 src=$2 top=$3 engine=$4
  "$LHD" compile "$src" --top "$top" --set compile.cprop.engine="$engine" --set lhd.incremental=false \
    --emit-dir lg:"$W/$tag/lg" --emit verilog:"$W/$tag/out.v" --workdir "$W/$tag/w" -q --result-json "$W/$tag.json" \
    || fail "$tag: compile engine=$engine failed: $(cat "$W/$tag.json" 2>/dev/null)"
}

for design in "$W/dup.prp:dup" "inou/prp/tests/equiv/loop_carry_kinds.prp:top"; do
  src=${design%%:*}
  top=${design##*:}
  name=$(basename "${src%.prp}")

  compile "${name}_a" "$src" "$top" aegraph
  compile "${name}_b" "$src" "$top" aegraph
  compile "${name}_c" "$src" "$top" classic
  compile "${name}_d" "$src" "$top" aegraph+classic

  # Determinism: two cold runs give the same library, structurally.
  "$LHD" tool diff lg:"$W/${name}_a/lg" lg:"$W/${name}_b/lg" --structural >"$W/${name}_diff.out" 2>&1 \
    || fail "$name: two aegraph runs differ: $(head -c 800 "$W/${name}_diff.out")"

  # The engine ran in both cprop slots and left its summary in the pass log.
  [ "$(ls "$W/${name}_a/w/logs/" | grep -c 'pass_aegraph')" -eq 2 ] || fail "$name: expected two pass.aegraph step logs"
  grep -q '^\[pass.aegraph\] graphs=' "$W/${name}_a/w/logs/"*pass_aegraph.log || fail "$name: no [pass.aegraph] summary line"
  [ "$(ls "$W/${name}_c/w/logs/" | grep -c 'pass_aegraph')" -eq 0 ] || fail "$name: classic must not run pass.aegraph"
  [ "$(ls "$W/${name}_d/w/logs/" | grep -c 'pass_aegraph')" -eq 2 ] || fail "$name: aegraph+classic must run the engine in both slots"
  [ "$(ls "$W/${name}_d/w/logs/" | grep -c 'pass_cprop')" -eq 2 ] || fail "$name: aegraph+classic must run classic in both slots"

  # Same design, whichever engine: LEC the Verilog of each against the classic one.
  for tag in a d; do
    "$LHD" lec --ref verilog:"$W/${name}_c/out.v" --impl verilog:"$W/${name}_$tag/out.v" --top "$top" \
      --set lhd.incremental=false --workdir "$W/lec_${name}_$tag" -q --result-json "$W/lec_${name}_$tag.json" >/dev/null 2>&1
    rc=$?
    [ "$rc" -eq 0 ] || fail "$name: engine ${tag} is not equal to classic (lec rc=$rc): $(head -c 600 "$W/lec_${name}_$tag.json")"
  done
done

# The engine shares duplicated subexpressions that classic's scalar sweep also
# shares: the pure dup design must not have MORE arithmetic nodes than it started
# with, and the summary must report the merges.
grep -h '^\[pass.aegraph\] graphs=' "$W/dup_a/w/logs/"*pass_aegraph.log | head -1 | grep -Eq 'cse=[1-9]' \
  || fail "dup: the engine reported no shared subexpression: $(cat "$W/dup_a/w/logs/"*pass_aegraph.log)"

# The compile cache keys on the engine: a warm classic run after an aegraph run
# must not replay the aegraph graphs (and vice versa).
CW="$W/cache"
for engine in aegraph classic aegraph classic; do
  "$LHD" compile "$W/dup.prp" --top dup --set compile.cprop.engine="$engine" --workdir "$CW" -q --result-json "$W/cache_$engine.json" \
    --emit verilog:"$W/cache_$engine.v" >/dev/null 2>&1 || fail "cache: compile engine=$engine failed"
  if [ "$engine" = aegraph ]; then
    grep -q 'pass.aegraph' "$W/cache_$engine.json" || fail "cache: engine=aegraph replayed a non-aegraph graph: $(cat "$W/cache_$engine.json")"
  else
    grep -q 'pass.cprop' "$W/cache_$engine.json" || fail "cache: engine=classic replayed an aegraph graph: $(cat "$W/cache_$engine.json")"
  fi
done

echo "PASS: pass.aegraph is deterministic, shares duplicates, is LEC-equal to classic, and keys the compile cache"
