#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Unity batching of the small generated simulator TUs (lhd_kernel_sim.cpp). A
# design with more than a handful of small generated translation units compiles
# them through `<simdir>/unity/unity-<k>.cpp` wrappers that `#include` the
# members, instead of one host-compiler process per few-KB file. This pins:
#
#   1. batching happens: the wrappers exist, their objects are built,
#      and there are fewer objects than generated sources;
#   2. the batched build simulates correctly;
#   3. a warm rebuild with nothing changed has no work left (the wrappers are
#      written only when their text changes, so their mtimes stay put);
#   4. an edit that rewrites a BATCHED source rebuilds the batch holding it --
#      the wrapper's depfile names every member. Without that the stale object
#      would be linked silently.
#
# Steps 3-4 inspect object timestamps.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_unity_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Fourteen distinct leaves keep more than twelve generated TUs after the
# identity leaf is removed, enough for two unity buckets and the leaf_7 edit.
# Keep the same batching, warm-build, dependency, and execution checks.
N=14
{
  for k in $(seq 0 $((N - 1))); do
    echo "pub comb leaf_$k(a:U8) -> (s:U8) {"
    echo "  s = (a + $k)#[0..=7]"
    echo "}"
  done
} > "$W/leaves.prp"

{
  for k in $(seq 0 $((N - 1))); do
    echo "const leaf_$k = import(\"leaves.leaf_$k\")"
  done
  echo "pub comb top(x:U8) -> (o:U8) {"
  echo "  mut inst0 = leaf_0::[name=inst0](a = x)"
  for k in $(seq 1 $((N - 1))); do
    echo "  mut inst$k = leaf_$k::[name=inst$k](a = inst$((k - 1)).s)"
  done
  echo "  o = inst$((N - 1)).s"
  echo "}"
} > "$W/top.prp"

cat > "$W/tb.prp" <<'EOF'
const top = import("top.top")
test top.chain(cycles:U20 = 2) {
  mut dut = top
  mut got = 0
  tick cycles {
    dut.x = 16
    step
    got = dut.o
  }
  puts("unity chain: got={got}")
}
EOF

run_sim() {
  "$LHD" sim "$W/tb.prp" --set compile.upass.inline=false --set sim.tune.profile=off --diag-fmt pretty --workdir "$W/wd" >"$W/run.log" 2>&1
}

if ! run_sim; then
  cat "$W/run.log" >&2
  fail "lhd sim failed"
fi

# ---- 1. batching happened ------------------------------------------------------
S="$W/wd/sim"
batches=("$S"/unity/unity-*.cpp)
[ -f "${batches[0]}" ] || fail "no unity batch was written under $S/unity"
ls "$S"/obj/unity-*.o >/dev/null 2>&1 || fail "the unity batch objects were not built"
sources=$(find "$S" -maxdepth 1 -name '*.cpp' ! -name 'drv.cpp' | wc -l)
objects=$(find "$S/obj" -name '*.o' | wc -l)
[ "$objects" -lt "$sources" ] || fail "batching did not reduce the TU count ($objects objects for $sources sources)"

# ---- 2. the batched build simulates correctly ---------------------------------
# 16 + (0 + 1 + ... + 13) = 107
grep -q 'unity chain: got=107' "$W/run.log" || { cat "$W/run.log" >&2; fail "wrong simulation result from the batched build"; }

# ---- 3. a warm rebuild has no work ----------------------------------
touch "$W/build-marker"
run_sim || { cat "$W/run.log" >&2; fail "warm build failed"; }
[ -z "$(find "$S" \( -name '*.o' -o -name drv.bin \) -newer "$W/build-marker")" ] \
  || fail "a warm build rewrote objects or relinked the simulator"

# ---- 4. rewriting a batched source rebuilds its batch -------------------------
# Widen one leaf's input port: its interface -- and so its generated sources --
# change, whatever else the plan does.
victim_cpp=""
for f in "$S"/leaves.leaf_7*.cpp; do
  base=$(basename "$f")
  if grep -lq "\"\.\./$base\"" "$S"/unity/unity-*.cpp; then
    victim_cpp="$base"
    break
  fi
done
[ -n "$victim_cpp" ] || fail "no generated source of leaf_7 was batched (test assumption broken): $(ls "$S"/unity)"
batch_obj=$(basename "$(grep -l "\"\.\./$victim_cpp\"" "$S"/unity/unity-*.cpp)" .cpp).o

touch "$W/marker"
sed -e 's/pub comb leaf_7(a:U8)/pub comb leaf_7(a:U9)/' "$W/leaves.prp" > "$W/leaves.new" && mv "$W/leaves.new" "$W/leaves.prp"
grep -q 'leaf_7(a:U9)' "$W/leaves.prp" || fail "the interface edit did not apply (test bug)"
"$LHD" sim "$W/tb.prp" --set compile.upass.inline=false --setup-only --workdir "$W/wd" >"$W/setup.log" 2>&1 \
  || { cat "$W/setup.log" >&2; fail "lhd sim --setup-only failed after the edit"; }
[ -n "$(find "$S" -maxdepth 1 -name "$victim_cpp" -newer "$W/marker")" ] \
  || fail "the interface edit did not rewrite $victim_cpp (test assumption broken)"
touch "$W/build-marker"
run_sim || { cat "$W/run.log" >&2; fail "lhd sim failed after the edit"; }
[ -n "$(find "$S" -name "$batch_obj" -newer "$W/build-marker")" ] \
  || fail "rewriting $victim_cpp did not rebuild its batch $batch_obj"
grep -q 'unity chain: got=107' "$W/run.log" || { cat "$W/run.log" >&2; fail "wrong simulation result after the edit"; }

echo "PASS (batched build, correct result, warm rebuild is a no-op, a member edit rebuilds its batch)"
