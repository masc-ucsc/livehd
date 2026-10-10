#!/usr/bin/env bash
set -uo pipefail
# Adversarial controls for pass/lean/scripts/axiom_audit.py.
#
# A gate nobody has tried to defeat is a gate nobody knows the strength of. The
# original audit was a LINE COUNT and passed controls 1-8; controls 9-12 are
# holes found in the replacement after it shipped. Each is a way an artifact
# could claim a proof it does not have.
#
# Usage: axiom_audit_controls.sh <verified.log> <VerifiedTop> <verified.lean>
#                                [<legacy.log> <LegacyTop> <legacy.lean>]

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
AUDIT="$R/pass/lean/scripts/axiom_audit.py"
GOOD="${1:?usage: axiom_audit_controls.sh <verified.log> <Top> <artifact.lean> [<legacy.log> <Top> <artifact.lean>]}"
TOP="${2:?}"
ART="${3:?}"
LGOOD="${4:-}"; LTOP="${5:-}"; LART="${6:-}"
T="$(mktemp -d "${TMPDIR:-$R/generated/pass_lean_runtime_tmp}/axctl.XXXXXX")"
trap 'rm -rf "$T"' EXIT
rc=0

reject() {  # <name> <log> [extra args...]
  local n="$1" l="$2"; shift 2
  if python3 -I "$AUDIT" "$l" "$@" >/dev/null 2>&1; then
    echo "FAIL: control '$n' was ACCEPTED; the auditor does not catch it"; rc=1
  else
    echo "ok: $n rejected"
  fi
}
accept() {  # <name> <log> [extra args...]
  local n="$1" l="$2"; shift 2
  if python3 -I "$AUDIT" "$l" "$@" >/dev/null 2>&1; then
    echo "ok: $n accepted"
  else
    echo "FAIL: genuine '$n' was REJECTED -- the auditor is too strict"; rc=1
    python3 -I "$AUDIT" "$l" "$@" 2>&1 | head -3 | sed 's/^/      /'
  fi
}

# --- 1-8: report-level attacks ------------------------------------------
head -1 "$GOOD"                                   > "$T/c1_missing.log"
sed "s/${TOP}_step_correct/foo_bar/"      "$GOOD" > "$T/c2_wrongname.log"
sed "s/Quot.sound/MyEvilAxiom/"           "$GOOD" > "$T/c3_customax.log"
sed "s/Quot.sound/sorryAx/"               "$GOOD" > "$T/c4_importedsorry.log"
grep -v 'clock_bounds._native'            "$GOOD" > "$T/c6_nobounds.log"
sed "s/${TOP}_compiles\._native/${TOP}_arbitrary_false._native/" \
                                          "$GOOD" > "$T/c7_fakenative.log"
sed "s/Quot.sound/Lean.trustCompiler/"    "$GOOD" > "$T/c8_trustaxiom.log"
{
  echo "'${TOP}_step_correct' depends on axioms: [propext, Classical.choice,"
  echo "  Quot.sound, ${TOP}_compiles._native.native_decide.ax_1_1]"
  echo "'${TOP}_step_legacy_correct' depends on axioms: [propext, Classical.choice,"
  echo "  Quot.sound, ${TOP}_compiles._native.native_decide.ax_1_1,"
  echo "  ${TOP}_clock_bounds._native.native_decide.ax_1_1, SmuggledAxiom]"
} > "$T/c5_wrapped.log"
for c in c1_missing c2_wrongname c3_customax c4_importedsorry c5_wrapped \
         c6_nobounds c7_fakenative c8_trustaxiom; do
  reject "$c" "$T/$c.log" "$TOP" --mode verified
done

# --- 9: the VACUOUS pass -------------------------------------------------
# An artifact proving only `Unrecognized : True`, audited with no obligations.
# The queue used exactly this combination for an unrecognized mode, and every
# other condition then passed, so it could print PROVEN.
cat > "$T/c9_vacuous.lean" <<'LEAN'
namespace Unrecognized_Lgraph
theorem Unrecognized_unrelated : True := trivial
#print axioms Unrecognized_unrelated
end Unrecognized_Lgraph
LEAN
echo "'Unrecognized_Lgraph.Unrecognized_unrelated' does not depend on any axioms" > "$T/c9_vacuous.log"
reject c9_vacuous_explicit "$T/c9_vacuous.log" Unrecognized --required "" --witnesses auto --artifact "$T/c9_vacuous.lean"
reject c9_vacuous_derived  "$T/c9_vacuous.log" Unrecognized --artifact "$T/c9_vacuous.lean"

# --- 10: a NAMESPACE SPOOF ----------------------------------------------
# A suffix match would accept `evil.<Top>_step_correct`. The namespace is read
# from the artifact, so a report under any other namespace is not the obligation.
sed "s/^'/'evil./" "$GOOD" > "$T/c10_nsspoof.log"
reject c10_nsspoof "$T/c10_nsspoof.log" "$TOP" --mode verified --artifact "$ART"

# --- 11-12: legacy SCOPE, only when a legacy fixture is supplied ---------
if [[ -n "$LGOOD" && -r "$LGOOD" && -r "$LART" ]]; then
  accept c0_legacy_genuine "$LGOOD" "$LTOP" --artifact "$LART"
  # A SEQUENTIAL design missing `_next_refines_fast` must FAIL, not be quietly
  # downgraded to "combinational, so next was never required".
  if grep -q "^structure ${LTOP}_state" "$LART"; then
    grep -v "${LTOP}_next_refines_fast" "$LGOOD" > "$T/c11_partial.log"
    reject c11_partial_sequential "$T/c11_partial.log" "$LTOP" --artifact "$LART"
  else
    echo "skip: c11 needs a SEQUENTIAL legacy fixture (no ${LTOP}_state)"
  fi
  # An artifact with a graph certificate must prove its well-formedness.
  grep -v "${LTOP}_graphCert_wf" "$LGOOD" > "$T/c12_nowf.log"
  reject c12_missing_graphcert_wf "$T/c12_nowf.log" "$LTOP" --artifact "$LART"
else
  echo "skip: c11/c12 need a legacy fixture (pass log, Top, artifact as \$4-\$6)"
fi

accept genuine_verified "$GOOD" "$TOP" --mode verified --artifact "$ART"
[[ $rc -eq 0 ]] && echo "PASS: every control rejected, every genuine artifact accepted"
exit $rc
