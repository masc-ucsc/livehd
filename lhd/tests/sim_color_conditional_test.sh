#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRP="inou/prp/tests/sim/cond_call_always_consumer.prp"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_conditional_$$}"
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
header="$(ls "$work"/setup/sim/*cond_accum_top.hpp | head -1)"
body="$(ls "$work"/setup/sim/*cond_accum_top.cpp | head -1)"
plan="$(ls "$work"/setup/sim/*cond_accum_top*.color-plan.txt | head -1)"

grep -q 'color-direct eligible=true' "$header" || fail "conditional occurrence fell back to the retired scheduler"
grep -q 'conditional-regions=1' "$plan" || fail "conditional protocol boundary was not retained in the plan"
grep -Eq 'if \(\(__in\.en\)\.is_known_true\(\) \|\| \(__in\.reset\)\.is_known_true\(\)\)' "$body" \
  || fail "serial schedule lost the exact __valid-or-reset guard"
refute "conditional simulator still contains Taskflow" -Eq 'taskflow|tf::Executor|tf::Taskflow' "$header" "$body"
# A negative assertion must distinguish "no match" (status 1) from "the search
# itself failed" (anything else). `<search> && fail ...` cannot: set -e exempts
# every non-final command of an AND list, so a missing tool or an unreadable
# directory exits non-zero, short-circuits the &&, and reports GREEN. Use grep
# (a declared-toolchain coreutil, unlike ripgrep) and branch on its status.
scheduler_status=0
grep -REl --include='*.hpp' --include='*.cpp' 'eval_posedge|eval_negedge|__settle_g|::__settle\(' "$work/setup/sim" \
  || scheduler_status=$?
case "$scheduler_status" in
  0) fail "ordinary hierarchy still emitted the retired module-local scheduler" ;;
  1) : ;;  # no match: the expected outcome
  *) fail "scheduler scan of $work/setup/sim failed with status $scheduler_status" ;;
esac

"$LHD" sim "$PRP" --workdir "$work/run" -q >/dev/null

echo "PASS: serial conditional colors preserve masked joins and guarded state"
