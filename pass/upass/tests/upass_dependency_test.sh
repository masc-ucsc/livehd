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

# A reduced order must still reject a runtime-dependent elaboration assertion.
# Keep this distinct from simple.prp: its untyped comb is a template whose
# constant calls can fold once SSA has populated its I/O metadata.
RUNTIME_PRP="${TEST_TMPDIR}/runtime_cassert.prp"
cat >"${RUNTIME_PRP}" <<'PRP'
pub comb runtime_cassert(a:U8) -> (y:U8) {
  cassert(a == 1)
  y = a
}
PRP
ERR_OUT="${TEST_TMPDIR}/upass_dependency_tolg.out"
if "${LHD}" compile "${RUNTIME_PRP}" --set upass.order=assert --workdir "${TEST_TMPDIR}/w2" \
     --emit "verilog:${TEST_TMPDIR}/w2.v" >"${ERR_OUT}" 2>&1; then
  echo "FAIL: a runtime-dependent cassert must not exit 0"
  cat "${ERR_OUT}"
  exit 4
fi
if ! grep -q 'cassert-not-comptime' "${ERR_OUT}"; then
  echo "FAIL: the reduced-order failure must be the directed cassert-not-comptime diagnostic"
  cat "${ERR_OUT}"
  exit 5
fi

echo "PASS: dependency ordering works"
