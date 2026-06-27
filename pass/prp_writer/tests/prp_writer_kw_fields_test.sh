#!/bin/sh
# Regression: a reconstructed bundle whose BASE and/or FIELD names are Pyrope
# reserved keywords must be emitted with EACH colliding component backtick-
# escaped per-component (`` wire `in`:(`reg`:u8, …) `` / `` `in`.`reg` ``), so the
# emitted Pyrope re-parses.  A non-keyword field (`valid`) must stay bare.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/prp_writer/tests/kw_bundle_keywords.prp"
ODIR="${TEST_TMPDIR}/kw_out"

# Pass 1: source -> upass -> emit Pyrope through pass.prp_writer.
"${LHD}" compile "${PRP_FILE}" --top kw_bundle_keywords \
  --emit-dir pyrope:"${ODIR}/" --workdir "${TEST_TMPDIR}/w1" -q \
  --result-json "${TEST_TMPDIR}/r1.json" || {
  echo "FAIL: pass 1 (emit) exited non-zero"
  cat "${TEST_TMPDIR}/r1.json" 2>/dev/null || true
  exit 1
}

EMITTED="$(grep -rl 'wire' "${ODIR}" 2>/dev/null | head -1 || true)"
if [ -z "${EMITTED}" ]; then
  echo "FAIL: pass 1 produced no .prp containing the bundle declarations"
  ls -la "${ODIR}" || true
  exit 1
fi
echo "Emitted:"
cat "${EMITTED}"

# Keyword bases escaped in the declarations.
grep -q 'wire `in`:(' "${EMITTED}"   || { echo "FAIL: base \`in\` not escaped"; exit 2; }
grep -q 'wire `wire`:(' "${EMITTED}" || { echo "FAIL: base \`wire\` not escaped"; exit 2; }
# Keyword FIELD names escaped, both in the decl and in field accesses.
for tok in '`reg`:u8' '`mut`:u8' '`match`:u1' '`type`:u8' '`for`:u8' '`in`.`reg`' '`wire`.`type`'; do
  grep -qF "${tok}" "${EMITTED}" || { echo "FAIL: expected escaped token '${tok}' not found"; exit 2; }
done
# The ordinary (non-keyword) field stays bare.
grep -q '`wire`.valid' "${EMITTED}" || { echo "FAIL: non-keyword field 'valid' should stay bare after the escaped base"; exit 2; }
# No BARE keyword base leaked.
if grep -qE 'wire (in|wire):\(' "${EMITTED}"; then
  echo "FAIL: a bare keyword base leaked"
  exit 2
fi

# Pass 2: the emitted Pyrope must re-parse with no errors.
"${LHD}" compile "${EMITTED}" --top kw_bundle_keywords \
  --workdir "${TEST_TMPDIR}/w2" -q --result-json "${TEST_TMPDIR}/r2.json" || {
  echo "FAIL: pass 2 produced errors when re-parsing the emitted Pyrope"
  cat "${TEST_TMPDIR}/r2.json" 2>/dev/null || true
  exit 3
}

echo "PASS: keyword tuple-name + keyword-field round-trip succeeded"
