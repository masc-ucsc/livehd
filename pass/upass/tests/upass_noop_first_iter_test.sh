#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
OUT_FILE="${TEST_TMPDIR}/upass_noop_first_iter.out"

printf 'inou.prp files:%s |> pass.upass order:noop\nquit\n' "${PRP_FILE}" \
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

if ! grep -q "uPass - walk complete" "${OUT_FILE}"; then
  echo "FAIL: expected walk-complete marker not found"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: no-op pass runs and completes its walk"
