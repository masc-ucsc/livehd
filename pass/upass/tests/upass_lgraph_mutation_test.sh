#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_sum_const.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_mutation.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_tag max_iters:3\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_tag" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph dependency-resolved order not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - tag fold_candidates:" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph fold_tag mutation summary not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass(lgraph) - iteration 1 changed: fold_tag" "${OUT_FILE}"; then
  echo "FAIL: expected fold_tag change report not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "uPass(lgraph) - converged at iteration 2" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph convergence at iteration 2 not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lgraph guarded mutation pass works and converges"
