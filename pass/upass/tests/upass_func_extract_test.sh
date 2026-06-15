#!/bin/sh
# Lambda split via the lhd kernel (2g): a `comb` lambda is extracted into its
# own LNAST unit (one <unit>.lnast file in the lnast-dump: dir), and the
# original func_def is removed from the module body. The split is now a
# front-end tree surgery at the pipeline entry (not a runner pass), so there is
# no "resolved order: func_extract" log line to check — assert the IR instead.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
DUMP="${TEST_TMPDIR}/dump"
W="${TEST_TMPDIR}/w"
EMPTY_PRP="${TEST_TMPDIR}/upass_func_extract_empty.prp"
EMPTY_DUMP="${TEST_TMPDIR}/dump_empty"
EMPTY_W="${TEST_TMPDIR}/w_empty"

"${LHD}" compile "${PRP_FILE}" \
  --set upass.constprop=false --set upass.assert=false --set upass.verifier=false --set upass.ssa=false \
  --emit-dir lnast-dump:"${DUMP}/" --workdir "${W}" -q \
  --result-json "${TEST_TMPDIR}/result.json" >/dev/null 2>&1

if [ ! -f "${DUMP}/simple.top.lnast" ]; then
  echo "FAIL: extracted function LNAST simple.top not found"
  ls "${DUMP}"
  exit 2
fi

if ! grep -qE "── io( |$)" "${DUMP}/simple.top.lnast"; then
  echo "FAIL: extracted function LNAST does not contain io node"
  cat "${DUMP}/simple.top.lnast"
  exit 3
fi

if grep -q "fdef" "${DUMP}"/*.lnast; then
  echo "FAIL: original func_def was not removed"
  cat "${DUMP}"/*.lnast
  exit 4
fi

cat >"${EMPTY_PRP}" <<'EOF'
comb answer() -> (r) {
  r = 3
}
EOF

"${LHD}" compile "${EMPTY_PRP}" \
  --set upass.constprop=false --set upass.assert=false --set upass.verifier=false --set upass.ssa=false \
  --emit-dir lnast-dump:"${EMPTY_DUMP}/" --workdir "${EMPTY_W}" -q \
  --result-json "${TEST_TMPDIR}/result_empty.json" >/dev/null 2>&1

if [ ! -f "${EMPTY_DUMP}/upass_func_extract_empty.answer.lnast" ]; then
  echo "FAIL: extracted no-input function LNAST not found"
  ls "${EMPTY_DUMP}"
  exit 5
fi

if ! grep -q "ref '__empty_tuple'" "${EMPTY_DUMP}/upass_func_extract_empty.answer.lnast"; then
  echo "FAIL: no-input function does not reference __empty_tuple"
  cat "${EMPTY_DUMP}/upass_func_extract_empty.answer.lnast"
  exit 6
fi

echo "PASS: func_def extraction emits function LNAST with io"
