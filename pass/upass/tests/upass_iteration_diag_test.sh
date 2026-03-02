#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_iteration_diag.out"

printf 'inou.pyrope files:%s |> pass.upass order:constprop,assert max_iters:5\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - resolved order: constprop assert" "${OUT_FILE}"; then
  echo "FAIL: resolved order diagnostic not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - iteration 1 changed: constprop" "${OUT_FILE}"; then
  echo "FAIL: iteration-change diagnostic not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass - converged at iteration 2" "${OUT_FILE}"; then
  echo "FAIL: convergence diagnostic not found"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: iteration diagnostics are present"
