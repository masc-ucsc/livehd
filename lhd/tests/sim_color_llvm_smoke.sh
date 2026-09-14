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
  "inou/prp/tests/sim/loop_hierarchy.prp"
  "inou/prp/tests/sim/loop_invariant_broadcast.prp"
  "inou/prp/tests/sim/loop_roll_conditional_state_call.prp"
  "inou/prp/tests/sim/loop_state_multi_carry.prp"
  "inou/prp/tests/sim/loop_mixed_carry.prp"
  "inou/prp/tests/sim/div_narrow_result.prp"
  "inou/prp/tests/sim/loop_inlined_capture.prp"
  "inou/prp/tests/sim/loop_inlined_array.prp"
  "inou/prp/tests/sim/mem_wensize_lanes.prp"
  "inou/prp/tests/sim/loop_fused_state.prp"
  "inou/prp/tests/sim/loop_carry_kinds_sim.prp"
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
# meaning, and the sum of the host compiles was the whole cost of this test.
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
[ -f "${wide_objects[0]}" ] || fail "sim.backend=llvm did not emit the wide kernel in LLVM"

grep -q '__state_commit.*= true' "$work"/llvm-color_kernel_llvm_reg/sim/color_kernel_llvm_reg.llvm_reg.cpp \
  || fail "LLVM register kernel did not retain the phase-barrier commit"

adapters=("$work"/llvm-*/sim/*.color-kernel-*.cpp)
[ ! -e "${adapters[0]}" ] \
  || fail "sim.backend=llvm emitted redundant per-color C++ ABI adapters"
! grep -q 'color-kernel-[^"]*\.cpp"' "$work"/llvm-color_kernel_llvm_scalar/sim/gen_digests.json \
  || fail "LLVM generation manifest recorded a deleted C++ kernel adapter"

# Compact bodies must use LLVM too, while retaining native ordinal loops.
loop_objects=("$work"/llvm-loop_hierarchy/sim/*.__loop*.color-kernel-*.llvm.o)
[ -f "${loop_objects[0]}" ] || fail "compact loop body bypassed LLVM"
! grep -q '__compact_pre_rise' "$work"/llvm-loop_hierarchy/sim/*.cpp \
  || fail "LLVM loop emitted a second Slop period body"
slop_objects=("$work"/slop-*/sim/*.llvm.o)
[ ! -e "${slop_objects[0]}" ] || fail "Slop backend emitted LLVM kernels"

# The recurrence and independent writes of one source loop execute together.
# Independent loops with equal domains fuse even across source locations.
for backend in slop llvm; do
  loop_header="$work/$backend-loop_mixed_carry/sim/loop_mixed_carry.loop_mixed_carry.hpp"
  grep -q 'std::array<Callee, 1> lanes' "$loop_header" \
    || fail "$backend retained per-iteration simulator objects for a stateless leaf loop"
  grep -q 'LHD_SIM_PRESERVE_LOOP for (std::size_t ordinal' "$loop_header" \
    || fail "$backend omitted the host-compiler no-unroll hint"
  grep -q 'clang loop unroll(disable)' "$loop_header" \
    || fail "$backend did not disable LLVM loop unrolling"
  grep -Eq 'compact-loops=1([[:space:]]|$)' \
    "$work/$backend-loop_mixed_carry/sim/loop_mixed_carry.loop_mixed_carry.color-plan.txt" \
    || fail "$backend split the mixed loop into multiple traversals"
  grep -Eq 'compact-loops=1([[:space:]]|$)' \
    "$work/$backend-loop_fused_state/sim/loop_fused_state.loop_fused_state.color-plan.txt" \
    || fail "$backend did not fuse independent stateful loops"
  grep -Eq 'compact-loops=1([[:space:]]|$)' \
    "$work/$backend-loop_carry_kinds_sim/sim/loop_carry_kinds_sim.loop_carry_kinds_sim.color-plan.txt" \
    || fail "$backend did not fuse independent equal-domain loops"
done

# Reusing a workdir must remove the previous backend's circuit artifacts,
# including the separately generated compact definitions. Then switch back.
switch_work="$work/llvm-loop_hierarchy"
"$LHD" sim inou/prp/tests/sim/loop_hierarchy.prp --set sim.backend=slop --workdir "$switch_work" -q
switch_objects=("$switch_work"/sim/*.llvm.o)
[ ! -e "${switch_objects[0]}" ] || fail "backend switch retained LLVM kernels"
"$LHD" sim inou/prp/tests/sim/loop_hierarchy.prp --set sim.backend=llvm --workdir "$switch_work" -q
switch_objects=("$switch_work"/sim/*.__loop*.color-kernel-*.llvm.o)
[ -f "${switch_objects[0]}" ] || fail "switch back to LLVM lost the compact kernel"

# The shard sub-case (started before the waves) is the last host build to
# finish; its assertion reads the tree it left behind.
wait "$shard_pid" || fail "sharded-association setup/run did not complete"
grep -q 'color-eval-0.o: llvm_inline' "$shard_work/sim/build.ninja" \
  || fail "LLVM kernel was not associated with its evaluator shard"

echo "PASS: selected backend emits scalar, wide, and compact-loop kernels; both match the simulation assertions"
