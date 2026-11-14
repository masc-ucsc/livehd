#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.aegraph through the lhd kernel (todo/livehd/2a-aegraph.html, milestone B):
#  - determinism: compiling twice gives structurally identical libraries
#    (`tool diff --structural`),
#  - the engine's own summary line names the run in the pass log,
#  - the rules keep the design: `lhd lec` proves the Verilog equal to a compile
#    with every rule group off,
#  - the compile cache keys on the cost profile (a profile switch never replays
#    the other profile's graphs).

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

RULES_OFF=compile.aegraph.rules_disable=group:arith+group:div+group:mask+group:mux+group:shift

compile() {  # compile <tag> <src> <top> [--set k=v ...]
  local tag=$1 src=$2 top=$3
  shift 3
  "$LHD" compile "$src" --top "$top" --set lhd.incremental=false "$@" \
    --emit-dir lg:"$W/$tag/lg" --emit verilog:"$W/$tag/out.v" --workdir "$W/$tag/w" -q --result-json "$W/$tag.json" \
    || fail "$tag: compile $* failed: $(cat "$W/$tag.json" 2>/dev/null)"
}

for design in "$W/dup.prp:dup" "inou/prp/tests/equiv/loop_carry_kinds.prp:top"; do
  src=${design%%:*}
  top=${design##*:}
  name=$(basename "${src%.prp}")

  compile "${name}_a" "$src" "$top"
  compile "${name}_b" "$src" "$top"
  compile "${name}_c" "$src" "$top" --set "$RULES_OFF"

  # Determinism: two cold runs give the same library, structurally.
  "$LHD" tool diff lg:"$W/${name}_a/lg" lg:"$W/${name}_b/lg" --structural >"$W/${name}_diff.out" 2>&1 \
    || fail "$name: two aegraph runs differ: $(head -c 800 "$W/${name}_diff.out")"

  # The engine ran in both cprop slots and left its summary in the pass log.
  [ "$(ls "$W/${name}_a/w/logs/" | grep -c 'pass_aegraph')" -eq 2 ] || fail "$name: expected two pass.aegraph step logs"
  grep -q '^\[pass.aegraph\] graphs=' "$W/${name}_a/w/logs/"*pass_aegraph.log || fail "$name: no [pass.aegraph] summary line"

  # Same design with or without the rules: LEC the Verilog against the rules-off compile.
  "$LHD" lec --ref verilog:"$W/${name}_c/out.v" --impl verilog:"$W/${name}_a/out.v" --top "$top" \
    --set lhd.incremental=false --workdir "$W/lec_${name}" -q --result-json "$W/lec_${name}.json" >/dev/null 2>&1
  rc=$?
  [ "$rc" -eq 0 ] || fail "$name: the rules changed the design (lec rc=$rc): $(head -c 600 "$W/lec_${name}.json")"
done

# The engine shares duplicated subexpressions: the summary must report the merges.
grep -h '^\[pass.aegraph\] graphs=' "$W/dup_a/w/logs/"*pass_aegraph.log | head -1 | grep -Eq 'cse=[1-9]' \
  || fail "dup: the engine reported no shared subexpression: $(cat "$W/dup_a/w/logs/"*pass_aegraph.log)"

# The compile cache keys on the cost profile (it keeps the latest one per workdir): a profile switch misses,
# a repeat of the same profile hits.
CW="$W/cache"
step=0
for cost in synth synth sim sim; do
  step=$((step + 1))
  "$LHD" compile "$W/dup.prp" --top dup --set compile.aegraph.cost="$cost" --workdir "$CW" -q --result-json "$W/cache_$step.json" \
    --emit verilog:"$W/cache_$step.v" >/dev/null 2>&1 || fail "cache: compile cost=$cost failed"
  if [ "$step" -eq 1 ] || [ "$step" -eq 3 ]; then
    grep -q '"compile":{"enabled":true,"hits":0' "$W/cache_$step.json" \
      || fail "cache: the first cost=$cost run replayed another profile's graph: $(cat "$W/cache_$step.json")"
  else
    grep -Eq '"compile":\{"enabled":true,"hits":[1-9]' "$W/cache_$step.json" \
      || fail "cache: a repeated cost=$cost run did not hit: $(cat "$W/cache_$step.json")"
  fi
done

echo "PASS: pass.aegraph is deterministic, shares duplicates, keeps the design, and keys the compile cache on its profile"
