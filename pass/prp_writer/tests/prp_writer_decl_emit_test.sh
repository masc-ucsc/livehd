#!/bin/sh
# REGRESSION: the writer must not LOSE information when it rebuilds a
# declaration, and must not emit a module name Pyrope cannot tokenize.
#
# Both bugs below reached the emitted TEXT, and both were invisible to the
# equivalence harnesses at the time (prp-v2v-* went slang -> lg -> cgen directly
# and never ran the writer). This gate asserts on the text itself, in ~0.1s with
# no yosys and no solver. Equivalence coverage of the same two shapes lives in
# //inou/prp:prp-v2v-tolg_field_read_zero and prp-v2v-writer_bad_char (plus the
# prp-v2prp-* and prp-equiv-* legs of each).
#
# 1. NESTED-MUT HOIST DROPPED THE DECLARATION. A `mut` declared in a nested
#    scope but usable from sibling scopes is hoisted to the function top and its
#    original declare is deleted (suppress_decl_). The hoist recorded the NAME
#    only, so an array whose sole use is a runtime-indexed read inside an `if`
#    — the reader declares a symbol at first use, so its declare lands there —
#    came back as a bare `mut mem = 0`: the `[2]u4` type AND the `initial`-block
#    contents the reader had folded in as the declare's initializer were both
#    gone. tolg then met `mem[sel]` on a base constprop had folded to the scalar
#    0 and aborted with "field/index read of '0' could not be resolved".
#
#    The two halves must land TOGETHER, which is why this asserts the whole
#    line: restoring only the type makes the design COMPILE and read 0,0 where
#    the golden reads 1,2 — a loud error traded for a silent miscompile.
#
# 2. MODULE NAMES WERE NOT ESCAPED. Signal names go through the writer's
#    backtick escaper; module names did not. A Verilog escaped identifier
#    (`\d\e`) was emitted verbatim into the Pyrope header, and `\` is not a
#    legal Pyrope token, so the generated file could not even be TOKENIZED
#    ("unexpected character in input").
#
# The closing invariant is the same one prp_writer_mux_decl_test.sh gates: the
# writer's own output must RE-COMPILE.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
V_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/prp_writer/tests/decl_emit.v"
ODIR="${TEST_TMPDIR}/out"
OUT="${TEST_TMPDIR}/emitted_all.prp"

"${LHD}" compile "${V_FILE}" --emit-dir pyrope:"${ODIR}/" \
  --workdir "${TEST_TMPDIR}/w1" -q --result-json "${TEST_TMPDIR}/r1.json" || {
  echo "FAIL: verilog -> Pyrope emit exited non-zero"
  cat "${TEST_TMPDIR}/r1.json" 2>/dev/null || true
  exit 1
}

# One file per unit; concatenate so the test does not depend on the file name
# (which is the module name, and this module's name has a backslash in it).
cat "${ODIR}"/*.prp > "${OUT}" 2>/dev/null || true
if [ ! -s "${OUT}" ]; then
  echo "FAIL: the emit produced no Pyrope"
  ls -la "${ODIR}" 2>/dev/null || true
  exit 1
fi

echo "emitted:"
cat "${OUT}"

# 1. The hoisted declaration must carry BOTH the array type and the contents.
#    Pre-fix this line reads `mut mem = 0`.
grep -q 'mut mem:\[2\]u4 = (1, 2)' "${OUT}" || {
  echo "FAIL: the hoisted declaration lost its type and/or its initializer."
  echo "      expected: mut mem:[2]u4 = (1, 2)"
  echo "      got:      $(grep -E '^[[:space:]]*mut mem' "${OUT}" || echo '<no mut mem line at all>')"
  exit 2
}

# 2. The escaped module name must be backtick-quoted in the header. Pre-fix the
#    header reads `pub comb d\e::[…]` and the file does not tokenize.
grep -q 'pub comb `d\\e`::' "${OUT}" || {
  echo "FAIL: the escaped Verilog module name was not backtick-escaped."
  echo '      expected the header to spell it `d\e`'
  echo "      got:      $(grep -E '^pub ' "${OUT}" || echo '<no pub header>')"
  exit 3
}

# Guard against a vacuous pass on either check: the module must really be here.
grep -q 'sel' "${OUT}" && grep -q 'mem\[sel\]' "${OUT}" || {
  echo "FAIL: emitted Pyrope does not contain the fixture's body"
  exit 4
}

# 3. The invariant: the writer's own output must recompile. Pre-fix this exits
#    nonzero — on the tokenizer for the module name, or on tolg for the array.
"${LHD}" compile "${ODIR}"/*.prp --workdir "${TEST_TMPDIR}/w2" -q \
  --result-json "${TEST_TMPDIR}/r2.json" || {
  echo "FAIL: the emitted Pyrope does not recompile"
  cat "${TEST_TMPDIR}/r2.json" 2>/dev/null || true
  exit 5
}

echo "PASS: the hoisted declaration keeps its type and contents, the escaped"
echo "      module name is backtick-quoted, and the output recompiles"
