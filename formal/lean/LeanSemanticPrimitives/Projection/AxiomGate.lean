/-
  # The axiom gate

  `#audit_axioms X` prints exactly what `#print axioms X` prints -- the same
  message text, from the same `collectAxioms` -- and then FAILS the build if
  `X` rests on any axiom outside the standard three:

      propext, Classical.choice, Quot.sound

  `#print axioms` alone only reports. A `sorry` anywhere in a dependency shows
  up as `sorryAx`, and a `native_decide` as an axiom whose name has a `_native`
  component (Lean 4.31 mints one per call; the older `Lean.ofReduceBool` is
  deprecated). Either would be printed and the build would still succeed, so a
  printed audit is only as good as whoever reads it. This command turns the
  same output into a gate.

  `#audit_axioms_native X` additionally admits those native-evaluation axioms
  (`_native.*`, `Lean.ofReduceBool`, `Lean.trustCompiler`). It exists for the
  NAMED trusted-native list only -- theorems whose statement is about one
  concrete design and whose evidence is a compiled run -- and must never be
  used for a theorem stated over all designs.

  The negative controls (a `sorry`, a `native_decide`, an explicit axiom) live
  in `pass/lean/specialization/tests/axiom_gate_controls.lean`, outside the library, so the
  library itself declares no axiom and uses no `sorry`.
-/

import Lean.Elab.Command
import Lean.Util.CollectAxioms

namespace Projection.AxiomGate

open Lean Elab Command

/-- The only axioms a theorem stated over all designs may rest on. -/
def stdAxioms : Array Name := #[``propext, ``Classical.choice, ``Quot.sound]

/-- Axioms that record a compiled evaluation rather than a kernel check. -/
def isNativeAxiom (n : Name) : Bool :=
  n == ``Lean.ofReduceBool || n == ``Lean.trustCompiler ||
    n.components.any (· == `_native)

/-- Print as `#print axioms` does, then refuse anything not admitted. -/
def gate (id : Syntax) (native : Bool) : CommandElabM Unit := do
  let cs ← liftCoreM <| realizeGlobalConstWithInfos id
  for c in cs do
    let axs ← collectAxioms c
    if axs.isEmpty then
      logInfo m!"'{c}' does not depend on any axioms"
    else
      logInfo m!"'{c}' depends on axioms: {axs.qsort Name.lt |>.map MessageData.ofConstName |>.toList}"
    let bad := axs.filter fun a => !(stdAxioms.contains a || (native && isNativeAxiom a))
    unless bad.isEmpty do
      logError m!"axiom gate: '{c}' depends on non-admitted axioms {bad.qsort Name.lt |>.toList}"

/-- Print the axioms of a theorem and fail unless they are the standard three. -/
syntax (name := auditAxioms) "#audit_axioms " ident : command

/-- As `#audit_axioms`, also admitting native-evaluation axioms. Trusted-native
list only. -/
syntax (name := auditAxiomsNative) "#audit_axioms_native " ident : command

elab_rules : command
  | `(#audit_axioms $id:ident) => withRef id <| gate id false
  | `(#audit_axioms_native $id:ident) => withRef id <| gate id true

end Projection.AxiomGate
