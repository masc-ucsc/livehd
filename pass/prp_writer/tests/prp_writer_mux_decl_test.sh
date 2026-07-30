#!/bin/sh
# REGRESSION: generated Pyrope must always RE-PARSE — the writer may never emit
# a read of a name it does not declare.
#
# upass/prp_writer had two analyses claim the same statement. A Verilog ternary
# lowers to a merge temp plus the store that consumes it (`out = _mux_1`);
# analyze_muxes runs first and folded that store away as the unconditional seed
# that becomes the enclosing `if (cond) out = 3` mux's else value, and then
# analyze_expr_inlines separately picked the very same store as the temp's
# single use — inlining the mux expression there and DROPPING the temp's
# definition. The folded store renders through render_def_rhs, which does not
# inline a mux, so the emitted Pyrope read a bare `_mux_1` that nothing
# declared and did not recompile:
#     read of undefined variable '_mux_1'
#
# analyze_expr_inlines now declines the inline when the consuming store is
# itself already folded, so the temp keeps its own statement.
#
# The gate is the invariant, not the mechanism: RE-COMPILING the writer's own
# output must succeed. The `_mux_` declaration check below is a corroborating
# mechanism check, skipped if a future writer spells the shape without a temp.
# Sibling coverage: //inou/prp:prp-v2v-writer_undefined_mux and
# prp-v2prp-writer_undefined_mux prove the round trip EQUIVALENT (this test only
# proves it re-parses, in 0.1s and with no yosys/solver); and
# //inou/prp:prp-undefined_read_if_expr_arm pins the other half of the contract
# — such a read is a compile error, so a regressed writer fails loudly.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
V_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/prp_writer/tests/mux_seed_fold.v"
ODIR="${TEST_TMPDIR}/out"
OUT="${TEST_TMPDIR}/emitted_all.prp"

"${LHD}" compile "${V_FILE}" --emit-dir pyrope:"${ODIR}/" \
  --workdir "${TEST_TMPDIR}/w1" -q --result-json "${TEST_TMPDIR}/r1.json" || {
  echo "FAIL: verilog -> Pyrope emit exited non-zero"
  cat "${TEST_TMPDIR}/r1.json" 2>/dev/null || true
  exit 1
}

# The emit writes one file per unit; concatenate them all so the test does not
# depend on which file the body lands in.
cat "${ODIR}"/*.prp > "${OUT}" 2>/dev/null || true
if [ ! -s "${OUT}" ]; then
  echo "FAIL: the emit produced no Pyrope"
  ls -la "${ODIR}" 2>/dev/null || true
  exit 1
fi

echo "emitted:"
cat "${OUT}"

# Guard against a vacuous pass: the emitted body must actually contain the
# module (an empty or unrelated emit would recompile clean for the wrong reason).
grep -q 'mux_seed_fold' "${OUT}" || {
  echo "FAIL: emitted Pyrope does not contain the fixture's module"
  exit 1
}

# 1. Mechanism: every `_mux_N` the output READS must also be DECLARED. `mut`,
#    `const`, `reg`, `wire` and a bare `_mux_N =` first write all declare it;
#    what must never happen is the name appearing ONLY on a right-hand side.
for m in $(grep -oE '_mux_[0-9]+' "${OUT}" | sort -u); do
  if ! grep -qE "(^|[^[:alnum:]_])(mut|const|reg|wire)[[:space:]]+${m}([^[:alnum:]_]|$)" "${OUT}" \
     && ! grep -qE "^[[:space:]]*${m}[[:space:]]*=" "${OUT}"; then
    echo "FAIL: emitted Pyrope reads '${m}' but never declares it"
    exit 2
  fi
done

# 2. The invariant: the writer's own output must recompile. Pre-fix this exits 6
#    with "read of undefined variable '_mux_1'".
"${LHD}" compile "${ODIR}"/*.prp --workdir "${TEST_TMPDIR}/w2" -q \
  --result-json "${TEST_TMPDIR}/r2.json" || {
  echo "FAIL: the emitted Pyrope does not recompile — the writer emitted a name"
  echo "      it never declared (generated Pyrope must always re-parse)."
  cat "${TEST_TMPDIR}/r2.json" 2>/dev/null || true
  exit 3
}

echo "PASS: the emitted Pyrope declares every _mux_N it reads and recompiles"
