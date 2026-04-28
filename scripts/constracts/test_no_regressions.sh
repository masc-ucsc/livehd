#!/usr/bin/env bash
# test_no_regressions.sh — run the LiveHD test suite and fail only if NEW
# tests fail compared to the recorded baseline.
#
# Baseline file: .contracts/test_baseline.txt
#   One bazel target label per line, listing tests that already fail on the
#   pre-change tree. Comments (# ...) and blank lines are ignored.
#
# Usage:
#   scripts/contracts/test_no_regressions.sh                # gate (default)
#   scripts/contracts/test_no_regressions.sh --update       # rebuild baseline
#   TEST_TARGETS="//pass/upass/...:all" scripts/contracts/test_no_regressions.sh
#
# Exit codes:
#   0  no new failures vs. baseline
#   1  new failures detected
#   2  test runner could not execute

set -u

cd "$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "test_no_regressions: not in a git repo" >&2
  exit 2
}

mode="gate"
case "${1:-}" in
--update | --update-baseline) mode="update" ;;
"") ;;
*)
  echo "unknown arg: $1" >&2
  exit 2
  ;;
esac

baseline_file=".contracts/test_baseline.txt"
targets="${TEST_TARGETS:-//...}"

# Run bazel test. We deliberately allow non-zero exit and parse the output.
log=$(mktemp)
trap 'rm -f "$log"' EXIT

if ! command -v bazel >/dev/null 2>&1; then
  echo "test_no_regressions: bazel not on PATH" >&2
  exit 2
fi

# --keep_going so we get the full failure list, not just the first.
bazel test --keep_going --test_output=errors $targets >"$log" 2>&1
bazel_rc=$?

# Bazel exit codes: 0=all pass, 3=tests failed, 4=no tests, others=infra.
case $bazel_rc in
0 | 3 | 4) ;;
*)
  echo "test_no_regressions: bazel infrastructure failure (rc=$bazel_rc)" >&2
  tail -50 "$log" >&2
  exit 2
  ;;
esac

# Extract FAILED targets. Bazel prints lines like:
#   //some/pkg:target_name                                FAILED in 1.2s
current_fails=$(awk '/^\/\/.*[[:space:]]+FAILED([[:space:]]|$)/ {print $1}' "$log" |
  sort -u)

# Load baseline (strip comments/blank).
if [ -f "$baseline_file" ]; then
  baseline=$(grep -vE '^\s*(#|$)' "$baseline_file" | sort -u)
else
  baseline=""
fi

if [ "$mode" = "update" ]; then
  mkdir -p "$(dirname "$baseline_file")"
  {
    echo "# Recorded by scripts/contracts/test_no_regressions.sh --update"
    echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)  bazel rc=$bazel_rc"
    printf '%s\n' "$current_fails"
  } >"$baseline_file"
  echo "baseline written: $baseline_file ($(printf '%s\n' "$current_fails" | grep -c . || true) failing tests)"
  exit 0
fi

# Compare: any current failure not in baseline = regression.
new_fails=$(comm -23 <(printf '%s\n' "$current_fails") <(printf '%s\n' "$baseline"))
new_fails=$(printf '%s\n' "$new_fails" | grep -v '^$' || true)

if [ -n "$new_fails" ]; then
  echo "ERROR: new test regressions vs. $baseline_file:" >&2
  printf '  %s\n' $new_fails >&2
  echo "" >&2
  echo "If these failures are intentional, refresh the baseline:" >&2
  echo "  $0 --update-baseline" >&2
  exit 1
fi

# Optional info: tests that newly pass (not a failure, just a hint).
fixed=$(comm -13 <(printf '%s\n' "$current_fails") <(printf '%s\n' "$baseline") | grep -v '^$' || true)
if [ -n "$fixed" ]; then
  echo "note: $(printf '%s\n' "$fixed" | grep -c .) baseline-failing test(s) now pass; consider refreshing baseline."
fi

exit 0
