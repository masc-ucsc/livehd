#!/bin/sh
# pass.upass dependency-cycle detection via the lhd kernel. The uPass cycle/
# chain diagnostics print to stderr; the final pass.upass configuration error
# lands in the per-step log AND the result JSON error block, and the run must
# exit non-zero (the lgshell "command aborted" equivalent).

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
W="${TEST_TMPDIR}/w"
ERR_FILE="${TEST_TMPDIR}/upass_cycle_cmd.err"
RESULT="${TEST_TMPDIR}/result.json"

set +e
"${LHD}" compile "${PRP_FILE}" --set upass.order=__upass_cycle_cmd_a --workdir "${W}" -q \
  --result-json "${RESULT}" >/dev/null 2>"${ERR_FILE}"
STATUS=$?
set -e

if [ "${STATUS}" -eq 0 ]; then
  echo "FAIL: dependency cycle unexpectedly succeeded"
  cat "${ERR_FILE}"
  exit 1
fi

if ! grep -q "uPass dependency cycle detected at __upass_cycle_cmd_a" "${ERR_FILE}"; then
  echo "FAIL: expected dependency-cycle diagnostic not found"
  cat "${ERR_FILE}"
  exit 2
fi

if ! grep -q "uPass dependency chain for __upass_cycle_cmd_b is invalid" "${ERR_FILE}"; then
  echo "FAIL: expected invalid-chain diagnostic not found"
  cat "${ERR_FILE}"
  exit 3
fi

if ! grep -q "pass.upass invalid pass configuration: dependency cycle detected at '__upass_cycle_cmd_a'" \
    "${W}"/logs/*pass_upass*.log "${RESULT}"; then
  echo "FAIL: expected pass.upass cycle error not found in step log or result JSON"
  cat "${RESULT}"
  exit 4
fi

echo "PASS: command-path dependency cycle is detected and cleanly aborted"
