#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_noop_first_iter.out"

printf 'inou.pyrope files:%s |> pass.upass order:noop max_iters:5\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - resolved order: noop" "${OUT_FILE}"; then
  echo "FAIL: expected noop resolved-order diagnostic not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - add noop" "${OUT_FILE}"; then
  echo "FAIL: expected noop pass registration not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass - converged at iteration 1" "${OUT_FILE}"; then
  echo "FAIL: expected first-iteration convergence not found"
  cat "${OUT_FILE}"
  exit 3
fi

if grep -q "uPass - reached max iterations" "${OUT_FILE}"; then
  echo "FAIL: unexpected max-iteration message found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: no-op pass converges on first iteration"
