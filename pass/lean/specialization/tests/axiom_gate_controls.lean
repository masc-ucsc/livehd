/-
  Controls for `Projection.AxiomGate` (D3: the axiom audit as a FAILING gate).

  Kept OUTSIDE the library on purpose: the negative cases need a `sorry`, an
  `axiom` and a `native_decide`, and the library must contain none of them.
  Every expected message is pinned with `#guard_msgs`, so this file compiles
  cleanly exactly when the gate behaves as specified:

    * no axioms / standard axioms             -> printed, accepted
    * `sorryAx`, a user axiom, a `_native` one -> printed AND an error
    * `#audit_axioms_native` admits the `_native` axiom and nothing else

  Run with `pass/lean/specialization/tests/axiom_gate_controls.sh`.
-/
import LeanSemanticPrimitives.Projection.AxiomGate

namespace AxiomGateControls

theorem ctlNone : (1 : Nat) = 1 := rfl

theorem ctlStd (p : Prop) : p ∨ ¬p := Classical.em p

theorem ctlSorry : (2 : Nat) = 2 := sorry

axiom ctlAx : (3 : Nat) = 4

theorem ctlUser : (3 : Nat) = 4 := ctlAx

theorem ctlNative : (2 : Nat) + 2 = 4 := by native_decide

theorem ctlNativeSorry : (5 : Nat) = 5 ∧ (2 : Nat) + 2 = 4 := ⟨sorry, ctlNative⟩

end AxiomGateControls

open AxiomGateControls

/-- info: 'AxiomGateControls.ctlNone' does not depend on any axioms -/
#guard_msgs in #audit_axioms ctlNone

/-- info: 'AxiomGateControls.ctlStd' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in #audit_axioms ctlStd

/--
info: 'AxiomGateControls.ctlSorry' depends on axioms: [sorryAx]
---
error: axiom gate: 'AxiomGateControls.ctlSorry' depends on non-admitted axioms [sorryAx]
-/
#guard_msgs in #audit_axioms ctlSorry

/--
info: 'AxiomGateControls.ctlUser' depends on axioms: [ctlAx]
---
error: axiom gate: 'AxiomGateControls.ctlUser' depends on non-admitted axioms [AxiomGateControls.ctlAx]
-/
#guard_msgs in #audit_axioms ctlUser

/--
info: 'AxiomGateControls.ctlNative' depends on axioms: [ctlNative._native.native_decide.ax_1_1]
---
error: axiom gate: 'AxiomGateControls.ctlNative' depends on non-admitted axioms [AxiomGateControls.ctlNative._native.native_decide.ax_1_1]
-/
#guard_msgs in #audit_axioms ctlNative

/-- info: 'AxiomGateControls.ctlNative' depends on axioms: [ctlNative._native.native_decide.ax_1_1] -/
#guard_msgs in #audit_axioms_native ctlNative

/--
info: 'AxiomGateControls.ctlNativeSorry' depends on axioms: [sorryAx, ctlNative._native.native_decide.ax_1_1]
---
error: axiom gate: 'AxiomGateControls.ctlNativeSorry' depends on non-admitted axioms [sorryAx]
-/
#guard_msgs in #audit_axioms_native ctlNativeSorry
