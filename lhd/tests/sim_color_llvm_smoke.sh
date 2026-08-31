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

# Every (fixture, backend) pair below is an INDEPENDENT `lhd sim`: its own
# fixture, its own workdir, and ~2.5s of HOST C++ compilation rather than any
# LiveHD work. Run them in bounded waves instead of end to end -- the assertions
# further down all read finished workdirs, so the order between pairs carries no
# meaning, and the sum of thirteen compiles was the whole cost of this test.
# SIM_COLOR_SMOKE_JOBS caps the fan-out; each `lhd sim` still parallelizes its
# own two or three translation units underneath.
SMOKE_JOBS="${SIM_COLOR_SMOKE_JOBS:-6}"
pids=()
labels=()

drain() {  # wait for the current wave; a failure names the pair that failed
  local i
  if [ "${#pids[@]}" -eq 0 ]; then return 0; fi
  for i in $(seq 0 $(( ${#pids[@]} - 1 ))); do
    wait "${pids[$i]}" || fail "${labels[$i]} did not simulate"
  done
  pids=()
  labels=()
}

launch() {  # launch <label> <lhd sim args...>
  local label="$1"
  shift
  "$LHD" "$@" -q &
  # Append BY INDEX: `("${pids[@]}" ...)` is an unbound-variable error on an
  # empty array under `set -u` in bash 3.2, which is the shell bazel runs.
  pids[${#pids[@]}]="$!"
  labels[${#labels[@]}]="$label"
  if [ "${#pids[@]}" -ge "$SMOKE_JOBS" ]; then drain; fi
}

# A large module puts kernel calls in separate color-eval shards. Reproduce
# that host-build boundary without checking in a 16k-node fixture: setup the
# scalar design, rename its one call-bearing TU exactly like a generated shard,
# and verify --run-only links the kernel into that TU rather than the absent
# unsplit module object. Its three steps are strictly ordered among themselves
# but independent of every pair below, so it rides alongside them in its own
# workdir and is collected at the end.
shard_work="$work/llvm-sharded-association"
(
  set -e
  "$LHD" sim inou/prp/tests/sim/color_kernel_llvm_scalar.prp --setup-only \
    --set sim.backend=llvm --workdir "$shard_work" -q
  mv "$shard_work/sim/color_kernel_llvm_scalar.llvm_scalar.cpp" \
     "$shard_work/sim/color_kernel_llvm_scalar.llvm_scalar.color-eval-0.cpp"
  "$LHD" sim inou/prp/tests/sim/color_kernel_llvm_scalar.prp --run-only \
    --set sim.backend=llvm --workdir "$shard_work" -q
) &
shard_pid=$!

# The assertions are the semantic oracle for both implementations. The second
# design keeps bits above 64 live across the generated object ABI.
for prp in "${PRPS[@]}"; do
  name="$(basename "$prp" .prp)"
  launch "slop/$name" sim "$prp" --set sim.backend=slop --workdir "$work/slop-$name"
  launch "llvm/$name" sim "$prp" --set sim.backend=llvm --workdir "$work/llvm-$name"
done
for prp in "${LLVM_ONLY_PRPS[@]}"; do
  name="$(basename "$prp" .prp)"
  launch "llvm/$name" sim "$prp" --set sim.backend=llvm --workdir "$work/llvm-$name"
done
drain

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
! grep -q 'color-kernel-[^"]*\.cpp"' "$work"/llvm-color_kernel_llvm_scalar/sim/gen_digests.json \
  || fail "LLVM generation manifest recorded a deleted C++ kernel adapter"

# The shard sub-case (started before the waves) is the last host build to
# finish; its assertion reads the tree it left behind.
wait "$shard_pid" || fail "sharded-association setup/run did not complete"
grep -q 'color-eval-0.o: llvm_inline' "$shard_work/sim/build.ninja" \
  || fail "LLVM kernel was not associated with its evaluator shard"

echo "PASS: profitable scalar LLVM regions inline, wide regions fall back to Slop, and both match Slop assertions"
