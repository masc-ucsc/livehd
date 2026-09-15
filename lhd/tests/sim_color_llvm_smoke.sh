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

# Compile the unchanged fixtures together so each backend builds one host driver.
# Their module and test names are distinct; every test block remains intact.
batch="$work/smoke.prp"
cat "${PRPS[@]}" > "$batch"
# Checkpoint/observation behavior has dedicated tests. These assertions need
# only the circuit and testbench; omit the unused state-walk code in the batches.
SMOKE_JOBS="${SIM_COLOR_SMOKE_JOBS:-4}"
run() { "$LHD" "$@" -q; }
run sim "$batch"  --set sim.checkpoint=false \
  --set sim.jobs="$SMOKE_JOBS" --workdir "$work/slop" --result-json "$work/slop.json" &
slop_pid=$!
run sim "$batch" --set sim.backend=llvm --set sim.checkpoint=false \
  --set sim.jobs="$SMOKE_JOBS" --workdir "$work/llvm" --result-json "$work/llvm.json" &
llvm_pid=$!

# Exercise the evaluator-shard build boundary with the default checkpoint-capable
# driver: rename the scalar evaluator TU and make --run-only link its LLVM body.
shard_work="$work/llvm-sharded-association"
(
  set -e
  run sim --set sim.jobs=2 inou/prp/tests/sim/color_kernel_llvm_scalar.prp --setup-only \
    --set sim.backend=llvm --workdir "$shard_work"
  mv "$shard_work/sim/color_kernel_llvm_scalar.llvm_scalar.cpp" \
     "$shard_work/sim/color_kernel_llvm_scalar.llvm_scalar.color-eval-0.cpp"
  run sim --set sim.jobs=2 inou/prp/tests/sim/color_kernel_llvm_scalar.prp --run-only \
    --set sim.backend=llvm --workdir "$shard_work"
  for prp in "${LLVM_ONLY_PRPS[@]}"; do
    run sim --set sim.jobs=2 "$prp" --set sim.backend=llvm --workdir "$work/llvm-only"
  done
) &
shard_pid=$!

# Reusing a workdir must remove the previous backend's circuit artifacts,
# including the separately generated compact definitions. Then switch back.
switch_work="$work/backend-switch"
(
  set -e
  for backend in llvm slop llvm; do
    backend_args=()
    [ "$backend" = "slop" ] || backend_args=(--set "sim.backend=$backend")
    run sim inou/prp/tests/sim/loop_hierarchy.prp --set sim.jobs=2 --set sim.checkpoint=false \
      ${backend_args[@]+"${backend_args[@]}"} --workdir "$switch_work"
    if [ "$backend" = slop ]; then
      switch_objects=("$switch_work"/sim/*.llvm.o)
      [ ! -e "${switch_objects[0]}" ] || fail "backend switch retained LLVM kernels"
    else
      switch_objects=("$switch_work"/sim/*.__loop*.color-kernel-*.llvm.o)
      [ -f "${switch_objects[0]}" ] || fail "switch to LLVM lost the compact kernel"
    fi
  done
) &
switch_pid=$!

wait "$slop_pid" || fail "Slop batch failed"
wait "$llvm_pid" || fail "LLVM batch failed"
wait "$switch_pid" || fail "backend switching did not complete"
wait "$shard_pid" || fail "sharded-association setup/run did not complete"

# Guard the batching itself: every original test must execute on both backends.
python3 - "$work" "${PRPS[@]}" <<'PY'
import json, pathlib, re, sys
work = pathlib.Path(sys.argv[1])
expected = sorted(name for path in sys.argv[2:]
                  for name in re.findall(r'^test\s+([\w.]+)', pathlib.Path(path).read_text(), re.M))
assert expected and len(expected) == len(set(expected)), expected
for backend in ('slop', 'llvm'):
    tests = json.loads((work / (backend + '.json')).read_text())['tests']
    assert sorted(t['test'] for t in tests) == expected, (backend, tests)
    assert all(t['status'] == 'pass' for t in tests), (backend, tests)
print(f'PASS: all {len(expected)} test blocks executed on both backends')
PY

objects=("$work"/llvm/sim/smoke.llvm_scalar.color-kernel-*.llvm.o)
[ -f "${objects[0]}" ] || fail "profitable scalar region did not emit LLVM bitcode"
grep -q 'llvm_inline .* | .*llvm_sim_link' "$work"/llvm/sim/build.ninja \
  || fail "LLVM native object does not depend on the version-matched link helper"

wide_objects=("$work"/llvm/sim/smoke.llvm_wide.color-kernel-*.llvm.o)
[ -f "${wide_objects[0]}" ] || fail "sim.backend=llvm did not emit the wide kernel in LLVM"

grep -q '__state_commit.*= true' "$work"/llvm/sim/smoke.llvm_reg.cpp \
  || fail "LLVM register kernel did not retain the phase-barrier commit"

adapters=("$work"/llvm*/sim/*.color-kernel-*.cpp)
[ ! -e "${adapters[0]}" ] \
  || fail "sim.backend=llvm emitted redundant per-color C++ ABI adapters"
! grep -q 'color-kernel-[^"]*\.cpp"' "$work"/llvm/sim/gen_digests.json \
  || fail "LLVM generation manifest recorded a deleted C++ kernel adapter"

# Compact bodies must use LLVM too, while retaining native ordinal loops.
loop_objects=("$work"/llvm/sim/smoke.loop_worker.__loop*.color-kernel-*.llvm.o)
[ -f "${loop_objects[0]}" ] || fail "compact loop body bypassed LLVM"
! grep -q '__compact_pre_rise' "$work"/llvm/sim/*.cpp \
  || fail "LLVM loop emitted a second Slop period body"
slop_objects=("$work"/slop/sim/*.llvm.o)
[ ! -e "${slop_objects[0]}" ] || fail "Slop backend emitted LLVM kernels"

# The recurrence and independent writes of one source loop execute together.
# Independent loops with equal domains fuse even across source locations.
for backend in slop llvm; do
  loop_header="$work/$backend/sim/smoke.loop_mixed_carry.hpp"
  grep -q 'std::array<Callee, 1> lanes' "$loop_header" \
    || fail "$backend retained per-iteration simulator objects for a stateless leaf loop"
  grep -q 'LHD_SIM_PRESERVE_LOOP for (std::size_t ordinal' "$loop_header" \
    || fail "$backend omitted the host-compiler no-unroll hint"
  grep -q 'clang loop unroll(disable)' "$loop_header" \
    || fail "$backend did not disable LLVM loop unrolling"
  grep -Eq 'compact-loops=1([[:space:]]|$)' \
    "$work/$backend/sim/smoke.loop_mixed_carry.color-plan.txt" \
    || fail "$backend split the mixed loop into multiple traversals"
  grep -Eq 'compact-loops=1([[:space:]]|$)' \
    "$work/$backend/sim/smoke.loop_fused_state.color-plan.txt" \
    || fail "$backend did not fuse independent stateful loops"
  grep -Eq 'compact-loops=1([[:space:]]|$)' \
    "$work/$backend/sim/smoke.loop_carry_kinds_sim.color-plan.txt" \
    || fail "$backend did not fuse independent equal-domain loops"
done

grep -q 'color-eval-0.o: llvm_inline' "$shard_work/sim/build.ninja" \
  || fail "LLVM kernel was not associated with its evaluator shard"

echo "PASS: selected backend emits scalar, wide, and compact-loop kernels; both match the simulation assertions"
