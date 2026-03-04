#!/bin/sh
# Tests the fold_mult_const LGraph pass, which folds fully-constant Mult nodes:
#   c1 * c2 → (c1 * c2)   (constant-multiplication fold)

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
MULT_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_mult.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_mult_fold.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_mult_const max_iters:3\nquit\n' "${MULT_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

# 1. Dependency order: fold_scan must be inserted automatically before fold_mult_const.
if ! grep -q "uPass(lgraph) - resolved order: visit fold_scan fold_mult_const" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph mult-fold dependency order not found"
  cat "${OUT_FILE}"
  exit 1
fi

# 2. The fold_mult_const summary line must appear.
if ! grep -q "uPass(lgraph) - mult_folded:" "${OUT_FILE}"; then
  echo "FAIL: expected mult_folded summary line not found"
  cat "${OUT_FILE}"
  exit 2
fi

# 3. Metrics must be present.
if ! grep -q "rewired:" "${OUT_FILE}" || ! grep -q "new_consts:" "${OUT_FILE}" || ! grep -q "deleted:" "${OUT_FILE}"; then
  echo "FAIL: expected mult-fold metrics (rewired, new_consts, deleted) not found"
  cat "${OUT_FILE}"
  exit 3
fi

# 4. The runner must converge.
if ! grep -q "uPass(lgraph) - converged at iteration" "${OUT_FILE}"; then
  echo "FAIL: expected lgraph mult-fold convergence message not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lgraph fold_mult_const pass runs and converges"
