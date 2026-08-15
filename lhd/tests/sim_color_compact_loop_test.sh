#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRP="inou/prp/tests/sim/loop_roll_conditional_state_call.prp"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_compact_loop_$$}"
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

"$LHD" sim "$PRP" --setup-only --workdir "$work/setup" -q >/dev/null
header="$(ls "$work"/setup/sim/*rolled_cond_top.hpp | head -1)"
body="$(ls "$work"/setup/sim/*rolled_cond_top.cpp | head -1)"
plan="$(ls "$work"/setup/sim/*rolled_cond_top.color-plan.txt | head -1)"

grep -q 'color-direct eligible=true' "$header" || fail "compact loop fell back to the retired root scheduler"
grep -q 'compact-loops=1' "$plan" || fail "compact loop descriptor was not retained"
grep -q 'grouped-sites=1 outer-sites=1 live-sites=1' "$plan" \
  || fail "compact body leaked into the executable root occurrence DAG"
grep -Eq 'kind=loop-control .*live=true' "$plan" || fail "plan has no live opaque loop-control site"
grep -q 'std::array<Callee, count> lanes' "$header" || fail "compact state was physically expanded instead of ordinal-indexed"
grep -q 'for (std::size_t ordinal = 0; ordinal < count; ++ordinal)' "$header" \
  || fail "compact wrapper lost its native runtime ordinal loop"
grep -q '__compact_advance();  // compact-loop control: one native ordinal walk' "$body" \
  || fail "pre-rise loop version is not one outer color"
grep -q '__compact_publish();  // compact-loop post-edge version: one native ordinal walk' "$body" \
  || fail "post-edge loop version is not one outer color"
grep -q '::__color_run()' "$body" || fail "root did not emit the serial color schedule"
refute "compact-loop simulator still contains Taskflow" -Eq 'taskflow|tf::Executor|tf::Taskflow' "$header" "$body"
# A negative assertion must distinguish "no match" (status 1) from "the search
# itself failed" (anything else). `grep ... && fail` cannot: set -e exempts
# every non-final command of an AND list, so an unreadable path exits non-zero,
# short-circuits the &&, and reports GREEN. Branch on the status instead.
posedge_status=0
grep -q '::eval_posedge()' "$body" || posedge_status=$?
case "$posedge_status" in
  0) fail "root retained the module-local posedge scheduler" ;;
  1) : ;;
  *) fail "posedge scan of $body failed with status $posedge_status" ;;
esac
scheduler_status=0
grep -REl --include='*.hpp' --include='*.cpp' 'eval_posedge|eval_negedge|__settle_g|::__settle\(' "$work/setup/sim" \
  || scheduler_status=$?
case "$scheduler_status" in
  0) fail "compact kernels still expose the retired module-local scheduler" ;;
  1) : ;;
  *) fail "scheduler scan of $work/setup/sim failed with status $scheduler_status" ;;
esac

"$LHD" sim "$PRP" --workdir "$work/run" -q >/dev/null

echo "PASS: compact loop remains one opaque root control site with native ordinal execution"
