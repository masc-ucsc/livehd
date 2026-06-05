#!/bin/sh
# pass.upass dependency resolution via the lhd kernel: order:assert must pull
# in constprop (assert depends_on constprop). The uPass stdout diagnostics
# land in the per-step log under --workdir.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
W="${TEST_TMPDIR}/w"
OUT_FILE="${TEST_TMPDIR}/upass_dependency.out"

"${LHD}" compile "${PRP_FILE}" --set upass.order=assert --workdir "${W}" -q \
  --result-json "${TEST_TMPDIR}/result.json" >/dev/null 2>&1

cat "${W}"/logs/*pass_upass*.log >"${OUT_FILE}"

if ! grep -q "uPass - resolved order: constprop assert" "${OUT_FILE}"; then
  echo "FAIL: expected resolved dependency order not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "uPass - add constprop" "${OUT_FILE}"; then
  echo "FAIL: expected constprop insertion not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "uPass - add assert" "${OUT_FILE}"; then
  echo "FAIL: expected assert pass not found"
  cat "${OUT_FILE}"
  exit 3
fi

echo "PASS: dependency ordering works"
