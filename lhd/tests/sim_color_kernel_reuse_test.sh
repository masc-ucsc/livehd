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

# Ordinary module boundaries may fuse. This fixture now checks the multi-write
# color directly: requiring two copies of a shared pair kernel would force the
# obsolete module-preserving partition policy.
grep -Eq 'register-budget words=256 ' "$plan" || fail "default live-word budget is missing from the plan"
grep -Eq 'Slop_u<[0-9]+> __color_tmp_[0-9]+ = Slop_u<[0-9]+>::from_proven' "$body" \
  || fail "proven-unsigned color values did not use the mask-free Slop_u landing by default"
! grep -q '::land(' "$body" || fail "default generated code retained a debug Slop_u landing mask"
grep -Eq 'add_op\([^;]*__color_tmp_[0-9]+[),]' "$body" \
  || fail "a downstream mixed Slop/Slop_u operation reconverted its unsigned temporary"

# sim.debug keeps the old materializing landing available for proof debugging;
# it must not leak into the production/default generated code above. Reuse the
# same workdir so this also guards the incremental generation digest.
"$LHD" sim "$PRP" --setup-only --set sim.debug=true --workdir "$work/setup" -q >/dev/null
grep -Eq 'Slop_u<[0-9]+> __color_tmp_[0-9]+ = Slop_u<[0-9]+>::land' "$body" \
  || fail "sim.debug=true did not retain the checked Slop_u landing"

# Run both generated backends. A mismatch between ABI write order and
# changed-bit order swaps or starves one of the exact-value assertions. Each is
# ~7s of host clang in its own workdir and they share nothing, so build them
# side by side; the header/body greps below read the setup-only tree.
"$LHD" sim "$PRP" --workdir "$work/serial" -q >/dev/null &
serial_pid=$!
"$LHD" sim "$PRP" --set sim.backend=llvm --workdir "$work/llvm" -q >/dev/null &
llvm_pid=$!

# Unsigned GraphIO and state are canonical boundaries too. A u8 flop feeding a
# same-width equality used to become `ar0.zext_to<8>()`, even though both the
# member and compare accept Slop_u<8> directly.
grep -q 'Slop_u<8> ar0{};  // flop' "$header" || fail "proven-unsigned flop was not stored as Slop_u"
grep -q 'Slop_u<8> d{};' "$header" || fail "proven-unsigned input was not stored as Slop_u"
grep -q 'Slop_u<1> a_zero{};' "$header" || fail "proven-unsigned output was not stored as Slop_u"
# `x == 0` is lnot_op: the compare reads the unsigned state DIRECTLY and never
# materializes a full-width zero to compare it against.
grep -q 'lnot_op(ar0)' "$body" || fail "unsigned state did not feed the zero test directly"
! grep -q 'eq_op(ar0, Slop<8>::create_integer(0))' "$body" \
  || fail "the zero test still materializes a full-width zero constant"
! grep -Eq 'ar0\.zext_to<8>|reset\.zext_to<1>' "$body" \
  || fail "same-width state/input conversion survived Slop_u storage"

wait "$serial_pid" || fail "serial-backend simulation failed"
wait "$llvm_pid" || fail "llvm-backend simulation failed"

# Changing the budget in the same workdir must invalidate generated artifacts.
"$LHD" sim "$PRP" --setup-only --set sim.debug=true --set sim.live_words=20 --workdir "$work/setup" -q >/dev/null
grep -Eq 'register-budget words=20 ' "$plan" || fail "explicit live-word budget did not replace the cached plan"

echo "PASS: fused multi-write colors preserve unsigned storage and exact values in Slop and LLVM"
