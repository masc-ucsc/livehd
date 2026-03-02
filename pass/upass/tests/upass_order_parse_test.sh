#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_order_parse.out"

set +e
printf 'inou.pyrope files:%s |> pass.upass order:,,constprop,,assert,, max_iters:2\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1
STATUS=$?
set -e

if [ "${STATUS}" -eq 0 ]; then
  echo "FAIL: malformed order input unexpectedly succeeded"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "field order with invalid value in pass.upass command" "${OUT_FILE}"; then
  echo "FAIL: expected malformed-order validation error not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "command aborted" "${OUT_FILE}"; then
  echo "FAIL: expected command-aborted path not found for malformed order"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: malformed order input is rejected by command validation"
