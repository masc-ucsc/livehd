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

# sim.debug is a Slop knob (Slop_u validation landings in the reference C++
# kernels); pin that backend so the landings are in the generated body.
BE=(--set sim.tune.backend=slop)
"$LHD" sim "$PRP" --setup-only "${BE[@]}" --workdir "$work/setup" -q >/dev/null
body="$(ls "$work"/setup/sim/*kernel_multiwrite.cpp | head -1)"
plan="$(ls "$work"/setup/sim/*kernel_multiwrite.color-plan.txt | head -1)"

# A debug landing must change the generated artifacts in the same workdir.
! grep -q '::land(' "$body" || fail "default generated code retained a debug landing"
"$LHD" sim "$PRP" --setup-only "${BE[@]}" --set sim.debug=true --workdir "$work/setup" -q >/dev/null
grep -q '::land(' "$body" || fail "sim.debug=true did not invalidate the generated body"

# Changing the budget in the same workdir must invalidate generated artifacts.
"$LHD" sim "$PRP" --setup-only "${BE[@]}" --set sim.debug=true --set sim.tune.live_words=20 --workdir "$work/setup" -q >/dev/null
grep -Eq 'register-budget words=20 ' "$plan" || fail "explicit live-word budget did not replace the cached plan"

echo "PASS: debug and live-word options invalidate cached simulator artifacts"
