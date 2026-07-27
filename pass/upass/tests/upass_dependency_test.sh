#!/bin/sh
# pass.upass dependency resolution via the lhd kernel: order:assert must pull
# in constprop (assert depends_on constprop). The uPass stdout diagnostics
# land in the per-step log under --workdir.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
W="${TEST_TMPDIR}/w"
OUT_FILE="${TEST_TMPDIR}/upass_dependency.out"

# upass.tolg=false: this test is about uPass ORDER RESOLUTION, not about
# producing hardware. `upass.order=assert` deliberately truncates the pass list
# (no verifier), so simple.prp's `cassert(c == 10)` — whose operand comes from a
# comb call the reduced order never folds — reaches the tolg seam undischarged
# and hard-errors `cassert-not-comptime` (the 2026-07-25 cassert ruling: an
# elaboration check must fold to true or it is an error). That seam explicitly
# exempts the LNAST-only tiers, which is exactly this one.
"${LHD}" compile "${PRP_FILE}" --set upass.order=assert --set upass.tolg=false \
  --workdir "${W}" -q --result-json "${TEST_TMPDIR}/result.json" >/dev/null 2>&1

cat "${W}"/logs/*pass_upass*.log >"${OUT_FILE}"

if ! grep -q "uPass - resolved order: attributes typecheck constprop assert" "${OUT_FILE}"; then
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

# The reduced order WITH tolg enabled is still a user-reachable configuration, so
# pin that it fails HONESTLY rather than crashing or silently emitting hardware.
# (Adding upass.tolg=false above fixed the test but would otherwise have deleted
# the only coverage of this path.) `cassert` is an elaboration check under the
# 2026-07-25 ruling: with the folding passes omitted from the order it cannot be
# discharged, and the contract is a DIRECTED diagnostic naming that, not silence.
# NB a HARDWARE emit is required: the cassert seam fires at tolg, and "reaching
# tolg is what says this compilation is producing hardware". Asking only for
# diagnostics is an LNAST-only flow, which the ruling explicitly exempts — so
# that spelling exits 0 and would make this case vacuous.
ERR_OUT="${TEST_TMPDIR}/upass_dependency_tolg.out"
if "${LHD}" compile "${PRP_FILE}" --set upass.order=assert --workdir "${TEST_TMPDIR}/w2" \
     --emit "verilog:${TEST_TMPDIR}/w2.v" >"${ERR_OUT}" 2>&1; then
  echo "FAIL: a reduced upass.order that cannot fold a cassert must not exit 0"
  cat "${ERR_OUT}"
  exit 4
fi
if ! grep -q 'cassert-not-comptime' "${ERR_OUT}"; then
  echo "FAIL: the reduced-order failure must be the directed cassert-not-comptime diagnostic"
  cat "${ERR_OUT}"
  exit 5
fi

echo "PASS: dependency ordering works"
