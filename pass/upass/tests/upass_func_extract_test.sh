#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
OUT_FILE="${TEST_TMPDIR}/upass_func_extract.out"
EMPTY_PRP="${TEST_TMPDIR}/upass_func_extract_empty.prp"
EMPTY_OUT_FILE="${TEST_TMPDIR}/upass_func_extract_empty.out"

"${LGSHELL}" -q -c "inou.prp files:${PRP_FILE} |> pass.upass constprop:false assert:false verifier:false max_iters:1 |> lnast.dump" \
  >"${OUT_FILE}" 2>&1

if ! grep -q "uPass - resolved order: func_extract" "${OUT_FILE}"; then
  echo "FAIL: expected func_extract-only order not found"
  cat "${OUT_FILE}"
  exit 1
fi

if ! grep -q "  simple.top" "${OUT_FILE}"; then
  echo "FAIL: extracted function LNAST simple.top not found"
  cat "${OUT_FILE}"
  exit 2
fi

if ! grep -q "io:" "${OUT_FILE}"; then
  echo "FAIL: extracted function LNAST does not contain io node"
  cat "${OUT_FILE}"
  exit 3
fi

if grep -q "fdef" "${OUT_FILE}"; then
  echo "FAIL: original func_def was not removed"
  cat "${OUT_FILE}"
  exit 4
fi

cat >"${EMPTY_PRP}" <<'EOF'
comb answer() -> (r) {
  r = 3
}
EOF

"${LGSHELL}" -q -c "inou.prp files:${EMPTY_PRP} |> pass.upass constprop:false assert:false verifier:false max_iters:1 |> lnast.dump" \
  >"${EMPTY_OUT_FILE}" 2>&1

if ! grep -q "  upass_func_extract_empty.answer" "${EMPTY_OUT_FILE}"; then
  echo "FAIL: extracted no-input function LNAST not found"
  cat "${EMPTY_OUT_FILE}"
  exit 5
fi

if ! grep -q "ref: __empty_tuple" "${EMPTY_OUT_FILE}"; then
  echo "FAIL: no-input function does not reference __empty_tuple"
  cat "${EMPTY_OUT_FILE}"
  exit 6
fi

echo "PASS: func_def extraction emits function LNAST with io"
