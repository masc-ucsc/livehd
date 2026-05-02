#!/bin/sh
# Round-trip test: Pyrope -> LNAST -> uPass -> Pyrope -> uPass
# Verifies that pass.prp_writer produces valid Pyrope that can be re-parsed.

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/comptime/trivial_if.prp"
ODIR="${TEST_TMPDIR}/prp_out"
OUT1="${TEST_TMPDIR}/pass1.out"
OUT2="${TEST_TMPDIR}/pass2.out"

mkdir -p "${ODIR}"

# Pass 1: read -> upass -> emit Pyrope
printf 'inou.prp files:%s |> pass.upass |> pass.lnastfmt |> pass.prp_writer odir:%s\nquit\n' \
  "${PRP_FILE}" "${ODIR}" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT1}" 2>&1

if [ ! -f "${ODIR}/trivial_if.prp" ]; then
  echo "FAIL: pass 1 did not produce ${ODIR}/trivial_if.prp"
  cat "${OUT1}"
  exit 1
fi

echo "Pass 1 output:"
cat "${ODIR}/trivial_if.prp"

# Pass 2: re-read the emitted Pyrope -> upass (should succeed with no errors)
printf 'inou.prp files:%s |> pass.upass |> pass.lnastfmt\nquit\n' \
  "${ODIR}/trivial_if.prp" \
  | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${OUT2}" 2>&1

if grep -v "cassert counts" "${OUT2}" | grep -qi "error\|fail\|exception"; then
  echo "FAIL: pass 2 produced errors when re-parsing the emitted Pyrope"
  cat "${OUT2}"
  exit 2
fi

echo "PASS: trivial_if round-trip succeeded"
