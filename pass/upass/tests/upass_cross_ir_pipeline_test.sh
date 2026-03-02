#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_shift_div.prp"
OUT_FILE="${TEST_TMPDIR}/upass_cross_ir_pipeline.out"

printf 'inou.pyrope files:%s |> pass.upass ir:lnast order:noop max_iters:1 |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_shift_div max_iters:3\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - resolved order: noop" "${OUT_FILE}"; then
  echo "FAIL: expected LNAST upass order log not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_shift_div" "${OUT_FILE}"; then
  echo "FAIL: expected LGraph upass order log not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -Eq "shiftdiv_simplified:[1-9][0-9]*.*to_const_other:[1-9][0-9]*" "${OUT_FILE}"; then
  echo "FAIL: expected shift/div fold summary with non-trivial const fold not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "uPass(lgraph) - converged at iteration" "${OUT_FILE}"; then
  echo "FAIL: expected LGraph convergence message not found"
  cat "${OUT_FILE}"
  exit 4
fi

if grep -q "runtime-error" "${OUT_FILE}"; then
  echo "FAIL: unexpected runtime error in cross-IR pipeline"
  cat "${OUT_FILE}"
  exit 5
fi

echo "PASS: cross-IR upass pipeline runs with expected fold diagnostics"
