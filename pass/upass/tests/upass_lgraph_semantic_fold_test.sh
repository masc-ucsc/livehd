#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_sum_const.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_semantic_fold.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_sum_const max_iters:3\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_sum_const" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph semantic-fold dependency order not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - sum_const_folded:" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph semantic fold summary not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "rewired:" "${OUT_FILE}" || ! grep -q "new_consts:" "${OUT_FILE}" || ! grep -q "deleted:" "${OUT_FILE}"; then
  echo "FAIL: expected semantic fold metrics not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "uPass(lgraph) - converged at iteration" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph semantic fold convergence message not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lgraph semantic fold pass runs and converges"
