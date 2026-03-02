#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_mode.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - visit " "${OUT_FILE}"; then
  echo "FAIL: expected lgraph node visit logs not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - visited " "${OUT_FILE}"; then
  echo "FAIL: expected lgraph visit summary not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass(lgraph) - scan visited:" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph fold-candidate scan summary not found"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: lgraph mode traverses graph nodes"
