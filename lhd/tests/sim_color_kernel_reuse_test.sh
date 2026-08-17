#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRP="inou/prp/tests/sim/color_kernel_multiwrite.prp"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_kernel_reuse_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" sim "$PRP" --setup-only --workdir "$work/setup" -q >/dev/null
body="$(ls "$work"/setup/sim/*kernel_multiwrite.cpp | head -1)"
header="$(ls "$work"/setup/sim/*kernel_multiwrite.hpp | head -1)"
plan="$(ls "$work"/setup/sim/*kernel_multiwrite.color-plan.txt | head -1)"

grep -Eq 'kernel-reuses=[1-9][0-9]*' "$plan" || fail "the repeated pair occurrences did not canonicalize"
kernel=""
kernel_file=""
for candidate in $(sed -n 's/.*= &\(__lhd_color_kernel_[^;]*\);/\1/p' "$body" | sort | uniq -d); do
  # A kernel returns void and takes (owner, bindings, changed-bitset): the
  # changed word is an OUT parameter, so a color with more than 64 boundary
  # writes is expressible. `|| true` keeps `set -e`/`pipefail` from killing the
  # loop (silently, with no diagnostic) on a candidate that matches nothing.
  candidate_file="$(grep -l "void $candidate(\[\[maybe_unused\]\] void\* __owner" \
    "$work"/setup/sim/*.color-kernel-*.cpp | head -1 || true)"
  if [ -n "$candidate_file" ] && grep -q 'std::uint64_t{1} << 1' "$candidate_file"; then
    kernel="$candidate"
    kernel_file="$candidate_file"
    break
  fi
done
[ -n "$kernel" ] || fail "no canonical kernel function is bound by multiple colors"
[ "$(grep -c "= &$kernel;" "$body")" -eq 2 ] || fail "the two pair occurrences do not share exactly one bound kernel"
[ -n "$kernel_file" ] || fail "the shared digest-named kernel translation unit is missing"
grep -q 'std::uint64_t{1} << 0' "$kernel_file" || fail "shared multi-write kernel lacks changed bit zero"
grep -q 'std::uint64_t{1} << 1' "$kernel_file" || fail "shared multi-write kernel lacks changed bit one"
grep -Eq 'Slop_u<[0-9]+> __k_tmp_[0-9]+ = Slop_u<[0-9]+>::from_proven' "$kernel_file" \
  || fail "proven-unsigned kernel outputs did not use the mask-free Slop_u landing by default"
! grep -q '::land(' "$kernel_file" || fail "default generated code retained a debug Slop_u landing mask"
grep -Eq 'add_op\([^;]*__k_tmp_[0-9]+[),]' "$kernel_file" \
  || fail "a downstream mixed Slop/Slop_u operation reconverted its unsigned temporary"

# sim.debug keeps the old materializing landing available for proof debugging;
# it must not leak into the production/default generated code above. Reuse the
# same workdir so this also guards the incremental generation digest.
"$LHD" sim "$PRP" --setup-only --set sim.debug=true --workdir "$work/setup" -q >/dev/null
grep -Eq 'Slop_u<[0-9]+> __k_tmp_[0-9]+ = Slop_u<[0-9]+>::land' "$work"/setup/sim/*.color-kernel-*.cpp \
  || fail "sim.debug=true did not retain the checked Slop_u landing"

# Run the generated serial backend. A mismatch between canonical ABI write
# order and changed-bit order swaps or starves one of the exact-value assertions.
"$LHD" sim "$PRP" --workdir "$work/serial" -q >/dev/null

# Unsigned GraphIO and state are canonical boundaries too. A u8 flop feeding a
# same-width equality used to become `ar0.zext_to<8>()`, even though both the
# member and compare accept Slop_u<8> directly.
grep -q 'Slop_u<8> ar0{};  // flop' "$header" || fail "proven-unsigned flop was not stored as Slop_u"
grep -q 'Slop_u<8> d{};' "$header" || fail "proven-unsigned input was not stored as Slop_u"
grep -q 'Slop_u<1> a_zero{};' "$header" || fail "proven-unsigned output was not stored as Slop_u"
grep -q 'eq_op(ar0, Slop<8>::create_integer(0))' "$body" || fail "unsigned state did not feed equality directly"
! grep -Eq 'ar0\.zext_to<8>|reset\.zext_to<1>' "$body" \
  || fail "same-width state/input conversion survived Slop_u storage"

echo "PASS: canonical multi-write kernel is emitted once, bound twice, and preserves ABI changed-bit order"
