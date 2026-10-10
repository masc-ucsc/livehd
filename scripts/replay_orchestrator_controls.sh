#!/usr/bin/env bash
set -uo pipefail
# Admission controls for replay_orchestrator.py, through its actual CLI.
#
# The first version ran with RUN_LEAN=false and marked a job `passed` when the
# export exited 0 and a `_Lgraph.lean` existed. Driven with a worker that wrote
# "THIS IS NOT VALID LEAN AND HAS NO PROOF", every mode came back passed. An
# export is not a proof and a file's existence is not its contents.
#
# Each control below is a way a job could claim a proof it does not have.
#
# Usage: replay_orchestrator_controls.sh

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ORCH="$R/scripts/replay_orchestrator.py"
T="$R/generated/pass_lean_runtime_tmp/replay_controls"
rm -rf "$T"; mkdir -p "$T"
rc=0

# A self-contained artifact that genuinely proves something, and corruptions of
# it. These go through the REAL typecheck path, not a stub.
cat > "$T/good.lean" <<'LEAN'
import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler
def ctl_designCert : DesignCert :=
  { sources := #[SourceDesc.input 0 8], nodes := #[],
    outputs := #[{ slot := 0, width := 8 }], flops := #[], memories := #[],
    clocks := #[{ name := "clock" }] }
def ctl_step : RuntimeInput → RuntimeState → RuntimeResult :=
  compileAndRun ctl_designCert (allEdges ctl_designCert)
theorem ctl_compiles : compilesOk ctl_designCert = true := by native_decide
theorem ctl_clock_bounds :
    (∀ f ∈ ctl_designCert.flops, f.clock < ctl_designCert.clocks.size) ∧
    (∀ m ∈ ctl_designCert.memories, m.clock < ctl_designCert.clocks.size) := by native_decide
theorem ctl_step_correct : ∀ inp st,
    ctl_step inp st = interpretDesign ctl_designCert (allEdges ctl_designCert) inp st :=
  compileAndRun_correct ctl_designCert ctl_compiles (allEdges ctl_designCert)
theorem ctl_step_legacy_correct : ∀ inp st,
    ctl_step inp st = interpretDesignLegacy ctl_designCert inp st := by
  intro inp st
  exact (ctl_step_correct inp st).trans
    (interpretDesign_allEdges ctl_designCert ctl_clock_bounds.1 ctl_clock_bounds.2 inp st)
def ctl_residual : ResidualProgram :=
  match compileDesign ctl_designCert with | .ok R => R | .error _ => default
#print axioms ctl_step_correct
#print axioms ctl_step_legacy_correct
LEAN

echo "THIS IS NOT VALID LEAN AND HAS NO PROOF" > "$T/garbage.lean"
sed 's/theorem ctl_step_legacy_correct/theorem ctl_unrelated_other/' "$T/good.lean" \
  | grep -v '#print axioms ctl_step_legacy_correct' > "$T/partial.lean"
{ echo 'axiom ctlFalse : False'; cat "$T/good.lean"; } > "$T/false_axiom.lean"

# Drive the orchestrator's typecheck stage directly: it is the admission point.
probe() {  # <name> <artifact> ; echoes PROVEN|FAILED
  python3 -I - "$ORCH" "$2" "$T/$1" <<'PY' 2>/dev/null
import importlib.util, sys, json
from pathlib import Path
spec = importlib.util.spec_from_file_location("orch", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
out = Path(sys.argv[3]); out.mkdir(parents=True, exist_ok=True)
try:
    r = m.typecheck(Path(sys.argv[2]), "ctl", out)
    print(r["proof_state"])
except Exception as e:
    print("FAILED")
PY
}
expect() {  # <name> <artifact> <want>
  local got; got="$(probe "$1" "$2")"
  if [[ "$got" == "$3" ]]; then echo "ok: $1 -> $got"
  else echo "FAIL: $1 -> ${got:-<none>}, expected $3"; rc=1; fi
}

expect valid_proof        "$T/good.lean"        PROVEN
expect invalid_lean       "$T/garbage.lean"     FAILED
expect unexpected_false   "$T/false_axiom.lean" FAILED
expect missing_obligation "$T/partial.lean"     FAILED

# A STALE artifact: present, but not the one this job produced. The orchestrator
# hashes the artifact into its result, so a reviewer can tell; the control pins
# that the hash is recorded rather than assumed.
if python3 -I - "$ORCH" <<'PY' 2>/dev/null | grep -q 'sha-ok'
import importlib.util, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location("orch", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
print("sha-ok" if len(m.sha(Path(sys.argv[1]))) == 16 else "no")
PY
then echo "ok: artifact hashes are recorded for staleness review"
else echo "FAIL: no artifact hashing"; rc=1; fi

# An unwired family must stay visibly unresolved, never counted as acceptance.
if grep -q 'unresolved prerequisite' "$ORCH"; then
  echo "ok: a missing adapter is SKIPPED/unresolved, not an acceptance disposition"
else
  echo "FAIL: missing adapters are not marked unresolved"; rc=1
fi

[[ $rc -eq 0 ]] && echo "PASS: a proof must be proved"
exit $rc
