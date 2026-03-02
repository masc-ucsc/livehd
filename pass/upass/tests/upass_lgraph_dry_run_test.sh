#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_shift_div.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_dry_run.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_shift_div dry_run:true max_iters:1 |> pass.upass ir:lgraph order:fold_shift_div dry_run:false max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - shiftdiv_simplified:" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph shift/div summary not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "dry_run:true" "${OUT_FILE}"; then
  echo "FAIL: expected dry_run:true summary marker not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "dry_run:false" "${OUT_FILE}"; then
  echo "FAIL: expected dry_run:false summary marker not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -Eq "shiftdiv_simplified:[1-9][0-9]*.*dry_run:false" "${OUT_FILE}"; then
  echo "FAIL: expected post-dry-run mutating pass to still find simplifications"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lgraph dry_run reports metrics without mutating graph"
