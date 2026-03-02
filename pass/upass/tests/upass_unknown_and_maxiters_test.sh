#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"

OUT_UNKNOWN="${TEST_TMPDIR}/upass_unknown.out"
OUT_MAXIT="${TEST_TMPDIR}/upass_maxiters.out"

printf 'inou.pyrope files:%s |> pass.upass order:does_not_exist max_iters:2\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_UNKNOWN}" 2>&1

if ! grep -q "does_not_exist is not defined." "${OUT_UNKNOWN}"; then
  echo "FAIL: expected unknown pass message not found"
  cat "${OUT_UNKNOWN}"
  exit 1
fi

if ! grep -q "ERROR: pass.upass invalid pass configuration: unknown pass 'does_not_exist'" "${OUT_UNKNOWN}"; then
  echo "FAIL: expected pass.upass unknown-pass error not found"
  cat "${OUT_UNKNOWN}"
  exit 2
fi

if ! grep -q "command aborted" "${OUT_UNKNOWN}"; then
  echo "FAIL: expected command-aborted output not found for unknown pass"
  cat "${OUT_UNKNOWN}"
  exit 21
fi

printf 'inou.pyrope files:%s |> pass.upass order:constprop,assert max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_MAXIT}" 2>&1

if ! grep -q "uPass - reached max iterations (1)" "${OUT_MAXIT}"; then
  echo "FAIL: expected max-iterations message not found"
  cat "${OUT_MAXIT}"
  exit 3
fi

echo "PASS: unknown-pass and max-iterations behavior works"
