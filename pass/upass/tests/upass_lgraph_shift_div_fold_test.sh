#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_shift_div.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_shift_div_fold.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_shift_div max_iters:3\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_shift_div" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph shift/div dependency order not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - shiftdiv_simplified:" "${OUT_FILE}"; then
  echo "FAIL: expected shift/div simplification summary not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "to_const1:" "${OUT_FILE}"; then
  echo "FAIL: expected shift/div to_const1 metric not found"
  cat "${OUT_FILE}"
  exit 21
fi

if ! grep -q "rewired:" "${OUT_FILE}" || ! grep -q "new_consts:" "${OUT_FILE}" || ! grep -q "deleted:" "${OUT_FILE}"; then
  echo "FAIL: expected shift/div metrics not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "uPass(lgraph) - converged at iteration" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph shift/div convergence message not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lgraph shift/div fold pass runs and converges"
