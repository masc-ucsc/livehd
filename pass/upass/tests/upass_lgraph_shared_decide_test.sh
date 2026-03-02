#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_shift_div.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_shared_decide.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:decide_shared max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - resolved order: decide_shared" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph shared decide order log not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - shared decide on lgraph fold_candidates:" "${OUT_FILE}"; then
  echo "FAIL: expected shared decide log for lgraph not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -Eq "shared decide summary fold_candidates:[1-9][0-9]* has_fold_candidates:true" "${OUT_FILE}"; then
  echo "FAIL: expected positive lgraph shared decide summary not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "uPass(lgraph) - converged at iteration 1" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph shared decide convergence message not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lgraph shared decide pass runs and reports fold-candidate decision"
