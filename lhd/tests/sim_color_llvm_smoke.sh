#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRPS=(
  "inou/prp/tests/sim/color_kernel_multiwrite.prp"
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

# An unmatched glob would otherwise hand grep a literal pattern, and its exit 2
# reads exactly like "clean" -- the check would silently stop testing anything.
kernel_bodies=("$work"/llvm-*/sim/*.color-kernel-*.cpp)
[ -f "${kernel_bodies[0]}" ] || fail "sim.backend=llvm emitted no color-kernel bodies at all"
if grep -q 'LLVM fallback' "${kernel_bodies[@]}"; then
  fail "sim.backend=llvm left a Slop color body in a focused regression"
fi

objects=("$work"/llvm-color_kernel_llvm_wide/sim/*.color-kernel-*.llvm.o)
[ -f "${objects[0]}" ] || fail "sim.backend=llvm did not emit a native color object"

multi_objects=("$work"/llvm-color_kernel_multiwrite/sim/*.color-kernel-*.llvm.o)
[ -f "${multi_objects[0]}" ] || fail "multiwrite emitted no native color objects"
[ "${#multi_objects[@]}" -ge 2 ] \
  || fail "multiwrite colors were not split into separate native objects"

reg_objects=("$work"/llvm-color_kernel_llvm_reg/sim/*.color-kernel-*.llvm.o)
[ -f "${reg_objects[0]}" ] || fail "wide register block emitted no native color objects"
[ "${#reg_objects[@]}" -ge 3 ] \
  || fail "register read/update/publish colors were not emitted as separate native objects"
grep -q '__state_commit.*= true' "$work"/llvm-color_kernel_llvm_reg/sim/color_kernel_llvm_reg.llvm_reg.cpp \
  || fail "LLVM register update did not retain the phase-barrier commit"

adapters=("$work"/llvm-color_kernel_llvm_wide/sim/*.color-kernel-*.cpp)
grep -q 'Bit operations are in the linked LLVM object' "${adapters[@]}" \
  || fail "the LLVM object was not reached through the generated simulator ABI adapter"

echo "PASS: opt-in LLVM color objects preserve wide combinational and register values and match Slop assertions"
