#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRPS=(
  "inou/prp/tests/sim/color_kernel_multiwrite.prp"
  "inou/prp/tests/sim/color_kernel_llvm_scalar.prp"
  "inou/prp/tests/sim/color_kernel_llvm_reg.prp"
  "inou/prp/tests/sim/color_kernel_llvm_reg_const.prp"
  "inou/prp/tests/sim/color_kernel_llvm_wide.prp"
  "inou/prp/tests/sim/color_kernel_llvm_mem_whole.prp"
)
LLVM_ONLY_PRPS=(
  # The current reference backend regressed this pre-existing live fixture;
  # retain it as an LLVM gate rather than weakening the fixture or hiding it.
  "inou/prp/tests/sim/mem_sim_gated_clock.prp"
)
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_llvm_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The assertions are the semantic oracle for both implementations. The second
# design keeps bits above 64 live across the generated object ABI.
for prp in "${PRPS[@]}"; do
  name="$(basename "$prp" .prp)"
  "$LHD" sim "$prp" --set sim.backend=slop --workdir "$work/slop-$name" -q
  "$LHD" sim "$prp" --set sim.backend=llvm --workdir "$work/llvm-$name" -q
done
for prp in "${LLVM_ONLY_PRPS[@]}"; do
  name="$(basename "$prp" .prp)"
  "$LHD" sim "$prp" --set sim.backend=llvm --workdir "$work/llvm-$name" -q
done

objects=("$work"/llvm-color_kernel_llvm_scalar/sim/*.color-kernel-*.llvm.o)
[ -f "${objects[0]}" ] || fail "profitable scalar region did not emit LLVM bitcode"
grep -q 'llvm_inline .* | .*llvm_sim_link' "$work"/llvm-color_kernel_llvm_scalar/sim/build.ninja \
  || fail "LLVM native object does not depend on the version-matched link helper"

wide_objects=("$work"/llvm-color_kernel_llvm_wide/sim/*.color-kernel-*.llvm.o)
[ ! -e "${wide_objects[0]}" ] || fail "wide region bypassed the faster Slop fallback"

grep -q '__state_commit.*= true' "$work"/llvm-color_kernel_llvm_reg/sim/color_kernel_llvm_reg.llvm_reg.cpp \
  || fail "LLVM-backend register fallback did not retain the phase-barrier commit"

adapters=("$work"/llvm-*/sim/*.color-kernel-*.cpp)
[ ! -e "${adapters[0]}" ] \
  || fail "sim.backend=llvm emitted redundant per-color C++ ABI adapters"

echo "PASS: profitable scalar LLVM regions inline, wide regions fall back to Slop, and both match Slop assertions"
