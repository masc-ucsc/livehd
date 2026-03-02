#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_shift_div.prp"
OUT_FILE="${TEST_TMPDIR}/upass_cross_ir_inherit_false.out"

printf 'inou.pyrope files:%s |> pass.upass ir:lnast order:noop dry_run:true max_iters:1 |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_shift_div max_iters:1 |> pass.upass ir:lgraph inherit:false order:fold_shift_div max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

SHIFTDIV_COUNT="$(grep -c "uPass(lgraph) - shiftdiv_simplified:" "${OUT_FILE}")"
if [ "${SHIFTDIV_COUNT}" -ne 2 ]; then
  echo "FAIL: expected shift/div summary exactly twice in cross-IR pipeline"
  cat "${OUT_FILE}"
  exit 1
fi

DRY_TRUE_COUNT="$(grep -c "shiftdiv_simplified:.*dry_run:true" "${OUT_FILE}")"
if [ "${DRY_TRUE_COUNT}" -ne 1 ]; then
  echo "FAIL: expected exactly one dry_run:true shift/div stage (carry-over from lnast stage)"
  cat "${OUT_FILE}"
  exit 2
fi

DRY_FALSE_COUNT="$(grep -c "shiftdiv_simplified:.*dry_run:false" "${OUT_FILE}")"
if [ "${DRY_FALSE_COUNT}" -ne 1 ]; then
  echo "FAIL: expected exactly one dry_run:false shift/div stage after inherit:false reset"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_shift_div" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph fold_shift_div resolved order not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: cross-IR inherit:false resets carried labels for lgraph stage"
