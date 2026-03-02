#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_invalid_ir.out"

printf 'inou.pyrope files:%s |> pass.upass ir:foo\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "pass.upass invalid ir:foo (expected lnast or lgraph)" "${OUT_FILE}"; then
  echo "FAIL: expected invalid-ir diagnostic not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "ERROR: pass.upass invalid ir:foo (expected lnast or lgraph)" "${OUT_FILE}"; then
  echo "FAIL: expected runtime invalid-ir error output not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "command aborted" "${OUT_FILE}"; then
  echo "FAIL: expected command-aborted output not found for invalid ir"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: invalid ir fails cleanly without process abort trap"
