#!/bin/sh
# pass.upass order:decide_shared via the lhd kernel: the shared decide pass
# runs over the LNAST and reports fold candidates in the per-step log.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
W="${TEST_TMPDIR}/w"
OUT_FILE="${TEST_TMPDIR}/upass_lnast_shared_decide.out"

"${LHD}" compile "${PRP_FILE}" --set upass.order=decide_shared --workdir "${W}" -q \
  --result-json "${TEST_TMPDIR}/result.json" >/dev/null 2>&1

cat "${W}"/logs/*pass_upass*.log >"${OUT_FILE}"

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

if ! grep -q "uPass - walk complete" "${OUT_FILE}"; then
  echo "FAIL: expected lnast shared decide walk-complete marker not found"
  cat "${OUT_FILE}"
  exit 4
fi

echo "PASS: lnast shared decide pass runs and reports fold candidates"
