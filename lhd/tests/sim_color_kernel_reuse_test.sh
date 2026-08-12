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
plan="$(ls "$work"/setup/sim/*kernel_multiwrite.color-plan.txt | head -1)"

grep -Eq 'kernel-reuses=[1-9][0-9]*' "$plan" || fail "the repeated pair occurrences did not canonicalize"
kernel=""
kernel_file=""
for candidate in $(sed -n 's/.*= &\(__lhd_color_kernel_[^;]*\);/\1/p' "$body" | sort | uniq -d); do
  candidate_file="$(grep -l "std::uint64_t $candidate(void\*\*" "$work"/setup/sim/*.color-kernel-*.cpp | head -1)"
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

# Run both generated backends. A mismatch between canonical ABI write order and
# changed-bit order swaps or starves one of the four exact-value assertions.
"$LHD" sim "$PRP" --workdir "$work/serial" -q >/dev/null
"$LHD" sim "$PRP" --set sim.workers=2 --workdir "$work/taskflow" -q >/dev/null

echo "PASS: canonical multi-write kernel is emitted once, bound twice, and preserves ABI changed-bit order"
