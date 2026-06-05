#!/bin/sh
# Round-trip test via the lhd kernel: Pyrope -> LNAST -> uPass -> Pyrope -> uPass
# Verifies that pass.prp_writer (--emit-dir pyrope:) produces valid Pyrope that
# can be re-parsed. lhd checks the diag sink after every step, so "no errors"
# is simply exit code 0.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/comptime/trivial_if.prp"
ODIR="${TEST_TMPDIR}/prp_out"

# Pass 1: read -> upass (verifier on, as the lgshell default pipeline ran) ->
# emit Pyrope through pass.prp_writer.
"${LHD}" compile "${PRP_FILE}" --set upass.verifier=true \
  --emit-dir pyrope:"${ODIR}/" --workdir "${TEST_TMPDIR}/w1" -q \
  --result-json "${TEST_TMPDIR}/r1.json" || {
  echo "FAIL: pass 1 exited non-zero"
  cat "${TEST_TMPDIR}/r1.json"
  exit 1
}

if [ ! -f "${ODIR}/trivial_if.prp" ]; then
  echo "FAIL: pass 1 did not produce ${ODIR}/trivial_if.prp"
  cat "${TEST_TMPDIR}/r1.json"
  exit 1
fi

echo "Pass 1 output:"
cat "${ODIR}/trivial_if.prp"

# Pass 2: re-read the emitted Pyrope -> upass (must succeed with no errors).
"${LHD}" compile "${ODIR}/trivial_if.prp" --set upass.verifier=true \
  --workdir "${TEST_TMPDIR}/w2" -q --result-json "${TEST_TMPDIR}/r2.json" || {
  echo "FAIL: pass 2 produced errors when re-parsing the emitted Pyrope"
  cat "${TEST_TMPDIR}/r2.json"
  exit 2
}

echo "PASS: trivial_if round-trip succeeded"
