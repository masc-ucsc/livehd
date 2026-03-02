#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_unknown.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:__upass_lgraph_does_not_exist max_iters:2\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "__upass_lgraph_does_not_exist is not defined." "${OUT_FILE}"; then
  echo "FAIL: expected lgraph unknown pass message not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "ERROR: pass.upass invalid lgraph pass configuration: unknown pass '__upass_lgraph_does_not_exist'" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph pass.upass unknown-pass error not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "command aborted" "${OUT_FILE}"; then
  echo "FAIL: expected command-aborted output not found for lgraph unknown pass"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: lgraph unknown-pass behavior fails cleanly"
