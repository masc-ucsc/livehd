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

"$LHD" sim "$PRP" --setup-only --workdir "$work/setup" -q >/dev/null
header="$(ls "$work"/setup/sim/*cond_accum_top.hpp | head -1)"
body="$(ls "$work"/setup/sim/*cond_accum_top.cpp | head -1)"
plan="$(ls "$work"/setup/sim/*cond_accum_top*.color-plan.txt | head -1)"

grep -q 'color-direct eligible=true' "$header" || fail "conditional occurrence fell back to the retired scheduler"
grep -q 'conditional-regions=1' "$plan" || fail "conditional protocol boundary was not retained in the plan"
grep -q 'condition-__valid-or-reset' "$body" || fail "conditional call has no Taskflow condition task"
grep -Eq 'return .*__in\.en.*is_known_true\(\).*\|\|.*__in\.reset.*is_known_true\(\).*\? 0 : 1' "$body" \
  || fail "condition predicate is not the exact __valid-or-reset contract"
grep -q 'composed_of(__region_graph)' "$body" || fail "active branch does not execute the reusable region graph"
grep -q 'successors have no other strong in-edge' "$body" \
  || fail "condition successor join-safety contract is missing"
grep -q 'rt.executor().corun(\*condition_graph)' "$body" \
  || fail "outer strong-edge task does not own conditional-region completion"
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

echo "PASS: conditional occurrence colors preserve masked joins and guarded state"
