#!/bin/sh
# Tests the dce (dead-code elimination) LGraph pass.
# Strategy: run fold_mult_const first so the original Const inputs to each Mult
# node become unreachable, then let dce remove them.

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
DCE_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/upass/tests/lgraph_dce.prp"
OUT_FILE="${TEST_TMPDIR}/upass_lgraph_dce.out"

printf 'inou.pyrope files:%s |> pass.lnast_tolg |> pass.upass ir:lgraph order:fold_mult_const,dce max_iters:5\nquit\n' "${DCE_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

# 1. Resolved order must list both passes.
if ! grep -q "uPass(lgraph) - resolved order:.*fold_mult_const.*dce" "${OUT_FILE}"; then
  echo "FAIL: expected resolved order containing fold_mult_const and dce"
  cat "${OUT_FILE}"
  exit 1
fi

# 2. The mult_folded summary must appear (at least one Mult was folded).
if ! grep -q "uPass(lgraph) - mult_folded:" "${OUT_FILE}"; then
  echo "FAIL: expected mult_folded summary line not found"
  cat "${OUT_FILE}"
  exit 2
fi

# 3. The dce summary line must appear.
if ! grep -q "uPass(lgraph) - dce_removed:" "${OUT_FILE}"; then
  echo "FAIL: expected dce_removed summary line not found"
  cat "${OUT_FILE}"
  exit 3
fi

# 4. DCE must have actually removed at least one node.
if ! grep -qE "uPass\(lgraph\) - dce_removed:[1-9]" "${OUT_FILE}"; then
  echo "FAIL: expected dce_removed to be non-zero"
  cat "${OUT_FILE}"
  exit 4
fi

# 5. The runner must converge.
if ! grep -q "uPass(lgraph) - converged at iteration" "${OUT_FILE}"; then
  echo "FAIL: expected convergence message not found"
  cat "${OUT_FILE}"
  exit 5
fi

echo "PASS: lgraph dce pass runs, removes dead nodes, and converges"
