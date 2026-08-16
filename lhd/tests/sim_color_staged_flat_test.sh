#!/usr/bin/env bash
# A flat ordinary-posedge definition uses the serial occurrence-wide color
# schedule with explicit commit barriers and no Taskflow dependency.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_staged_flat_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Negative assertion. A bare `grep ... && fail` cannot distinguish "no match"
# (status 1, the assertion HOLDS) from "the search itself failed" (status >1 --
# a missing or unreadable path), and `set -e` exempts every non-final command of
# an AND list, so the broken case reports GREEN. Branch on the status instead.
refute() {
  local msg="$1"
  shift
  local st=0
  grep "$@" >/dev/null 2>&1 || st=$?
  case "$st" in
    0) fail "$msg" ;;
    1) : ;;
    *) fail "negative search failed with status $st: grep $*" ;;
  esac
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

grep -q 'void __color_prepare_runtime()' "$header" || fail "missing persistent color-runtime preparation"
grep -q 'void __color_refresh_inputs()' "$header" || fail "missing versioned input refresh"
grep -q 'void __color_reset_settle()' "$header" || fail "missing reset publication path"
grep -q 'void __color_run()' "$header" || fail "missing serial color runner"
grep -q 'void __color_eval(std::size_t color)' "$header" || fail "missing indexed color dispatcher"
grep -q 'std::shared_ptr<__Color_runtime> __color_runtime' "$header" || fail "color runtime is not persistent"
refute "flat color root retained monolithic posedge evaluation" -q 'void eval_posedge()' "$header"
refute "color root retained module-local settle API" -q 'void __settle' "$header"
refute "color root retained module-local settle generation" -q '::__settle()' "$body"
grep -q '::__color_run()' "$body" || fail "serial color schedule body was not emitted"
grep -q '::__color_eval(std::size_t' "$body" || fail "planned colors were not lowered"
grep -q '__color_slot_' "$body" || fail "cross-color values lack producer-owned storage"
grep -q 'struct .*::__Color_runtime' "$runtime" || fail "runtime details are not isolated from the module header"
refute "generated simulator still contains Taskflow" -Eq 'taskflow|tf::Executor|tf::Taskflow' "$header" "$body" "$runtime"

commit_body="$(sed -n '/::__color_commit(std::size_t/,/^}/p' "$body")"
# A scalar (non-pipelined) commit lands the pending value with a plain
# assignment: the dynamic `__state_commit` flag is only raised after proving
# D != Q, so slop_update's compare-on-write would repeat that test. A pipe
# stage still needs slop_update (it carries its own per-stage change bit).
grep -Eq 'slop_update\(state, state_din\)|state = state_din;' <<<"$commit_body" \
  || fail "rise commit does not consume pending state"
grep -q 'sum_op\|__out\.\|__o\.' <<<"$commit_body" && fail "rise commit re-evaluates combinational logic"

[ ! -e "$work/setup/sim/runtime/taskflow" ] || fail "generated tree still stages Taskflow"
refute "standalone BUILD still references Taskflow" -q 'runtime/taskflow' "$work/setup/sim/BUILD"

# Host-compile and execute the exact generated source, including checkpointing.
"$LHD" sim "$work/staged.prp" --set sim.checkpoint_every=1 --workdir "$work/run" -q >/dev/null
refute "host build still references Taskflow" -q 'runtime/taskflow' "$work/run/sim/build.ninja"
refute "checkpoint driver retained worker quiescing" -q '__color_quiesce' "$work/run/sim/drv.cpp"

# A relative workdir must remain valid now that no staged include path is needed.
lhd_abs="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
(cd "$work" && "$lhd_abs" sim staged.prp --workdir relwork -q >/dev/null) \
  || fail "relative --workdir simulation failed"

echo "PASS: flat posedge simulation uses the serial occurrence-wide color schedule"
