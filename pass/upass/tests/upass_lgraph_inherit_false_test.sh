#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_shift_div.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_inherit_false.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_shift_div dry_run:true max_iters:1 |> pass.upass ir:lgraph inherit:false max_iters:1 |> pass.upass ir:lgraph inherit:false order:fold_shift_div max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

SHIFTDIV_COUNT="$(grep -c "uPass(lgraph) - shiftdiv_simplified:" "${OUT_FILE}")"
if [ "${SHIFTDIV_COUNT}" -ne 2 ]; then
  echo "FAIL: expected shift/div summary exactly twice (stage1 inherited + stage3 explicit reapply)"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan$" "${OUT_FILE}"; then
  echo "FAIL: expected inherit:false stage without explicit order to use default order"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "dry_run:false" "${OUT_FILE}"; then
  echo "FAIL: expected inherit:false explicit-reapply stage to reset dry_run to false"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "dry_run:true" "${OUT_FILE}"; then
  echo "FAIL: expected initial inherited dry_run:true stage to still be visible"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: inherit:false resets inherited labels and reapplies only explicit stage labels"
