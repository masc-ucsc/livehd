#!/bin/sh
# REGRESSION: pass.prp_writer renames `<v>___ssa_<N>` to `<v>__w<N>` on emit
# (lnast_prp_writer.cpp emit_name).  `__wN` is NOT a free namespace — the
# writer itself emits it, so generated Pyrope is full of source-level `__wN`
# names.  Renaming onto an occupied name makes two distinct variables share one
# identifier in the emitted source: a SILENT MISCOMPILE of the writer's own
# output, needing no import loop and no multi-file compile.
#
# This is the twin of the SSA-entry demotion fixed by
# //lhd/tests:upass_converged_rewalk_test, on the OUTPUT side; that fix does
# not cover this one.  Tracked as lhdsuite fixme.md issue 16.
#
# Pre-fix, the fixture emits
#     mut v__w1 = 0
#     v__w1 = b            <- assigned, then NEVER READ
#     o = v + 3
#     q = v + 3            <- WRONG: q must be b
# because `v___ssa_1` demoted straight onto the source's `v__w1`.
#
# The assertions read the EMITTED TEXT rather than re-compiling it, because the
# writer's output for this shape does not currently lower back to an LGraph
# ("no synthesizable modules") — a separate round-trip gap.  The emitted text is
# the writer's deliverable, so checking it tests the right artifact.

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"
PRP_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/pass/prp_writer/tests/ssa_demote_collision.prp"
ODIR="${TEST_TMPDIR}/out"
# The emit writes one file per unit — a (possibly empty) file-level unit plus
# `<file>.<entity>.prp` for the module. Assert over ALL of them so the test
# does not depend on which file the body lands in.
OUT="${TEST_TMPDIR}/emitted_all.prp"

"${LHD}" compile "${PRP_FILE}" --top ssa_demote_collision \
  --emit-dir pyrope:"${ODIR}/" --workdir "${TEST_TMPDIR}/w1" -q \
  --result-json "${TEST_TMPDIR}/r1.json" || {
  echo "FAIL: emit exited non-zero"
  cat "${TEST_TMPDIR}/r1.json" 2>/dev/null || true
  exit 1
}

cat "${ODIR}"/*.prp > "${OUT}" 2>/dev/null || true
if [ ! -s "${OUT}" ]; then
  echo "FAIL: the emit produced no Pyrope"
  ls -la "${ODIR}" 2>/dev/null || true
  exit 1
fi

# Guard against the assertions below passing vacuously: the emitted body must
# actually contain the module (and so the collision-prone declaration).
grep -q 'mut v__w1' "${OUT}" || {
  echo "FAIL: emitted Pyrope does not contain the fixture's v__w1 declaration"
  cat "${OUT}"
  exit 1
}

echo "emitted:"
cat "${OUT}"

# 1. `v__w1` holds the input `b` and MUST be read — that read is the only path
#    from `b` to an output.  Under the collision it is written and then dead,
#    so no line has `v__w1` on the right of an `=`.
if ! grep -qE '=[^=]*v__w1' "${OUT}"; then
  echo "FAIL: emitted Pyrope never READS v__w1 — the demoted SSA name collided"
  echo "      with it, so input b no longer reaches any output."
  exit 2
fi

# 2. Corroborating check: `o` and `q` are plainly different in the source, so
#    they must not be emitted with an identical driving expression.  Under the
#    collision both come out as `v + 3`.
o_rhs=$(sed -n 's/^[[:space:]]*o[[:space:]]*=[[:space:]]*\(.*\)$/\1/p' "${OUT}")
q_rhs=$(sed -n 's/^[[:space:]]*q[[:space:]]*=[[:space:]]*\(.*\)$/\1/p' "${OUT}")
if [ -n "${o_rhs}" ] && [ "${o_rhs}" = "${q_rhs}" ]; then
  echo "FAIL: outputs o and q were emitted with the same expression '${o_rhs}'"
  echo "      — two distinct variables collapsed onto one emitted identifier."
  exit 3
fi

echo "PASS: the demoted SSA name did not collide with the source's __wN"
