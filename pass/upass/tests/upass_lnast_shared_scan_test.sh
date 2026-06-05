#!/bin/sh
# pass.upass order:scan_shared via the lhd kernel: the shared scan pass runs
# over the LNAST and reports non-empty node metrics in the per-step log.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
W="${TEST_TMPDIR}/w"
OUT_FILE="${TEST_TMPDIR}/upass_lnast_shared_scan.out"

"${LHD}" compile "${PRP_FILE}" --set upass.order=scan_shared --workdir "${W}" -q \
  --result-json "${TEST_TMPDIR}/result.json" >/dev/null 2>&1

cat "${W}"/logs/*pass_upass*.log >"${OUT_FILE}"

if ! grep -q "uPass - resolved order: scan_shared" "${OUT_FILE}"; then
  echo "FAIL: expected lnast shared scan order log not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - shared scan on lnast nodes:" "${OUT_FILE}"; then
  echo "FAIL: expected shared scan log for lnast not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass - shared scan summary nodes:" "${OUT_FILE}"; then
  echo "FAIL: expected shared scan summary log for lnast not found"
  cat "${OUT_FILE}"
  exit 3
fi

if ! grep -Eq "shared scan summary nodes:[1-9][0-9]* const:[0-9]+ arith:[0-9]+" "${OUT_FILE}"; then
  echo "FAIL: expected non-empty shared scan summary metrics not found"
  cat "${OUT_FILE}"
  exit 4
fi

if ! grep -q "uPass - walk complete" "${OUT_FILE}"; then
  echo "FAIL: expected lnast shared scan walk-complete marker not found"
  cat "${OUT_FILE}"
  exit 5
fi

echo "PASS: lnast shared scan pass runs and reports node count"
