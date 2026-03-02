#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lnast_shared_decide.out"

printf 'inou.pyrope files:%s |> pass.upass ir:lnast order:decide_shared max_iters:1\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - resolved order: decide_shared" "${OUT_FILE}"; then
  echo "FAIL: expected lnast shared decide order log not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - shared decide on lnast fold_candidates:" "${OUT_FILE}"; then
  echo "FAIL: expected shared decide log for lnast not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -Eq "shared decide summary fold_candidates:[1-9][0-9]* has_fold_candidates:true" "${OUT_FILE}"; then
  echo "FAIL: expected positive lnast shared decide summary not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "uPass - converged at iteration 1" "${OUT_FILE}"; then
  echo "FAIL: expected lnast shared decide convergence message not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lnast shared decide pass runs and reports fold-candidate decision"
