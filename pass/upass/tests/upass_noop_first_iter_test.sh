#!/bin/sh
# pass.upass order:noop via the lhd kernel: the no-op pass registers, runs,
# and completes its walk. Diagnostics come from the per-step log.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
W="${TEST_TMPDIR}/w"
OUT_FILE="${TEST_TMPDIR}/upass_noop_first_iter.out"

"${LHD}" compile "${PRP_FILE}" --set upass.order=noop --workdir "${W}" -q \
  --result-json "${TEST_TMPDIR}/result.json" >/dev/null 2>&1

cat "${W}"/logs/*pass_upass*.log >"${OUT_FILE}"

if ! grep -q "uPass - resolved order: noop" "${OUT_FILE}"; then
  echo "FAIL: expected noop resolved-order diagnostic not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - add noop" "${OUT_FILE}"; then
  echo "FAIL: expected noop pass registration not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass - walk complete" "${OUT_FILE}"; then
  echo "FAIL: expected walk-complete marker not found"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: no-op pass runs and completes its walk"
