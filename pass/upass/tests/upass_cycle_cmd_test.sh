#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/pyrope/tests/sum2.prp"
OUT_FILE="${TEST_TMPDIR}/upass_cycle_cmd.out"

printf 'inou.pyrope files:%s |> pass.upass order:__upass_cycle_cmd_a max_iters:3\nquit\n' "${PRP_FILE}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT_FILE}" 2>&1

if ! grep -q "uPass dependency cycle detected at __upass_cycle_cmd_a" "${OUT_FILE}"; then
  echo "FAIL: expected dependency-cycle diagnostic not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass dependency chain for __upass_cycle_cmd_b is invalid" "${OUT_FILE}"; then
  echo "FAIL: expected invalid-chain diagnostic not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "ERROR: pass.upass invalid pass configuration: dependency cycle detected at '__upass_cycle_cmd_a'" "${OUT_FILE}"; then
  echo "FAIL: expected pass.upass cycle error not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -q "command aborted" "${OUT_FILE}"; then
  echo "FAIL: expected command-aborted output not found for cycle"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: command-path dependency cycle is detected and cleanly aborted"
