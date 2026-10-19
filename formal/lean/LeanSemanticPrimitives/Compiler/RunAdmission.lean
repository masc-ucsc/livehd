/-
# The two run-time admission checks every FP evidence driver applies

Kept tiny and Mathlib-free (it imports only `DesignCert`, `Runtime` and
`ResetPolarity`) so the scalar probes can apply it without pulling in the
memory route.

  * `designError` (decision C4): refuse a certificate whose commit-side and
    read-side reset records name the same RAW input with opposite polarity --
    a DCERT1 exporter defect (`Compiler/ResetPolarity.lean`).
  * `edgesError` (decision C3): every step's clock-edge vector has EXACTLY one
    entry per declared clock.  `Projection/EdgePolicy.lean` proves that, on a
    certificate `certWfError` accepts, this implies the edge premise of all
    three routes (scalar `≤`, Direct exact, memory per element).

Which drivers apply them: the ones that produce coverage evidence --
`cert_runner_any` (`CertLoadAny.runOneAny`), `total_probe`, `gen_probe`,
`gen_probe_v2`, `gen_readiness_v2`, `dcert2_memroute`.  The measurement probes
(`proto_probe`, `s2`, `s2a`, `s3`) never produce coverage credit and include a
reset-polarity CENSUS mode that must read conflicted files, so they do not.
-/

import LeanSemanticPrimitives.Compiler.Runtime
import LeanSemanticPrimitives.Compiler.ResetPolarity

namespace Compiler
namespace RunAdmission

/-- The one edge-vector rule: exactly one entry per declared clock. -/
def edgesExactOK (D : DesignCert) (e : ClockEdges) : Bool :=
  e.size == D.clocks.size

/-- Why this step's edge vector is refused, or `none`. -/
def edgesError (D : DesignCert) (e : ClockEdges) : Option String :=
  if edgesExactOK D e then none
  else some s!"edge vector has {e.size} entries but the design declares \
{D.clocks.size} clock(s); an exact match is required (a short vector would \
silently freeze a clock domain)"

/-- Why this design is refused before any run, or `none`. -/
def designError (D : DesignCert) : Option String :=
  ResetPolarity.polarityError D

/-- The exit code every FP evidence driver uses for a run-admission refusal
(either check above).  Unused by any driver before these checks existed. -/
def refusedExit : UInt32 := 9

/-- Apply `designError`: print the reason and return `some refusedExit`, or
`none` to proceed. -/
def refuseDesign? (D : DesignCert) : IO (Option UInt32) := do
  match designError D with
  | some why =>
      IO.eprintln s!"  REFUSED (run admission): {why}"
      IO.println "  RESULT: RUN-ADMISSION REFUSED"
      pure (some refusedExit)
  | none => pure none

/-- Apply `edgesError` to one step's edge vector, likewise. -/
def refuseEdges? (D : DesignCert) (k : Nat) (e : ClockEdges) : IO (Option UInt32) := do
  match edgesError D e with
  | some why =>
      IO.eprintln s!"  REFUSED (run admission) at step {k}: {why}"
      IO.println "  RESULT: RUN-ADMISSION REFUSED"
      pure (some refusedExit)
  | none => pure none

#guard edgesExactOK { sources := #[], nodes := #[], outputs := #[], flops := #[],
                      memories := #[], clocks := #[⟨"a"⟩, ⟨"b"⟩] } #[true, false]
#guard !edgesExactOK { sources := #[], nodes := #[], outputs := #[], flops := #[],
                       memories := #[], clocks := #[⟨"a"⟩, ⟨"b"⟩] } #[true]

end RunAdmission
end Compiler
