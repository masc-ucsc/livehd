#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 3d-sim-color migration rung I: a flat ordinary-posedge definition is emitted
# as a persistent color graph with explicit commit barriers and a post-fall-only
# reset publication path. No module-local edge/settle scheduler may survive.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_staged_flat_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat > "$work/staged.prp" <<'EOF'
/*
:name: staged
:type: simulation
*/
mod accum(d:u8) -> (q:u8@[1]) {
  reg state:u8 = 0
  q = state
  state = d + 1
}
test accum.run {
  mut dut = accum
  tick 5 {
    dut.d = clock
    step
  }
  assert(dut.q == 5)
}
EOF

"$LHD" sim "$work/staged.prp" --setup-only --workdir "$work/setup" -q >/dev/null
header="$(ls "$work"/setup/sim/*accum.hpp | head -1)"
body="$(ls "$work"/setup/sim/*accum.cpp | head -1)"
runtime="$(ls "$work"/setup/sim/*accum.color-runtime.hpp | head -1)"

grep -q 'void __color_prepare_taskflow()' "$header" || fail "missing persistent Taskflow preparation method"
grep -q 'void __color_refresh_inputs()' "$header" || fail "missing versioned input refresh method"
grep -q 'void __color_reset_settle()' "$header" || fail "missing post-fall-only reset publication method"
grep -q '#include <taskflow/taskflow.hpp>' "$runtime" || fail "selected root runtime does not compile its Taskflow API"
grep -q 'std::shared_ptr<__Color_runtime> __color_runtime' "$header" || fail "root schedule is not persistent/reusable"
grep -q 'tf::Executor' "$runtime" \
  && fail "the serial backend created an unused resident Taskflow worker"
grep -q 'void __color_run_taskflow()' "$header" || fail "root does not declare its Taskflow runner"
grep -q 'void __color_eval(std::size_t color)' "$header" || fail "root lacks the body-private indexed color dispatcher"
grep -q 'void __color_quiesce() { __color_runtime.reset(); }' "$header" \
  || fail "root cannot join Taskflow workers before a fork-based checkpoint"
grep -q 'void eval_posedge()' "$header" && fail "flat staged definition retained the monolithic posedge method"
grep -q 'void __settle' "$header" && fail "color root retained the module-local settle API"
grep -q '::__settle()' "$body" && fail "color root retained module-local settle generation"
grep -q '::__color_run_taskflow()' "$body" || fail "root Taskflow schedule body was not emitted"
grep -q 'runtime->graph.emplace' "$body" || fail "root schedule does not build reusable Taskflow tasks"
grep -q '::__color_eval(std::size_t' "$body" || fail "planned colors were not lowered to an executable dispatcher"
grep -Fq 'slots[slot].precede(colors[color])' "$body" || fail "Taskflow lost per-color slot precedence"
grep -Fq 'colors[color].precede(slots[slot + 1])' "$body" || fail "Taskflow lost the next-phase barrier"
grep -q 'rise-commit-barrier' "$body" || fail "Taskflow has no staged rise commit action"
grep -q 'struct .*::__Color_runtime' "$runtime" || fail "color runtime details are not isolated from the module header"
grep -q '__color_slot_' "$body" || fail "cross-color values do not have producer-owned storage"
grep -q '__color_run_serial();  // auto/one worker avoids per-period Taskflow dispatch overhead' "$header" \
  || fail "default cycle does not use the generated serial topological backend"

# The opt-in parallel backend retains generation-based dirty activation. It is
# intentionally absent from the faster generated serial schedule.
"$LHD" sim "$work/staged.prp" --setup-only --set sim.workers=2 \
  --workdir "$work/setup-workers" -q >/dev/null
parallel_body="$(ls "$work"/setup-workers/sim/*accum.cpp | head -1)"
parallel_runtime="$(ls "$work"/setup-workers/sim/*accum.color-runtime.hpp | head -1)"
grep -q '__Color_runtime() : executor(2)' "$parallel_runtime" || fail "sim.workers=2 did not size the Taskflow executor"
grep -q '__color_runtime->executor.run(__color_runtime->graph).wait()' "$parallel_body" \
  || fail "the explicit multi-worker backend cannot execute the persistent Taskflow graph"
grep -q 'if (!__dirty && !__always_run\[__color_index\]) return;' "$parallel_body" \
  || fail "parallel colors do not retain per-boundary dirty generations"
grep -q '__color_seen\[' "$parallel_body" || fail "parallel color input generations are not retained across periods"
grep -q 'slop_update(__rt.__color_slot_' "$parallel_body" \
  || fail "parallel producer slots do not advance generations on change"

commit_body="$(sed -n '/::__color_commit_taskflow(std::size_t/,/^}/p' "$body")"
grep -q 'slop_update(state, state_din)' <<<"$commit_body" || fail "rise commit does not consume the parked pending slot"
grep -q 'sum_op\|__out\.\|__o\.' <<<"$commit_body" && fail "rise commit re-evaluates combinational or output logic"

taskflow="$work/setup/sim/runtime/taskflow/taskflow/taskflow.hpp"
[ -f "$taskflow" ] || fail "the exact OpenTimer Taskflow payload was not staged into the generated tree"
grep -q 'runtime/taskflow' "$work/setup/sim/BUILD" || fail "standalone Bazel scaffold cannot include staged Taskflow"
grep -q 'linkopts = \["-pthread"\]' "$work/setup/sim/BUILD" || fail "standalone Bazel scaffold lacks pthread linkage"

# Host-compile and execute the exact generated source, not only its shape.
"$LHD" sim "$work/staged.prp" --set sim.checkpoint_every=1 \
  --workdir "$work/run" -q >/dev/null
grep -q -- '-pthread' "$work/run/sim/build.ninja" || fail "Ninja host build lacks pthread compile/link flags"
grep -q 'runtime/taskflow' "$work/run/sim/build.ninja" || fail "Ninja host build does not use the staged Taskflow copy"
grep -q '\.__color_quiesce();  // destroy/join Taskflow workers before fork' "$work/run/sim/drv.cpp" \
  || fail "checkpoint driver can fork with resident Taskflow workers"
grep -Eq 'runfiles.*opentimer|opentimer.*runfiles' "$work/run/sim/build.ninja" \
  && fail "Ninja host build baked a disposable Taskflow runfiles path"

# ...and that -I must be ABSOLUTE. `--workdir` is routinely RELATIVE (the runs
# above happen to pass an absolute TEST_TMPDIR path, which hid this), while the
# compile runs with its cwd set to the sim dir — so a relative
# `-I<workdir>/sim/runtime/taskflow` resolves against the wrong directory and the
# staged payload is invisible ("'taskflow/taskflow.hpp' file not found" with the
# headers sitting right there). Drive it the way a user does: relative workdir,
# from a cwd that is not the repo root.
lhd_abs="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
(cd "$work" && "$lhd_abs" sim staged.prp --workdir relwork -q >/dev/null) \
  || fail "a relative --workdir run failed to build/run the generated simulator"
tf_inc="$(grep -o -- "-I'\?[^' ]*runtime/taskflow" "$work/relwork/sim/build.ninja" | head -1)"
[ -n "$tf_inc" ] || fail "relative-workdir Ninja build does not use the staged Taskflow copy"
case "$tf_inc" in
  -I\'/* | -I/*) ;;
  *) fail "staged Taskflow -I is relative ($tf_inc); the compile runs from the sim dir" ;;
esac

echo "PASS: flat posedge simulation uses only the persistent state-version color graph"
