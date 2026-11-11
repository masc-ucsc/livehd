/-
`Sym.Closed` -- the closed checks on every simulator route.

A closed check is a `Bool` with no free parameters but the certificate, the spec and the
proofs, so that an emitted theorem file can state it and `native_decide` can evaluate it.
`Sim` names the route that runs the certificate (`sim:` of the drivers):
- `fp sf wf`: the fused memory interpreter specialized to the certificate
  (`MemFused.projectMemFDesignAt`, `MemResidual.of_projF`);
- `ir`: the certificate's dense nodes, walked directly (`irStepper_sound`);
- `vc`: the bindings of the verified compiler (`compileDesign`, `vcStepper_sound`).

`Sim.stepper_sound` gives each route's `MemResidual` witness, so a check stated with
`runInvKOn` has the same conclusion on every route (`runInvKOn_sound`). The refinement-map and
spec-facts checks on every route are in `Check.RefMapRef` (`runRefMapOn`, `runSpecFactsOn`).
Mathlib-free.
-/
import HDLProof.Sym.NativeRoute
import HDLProof.Sym.KInd

open Compiler Projection Projection.MemProjection Projection.HwMem HDLProof.Sym.Native
namespace HDLProof.Sym

/-- The simulator route a closed check runs a certificate on. -/
inductive Sim where
  /-- the fused memory interpreter specialized with fuel `sf`/`wf` -/
  | fp (sf wf : Nat)
  /-- the certificate's dense nodes -/
  | ir
  /-- the verified compiler's bindings -/
  | vc
  deriving Repr, DecidableEq, Inhabited

/-- The symbolic step of `D` on route `r`, when the route accepts `D`. -/
def Sim.stepper : Sim → DesignCert → Option Stepper
  | .fp sf wf, D =>
      match MemFused.projectMemFDesignAt sf wf D with
      | .ok R => some (R : Stepper)
      | .error _ => none
  | .ir, D => some (irStepper D)
  | .vc, D =>
      match compileDesign D with
      | .ok R => some (vcStepper R)
      | .error _ => none

/-- Every route's step is a `MemResidual` of the certificate it was built from. -/
theorem Sim.stepper_sound {r : Sim} {D : DesignCert} {S : Stepper} (h : r.stepper D = some S) :
    MemResidual D S := by
  cases r with
  | fp sf wf =>
      simp only [Sim.stepper] at h
      split at h
      · rename_i R hR
        cases h
        exact MemResidual.of_projF hR
      · cases h
  | ir =>
      simp only [Sim.stepper, Option.some.injEq] at h
      subst h
      exact irStepper_sound D
  | vc =>
      simp only [Sim.stepper] at h
      split at h
      · rename_i R hR
        cases h
        exact vcStepper_sound hR
      · cases h

/-- The closed k-induction check on route `r`. -/
def runInvKOn (r : Sim) (D : DesignCert) (sp : InvSpec) (s0 : StateM) (pfB pfS : ByteArray) : Bool :=
  admitsWB D &&
    match r.stepper D with
    | some R => runInvKOf D R sp s0 pfB pfS
    | none => false

theorem runInvKOn_sound {r : Sim} {D : DesignCert} {sp : InvSpec} {s0 : StateM}
    {pfB pfS : ByteArray} (h : runInvKOn r D sp s0 pfB pfS = true)
    (ins : Nat → RuntimeInput) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (refOut D (StateM.denote s0) ins j) (ins j) sp.asmOut sp.asmIn = true) →
      propBit (refOut D (StateM.denote s0) ins n) sp.prop = true := by
  unfold runInvKOn at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨hA, hr⟩ := h
  split at hr
  · rename_i R hR
    exact runInvKOf_sound_ref (Sim.stepper_sound hR) hA hr ins hins
  · cases hr

end HDLProof.Sym
