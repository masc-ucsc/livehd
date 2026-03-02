#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_maxiters_zero.out"

printf 'inou.pyrope files:%s |> pass.upass order:constprop,assert max_iters:0\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "pass.upass max_iters=0 is invalid, forcing 1" "${OUT_FILE}"; then
  echo "FAIL: expected max_iters normalization warning not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - reached max iterations (1)" "${OUT_FILE}"; then
  echo "FAIL: expected max-iterations(1) behavior not found"
  cat "${OUT_FILE}"
  exit 2
fi

echo "PASS: max_iters=0 normalization works"
