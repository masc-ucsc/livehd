#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/simple.prp"
PROC_PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/inou/prp/tests/pyrope/proc_lambda.prp"
OUT_FILE="${TEST_TMPDIR}/upass_func_extract.out"
EMPTY_PRP="${TEST_TMPDIR}/upass_func_extract_empty.prp"
EMPTY_OUT_FILE="${TEST_TMPDIR}/upass_func_extract_empty.out"
PROC_OUT_FILE="${TEST_TMPDIR}/upass_func_extract_proc.out"

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

if ! grep -q "io      :" "${OUT_FILE}"; then
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

if ! grep -q "ref     : __empty_tuple" "${EMPTY_OUT_FILE}"; then
  echo "FAIL: no-input function does not reference __empty_tuple"
  cat "${EMPTY_OUT_FILE}"
  exit 6
fi

"${LGSHELL}" -q -c "inou.prp files:${PROC_PRP_FILE} |> pass.lnastfmt |> pass.upass verifier:0 constprop:1 max_iters:1 |> lnast.dump" \
  >"${PROC_OUT_FILE}" 2>&1

if grep -q "const   : [ab]=[0-9]" "${PROC_OUT_FILE}"; then
  echo "FAIL: named fcall argument was emitted as raw a=5/b=2 const"
  cat "${PROC_OUT_FILE}"
  exit 7
fi

if grep -q "ref     : [\$%][abr]" "${PROC_OUT_FILE}"; then
  echo "FAIL: extracted function body used directed $/% refs instead of plain local refs"
  cat "${PROC_OUT_FILE}"
  exit 8
fi

if ! grep -q "ref     : __input_tuple_ref" "${PROC_OUT_FILE}" || ! grep -q "const   : nil" "${PROC_OUT_FILE}"; then
  echo "FAIL: extracted function IO tuple does not use nil-initialized fields"
  cat "${PROC_OUT_FILE}"
  exit 9
fi

if ! grep -q "assign  :" "${PROC_OUT_FILE}" || ! grep -q "ref     : do_thing" "${PROC_OUT_FILE}"; then
  echo "FAIL: tuple-shaped fcall with named assigns not found"
  cat "${PROC_OUT_FILE}"
  exit 10
fi

echo "PASS: func_def extraction emits function LNAST with io"
