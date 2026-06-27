#!/bin/sh
# Regression: a reconstructed bundle whose base is a Pyrope reserved keyword
# (`in:(ready, valid, bits)`, as in the XiangShan DCache CtrlUnit) must be
# emitted backtick-escaped by pass.prp_writer, otherwise the emitted Pyrope
# fails to re-parse with "expected an expression" at the `.` of `in.bits`.
#
# Pre-fix: the writer dropped the escaping and emitted bare `wire in:(...)` /
# `in.bits`, so Pass 2 (re-parse) failed.  Post-fix: it emits `` wire `in`:(...) ``
# / `` `in`.bits ``, which re-parses cleanly.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/prp_writer/tests/kw_bundle_base.prp"
ODIR="${TEST_TMPDIR}/kw_out"

# Pass 1: source -> upass -> emit Pyrope through pass.prp_writer.
"${LHD}" compile "${PRP_FILE}" --top kw_bundle_base \
  --emit-dir pyrope:"${ODIR}/" --workdir "${TEST_TMPDIR}/w1" -q \
  --result-json "${TEST_TMPDIR}/r1.json" || {
  echo "FAIL: pass 1 (emit) exited non-zero"
  cat "${TEST_TMPDIR}/r1.json" 2>/dev/null || true
  exit 1
}

EMITTED="$(grep -rl 'wire' "${ODIR}" 2>/dev/null | head -1 || true)"
if [ -z "${EMITTED}" ]; then
  echo "FAIL: pass 1 produced no .prp containing the bundle declaration"
  ls -la "${ODIR}" || true
  exit 1
fi
echo "Emitted bundle module: ${EMITTED}"
cat "${EMITTED}"

# The keyword base/field accesses must be backtick-escaped.
if ! grep -q 'wire `in`:(' "${EMITTED}"; then
  echo "FAIL: bundle declaration base not escaped (expected: wire \`in\`:( )"
  exit 2
fi
if ! grep -q '`in`.bits' "${EMITTED}"; then
  echo "FAIL: bundle field access base not escaped (expected: \`in\`.bits )"
  exit 2
fi
# The BARE keyword form (which fails to re-parse) must NOT leak.
if grep -q 'wire in:(' "${EMITTED}"; then
  echo "FAIL: bare keyword bundle declaration leaked (wire in:( )"
  exit 2
fi

# Pass 2: the emitted Pyrope must re-parse with no errors (the real guard).
"${LHD}" compile "${EMITTED}" --top kw_bundle_base \
  --workdir "${TEST_TMPDIR}/w2" -q --result-json "${TEST_TMPDIR}/r2.json" || {
  echo "FAIL: pass 2 produced errors when re-parsing the emitted Pyrope"
  cat "${TEST_TMPDIR}/r2.json" 2>/dev/null || true
  exit 3
}

echo "PASS: keyword-bundle round-trip succeeded"
