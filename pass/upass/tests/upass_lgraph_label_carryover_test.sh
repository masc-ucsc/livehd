#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_shift_div.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_label_carryover.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_shift_div dry_run:true max_iters:1 |> pass.upass ir:lgraph order:fold_shift_div max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - shiftdiv_simplified:" "${OUT_FILE}"; then
  echo "FAIL: expected shift/div summary not found"
  cat "${OUT_FILE}"
  exit 1
fi

DRY_TRUE_COUNT="$(grep -c "dry_run:true" "${OUT_FILE}")"
if [ "${DRY_TRUE_COUNT}" -lt 2 ]; then
  echo "FAIL: expected dry_run:true to carry over to second pass.upass stage"
  cat "${OUT_FILE}"
  exit 2
fi

if grep -q "dry_run:false" "${OUT_FILE}"; then
  echo "FAIL: unexpected dry_run:false; carry-over semantics changed"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: label carry-over keeps dry_run:true across pipeline stages unless overridden"
