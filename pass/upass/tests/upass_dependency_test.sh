#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_dependency.out"

printf 'inou.pyrope files:%s |> pass.upass order:assert max_iters:2\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - resolved order: constprop assert" "${OUT_FILE}"; then
  echo "FAIL: expected resolved dependency order not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - add constprop" "${OUT_FILE}"; then
  echo "FAIL: expected constprop insertion not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass - add assert" "${OUT_FILE}"; then
  echo "FAIL: expected assert pass not found"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: dependency ordering works"
