#!/bin/sh
# Tests the fold_sub_const LGraph pass, which handles three subtraction patterns:
#   a - 0  → a        (neutral-element fold)
#   a - a  → 0        (self-cancellation fold)
#   c1 - c2 → result  (fully-constant subtraction fold)

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
SUB_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_sub.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_sub_fold.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_sub_const max_iters:3\nquit\n' "${SUB_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

# 1. Dependency order: fold_scan must be inserted automatically before fold_sub_const.
if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_sub_const" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph sub-fold dependency order not found"
  cat "${OUT_FILE}"
  exit 1
fi

# 2. The fold_sub_const summary line must appear.
if ! grep -q "uPass(lgraph) - sub_folded:" "${OUT_FILE}"; then
  echo "FAIL: expected sub_folded summary line not found"
  cat "${OUT_FILE}"
  exit 2
fi

# 3. Metrics must be present.
if ! grep -q "sub_zero:" "${OUT_FILE}" || ! grep -q "sub_self:" "${OUT_FILE}" || ! grep -q "const_sub:" "${OUT_FILE}"; then
  echo "FAIL: expected sub-fold metrics (sub_zero, sub_self, const_sub) not found"
  cat "${OUT_FILE}"
  exit 3
fi

# 4. The runner must converge.
if ! grep -q "uPass(lgraph) - converged at iteration" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph sub-fold convergence message not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lgraph fold_sub_const pass runs and converges"
