#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_neutral.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_neutral_fold.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_neutral max_iters:3\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_neutral" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph neutral-fold dependency order not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - neutral_simplified:" "${OUT_FILE}"; then
  echo "FAIL: expected neutral simplification summary not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -Eq "uPass\\(lgraph\\) - neutral_simplified:[1-9]" "${OUT_FILE}"; then
  echo "FAIL: expected at least one neutral simplification rewrite"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "to_const1:" "${OUT_FILE}"; then
  echo "FAIL: expected neutral fold to_const1 metric not found"
  cat "${OUT_FILE}"
  exit 31
fi

if ! grep -q "rewired:" "${OUT_FILE}" || ! grep -q "new_consts:" "${OUT_FILE}" || ! grep -q "deleted:" "${OUT_FILE}"; then
  echo "FAIL: expected neutral fold metrics not found"
  cat "${OUT_FILE}"
  exit 4
fi

if ! grep -q "uPass(lgraph) - converged at iteration" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph neutral fold convergence message not found"
  cat "${OUT_FILE}"
  exit 5
fi

echo "PASS: lgraph neutral fold pass runs and converges"
