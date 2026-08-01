#!/bin/sh
# A Verilog `unique case` lowers to a unique-if LNAST and then to a collapsed
# mux expression. The writer must not silently turn it into a priority `if`:
# re-reading the emitted Pyrope must reconstruct a Hotmux.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
V_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/prp_writer/tests/hotmux_emit.sv"
ODIR="${TEST_TMPDIR}/out"
OUT="${TEST_TMPDIR}/emitted_all.prp"

"${LHD}" compile "${V_FILE}" --emit-dir pyrope:"${ODIR}/" \
  --workdir "${TEST_TMPDIR}/w1" -q --result-json "${TEST_TMPDIR}/r1.json" || {
  echo "FAIL: SystemVerilog -> Pyrope emit exited non-zero"
  cat "${TEST_TMPDIR}/r1.json" 2>/dev/null || true
  exit 1
}

cat "${ODIR}"/*.prp > "${OUT}" 2>/dev/null || true
if [ ! -s "${OUT}" ]; then
  echo "FAIL: the emit produced no Pyrope"
  exit 2
fi

if ! grep -Eq 'match |unique[[:space:]]+if ' "${OUT}"; then
  echo "FAIL: unique case was emitted as a priority mux"
  cat "${OUT}"
  exit 3
fi

"${LHD}" compile "${ODIR}"/*.prp --workdir "${TEST_TMPDIR}/w2" -q \
  --result-json "${TEST_TMPDIR}/r2.json" || {
  echo "FAIL: emitted hotmux Pyrope does not recompile"
  cat "${TEST_TMPDIR}/r2.json" 2>/dev/null || true
  exit 4
}

echo "PASS: unique case remains match/unique-if and recompiles"
