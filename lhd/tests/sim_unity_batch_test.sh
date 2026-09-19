#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Unity batching of the small generated simulator TUs (lhd_kernel_sim.cpp). A
# design with more than a handful of small generated translation units compiles
# them through `<simdir>/unity/unity-<k>.cpp` wrappers that `#include` the
# members, instead of one host-compiler process per few-KB file. This pins:
#
#   1. batching happens: the wrappers exist, build.ninja links their objects,
#      and there are fewer objects than generated sources;
#   2. the batched build simulates correctly;
#   3. a warm rebuild with nothing changed has no work left (the wrappers are
#      written only when their text changes, so their mtimes stay put);
#   4. an edit that rewrites a BATCHED source rebuilds the batch holding it --
#      the wrapper's depfile names every member. Without that the stale object
#      would be linked silently.
#
# Steps 3-4 need `ninja` on PATH (the same staleness rules also drive lhd's
# built-in fallback, but only ninja can answer "what would rebuild" without
# rebuilding).

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_unity_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Twenty distinct leaves, kept as separate modules (inline=false) so each is its
# own small generated TU, chained through one top: o = x + (0 + 1 + ... + 19).
N=20
{
  for k in $(seq 0 $((N - 1))); do
    echo "pub comb leaf_$k(a:u8) -> (s:u8) {"
    echo "  s = (a + $k)#[0..=7]"
    echo "}"
  done
} > "$W/leaves.prp"

{
  for k in $(seq 0 $((N - 1))); do
    echo "const leaf_$k = import(\"leaves.leaf_$k\")"
  done
  echo "pub comb top(x:u8) -> (o:u8) {"
  echo "  mut i0 = leaf_0::[name=i0](a = x)"
  for k in $(seq 1 $((N - 1))); do
    echo "  mut i$k = leaf_$k::[name=i$k](a = i$((k - 1)).s)"
  done
  echo "  o = i$((N - 1)).s"
  echo "}"
} > "$W/top.prp"

cat > "$W/tb.prp" <<'EOF'
const top = import("top.top")
test top.chain(cycles:u20 = 2) {
  mut dut = top
  mut got = 0
  tick cycles clocks=(clock=1) {
    dut.x = 16
    step
    got = dut.o
  }
  puts("unity chain: got={got}")
}
EOF

run_sim() {
  "$LHD" sim "$W/tb.prp" --set compile.upass.inline=false --diag-fmt pretty --workdir "$W/wd" >"$W/run.log" 2>&1
}

if ! run_sim; then
  if grep -q 'could not locate the sim runtime headers\|host C++ compiler' "$W/run.log"; then
    echo "PASS (no host sim toolchain here: $(tail -1 "$W/run.log"))"
    exit 0
  fi
  cat "$W/run.log" >&2
  fail "lhd sim failed"
fi

# ---- 1. batching happened ------------------------------------------------------
S="$W/wd/sim"
batches=("$S"/unity/unity-*.cpp)
[ -f "${batches[0]}" ] || fail "no unity batch was written under $S/unity"
grep -q 'unity-[0-9]*\.o' "$S/build.ninja" || fail "build.ninja does not link the unity batch objects"
sources=$(find "$S" -maxdepth 1 -name '*.cpp' ! -name 'drv.cpp' | wc -l)
objects=$(grep -c '^build .*\.o: cc ' "$S/build.ninja")
[ "$objects" -lt "$sources" ] || fail "batching did not reduce the TU count ($objects objects for $sources sources)"

# ---- 2. the batched build simulates correctly ---------------------------------
# 16 + (0 + 1 + ... + 19) = 206
grep -q 'unity chain: got=206' "$W/run.log" || { cat "$W/run.log" >&2; fail "wrong simulation result from the batched build"; }

if ! command -v ninja >/dev/null 2>&1; then
  echo "PASS (steps 1-2; no ninja on PATH, skipped the rebuild checks)"
  exit 0
fi

# ---- 3. a warm rebuild has no work --------------------------------------------
plan=$(ninja -C "$S" -n 2>&1 | grep -v '^ninja: Entering')
case "$plan" in
*"no work to do"*) ;;
*) fail "a rebuild with nothing changed still has work to do: $plan" ;;
esac

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
sed -e 's/pub comb leaf_7(a:u8)/pub comb leaf_7(a:u9)/' "$W/leaves.prp" > "$W/leaves.new" && mv "$W/leaves.new" "$W/leaves.prp"
grep -q 'leaf_7(a:u9)' "$W/leaves.prp" || fail "the interface edit did not apply (test bug)"
"$LHD" sim "$W/tb.prp" --set compile.upass.inline=false --setup-only --workdir "$W/wd" >"$W/setup.log" 2>&1 \
  || { cat "$W/setup.log" >&2; fail "lhd sim --setup-only failed after the edit"; }
[ -n "$(find "$S" -maxdepth 1 -name "$victim_cpp" -newer "$W/marker")" ] \
  || fail "the interface edit did not rewrite $victim_cpp (test assumption broken)"
# build.ninja is rewritten by the build step, not by setup: the plan below is
# computed from the PREVIOUS build file, exactly what the next build starts from.
plan=$(ninja -C "$S" -n 2>&1 | grep -v '^ninja: Entering')
case "$plan" in
*"$batch_obj"*) ;;
*) fail "rewriting $victim_cpp did not rebuild its batch $batch_obj -- the wrapper's depfile is not tracking members: $plan" ;;
esac

run_sim || { cat "$W/run.log" >&2; fail "lhd sim failed after the edit"; }
grep -q 'unity chain: got=206' "$W/run.log" || { cat "$W/run.log" >&2; fail "wrong simulation result after the edit"; }

echo "PASS (batched build, correct result, warm rebuild is a no-op, a member edit rebuilds its batch)"
