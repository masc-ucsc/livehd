#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"

if [ ! -x "${LGSHELL}" ]; then
  echo "FAIL: lgshell binary not found at ${LGSHELL}"
  exit 1
fi

if [ ! -f "${PRP_FILE}" ]; then
  echo "FAIL: test input not found at ${PRP_FILE}"
  exit 1
fi

OUT_FILE="${TEST_TMPDIR}/upass_converge.out"

printf 'inou.pyrope files:%s |> pass.upass order:constprop,assert max_iters:5\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - converged at iteration 2" "${OUT_FILE}"; then
  echo "FAIL: expected convergence message not found"
  cat "${OUT_FILE}"
  exit 2
fi

if grep -q "uPass - reached max iterations" "${OUT_FILE}"; then
  echo "FAIL: unexpected max-iterations message found"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: upass converged before max_iters"
