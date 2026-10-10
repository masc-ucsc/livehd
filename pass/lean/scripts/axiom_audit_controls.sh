#!/usr/bin/env bash
set -uo pipefail
# Adversarial controls for pass/lean/scripts/axiom_audit.py.
#
# A gate nobody has tried to defeat is a gate nobody knows the strength of.  The
# previous audit was a LINE COUNT and passed every one of these.  Each control
# below is a way a file could claim a proof it does not have; the auditor must
# reject all six and accept the genuine log.
#
# Usage: axiom_audit_controls.sh <a-real-typecheck.log> <Top>

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
AUDIT="$R/pass/lean/scripts/axiom_audit.py"
GOOD="${1:?usage: axiom_audit_controls.sh <real-typecheck.log> <Top>}"
TOP="${2:?usage: axiom_audit_controls.sh <real-typecheck.log> <Top>}"
T="$(mktemp -d "${TMPDIR:-$R/generated/pass_lean_runtime_tmp}/axctl.XXXXXX")"
trap 'rm -rf "$T"' EXIT

[[ -s "$GOOD" ]] || { echo "FATAL: no reference log at $GOOD" >&2; exit 2; }

# 1 missing obligation   2 wrong theorem name    3 custom axiom
# 4 imported sorry       5 axiom smuggled onto a WRAPPED continuation line
# 6 legacy obligation that does not cite the separate clock-bounds fact
# 7 a NATIVE-SHAPED fact for an obligation nobody expects.  The first version of
#   the auditor matched native facts by SHAPE (`<Top>_<anything>._native...`),
#   which admits a native_decide of an arbitrary proposition -- a proof of
#   something, about nothing.
# 8 a BROADENED TRUST axiom.  `Lean.trustCompiler` and `Lean.ofReduceBool` were
#   allowlisted on the assumption they might appear; neither occurs anywhere in
#   the 160 measured reports, and allowlisting an unobserved axiom is how a
#   trust assumption widens without anyone deciding to widen it.
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

rc=0
for c in c1_missing c2_wrongname c3_customax c4_importedsorry c5_wrapped c6_nobounds \
         c7_fakenative c8_trustaxiom; do
  if python3 -I "$AUDIT" "$T/$c.log" "$TOP" >/dev/null 2>&1; then
    echo "FAIL: control '$c' was ACCEPTED; the auditor does not catch it"; rc=1
  else
    echo "ok: $c rejected"
  fi
done
if python3 -I "$AUDIT" "$GOOD" "$TOP" >/dev/null 2>&1; then
  echo "ok: genuine log accepted"
else
  echo "FAIL: the genuine log was REJECTED -- the auditor is too strict"; rc=1
fi
[[ $rc -eq 0 ]] && echo "PASS: all 8 controls rejected, genuine log accepted"
exit $rc
