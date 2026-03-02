#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_missing_input.out"

printf 'inou.pyrope files:%s |> pass.upass ir:lgraph\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "pass.upass ir:lgraph requires lgraph input" "${OUT_FILE}"; then
  echo "FAIL: expected missing-lgraph-input diagnostic not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "ERROR: pass.upass ir:lgraph requires lgraph input" "${OUT_FILE}"; then
  echo "FAIL: expected runtime error diagnostic not found for ir:lgraph missing input"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "command aborted" "${OUT_FILE}"; then
  echo "FAIL: expected command-aborted output not found for ir:lgraph missing input"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: ir:lgraph rejects missing lgraph input"
