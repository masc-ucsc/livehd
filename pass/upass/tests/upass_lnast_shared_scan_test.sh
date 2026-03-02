#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lnast_shared_scan.out"

printf 'inou.pyrope files:%s |> pass.upass ir:lnast order:scan_shared max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - resolved order: scan_shared" "${OUT_FILE}"; then
  echo "FAIL: expected lnast shared scan order log not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - shared scan on lnast nodes:" "${OUT_FILE}"; then
  echo "FAIL: expected shared scan log for lnast not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass - shared scan summary nodes:" "${OUT_FILE}"; then
  echo "FAIL: expected shared scan summary log for lnast not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -Eq "shared scan summary nodes:[1-9][0-9]* const:[0-9]+ arith:[0-9]+" "${OUT_FILE}"; then
  echo "FAIL: expected non-empty shared scan summary metrics not found"
  cat "${OUT_FILE}"
  exit 4
fi

if ! grep -q "uPass - converged at iteration 1" "${OUT_FILE}"; then
  echo "FAIL: expected lnast shared scan convergence message not found"
  cat "${OUT_FILE}"
  exit 5
fi

echo "PASS: lnast shared scan pass runs and reports node count"
