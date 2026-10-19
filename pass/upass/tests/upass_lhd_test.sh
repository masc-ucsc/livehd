#!/bin/sh
# Runs the lhd-driven uPass smoke scripts back to back. Each script keeps all of its own assertions and gets its own TEST_TMPDIR so workdirs, logs and result.json never collide; every script always runs and one failure fails the target.
set -u
DIR="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests"
fail=0
for t in upass_dependency_test upass_noop_first_iter_test upass_cycle_cmd_test upass_func_extract_test upass_coalescer_dse_test; do
  d="${TEST_TMPDIR}/${t}"
  mkdir -p "${d}"
  echo "=== ${t}"
  if TEST_TMPDIR="${d}" sh "${DIR}/${t}.sh"; then echo "ok: ${t}"; else echo "FAIL: ${t}"; fail=1; fi
done
exit "${fail}"
