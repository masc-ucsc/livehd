/-
# The memory step without a closure over the step

`srcMemNext rho e s idx m` returns the next image of memory `idx`, a function `Int → BV`.
Lean compiles a definition whose result is a function with the function's argument added to
its parameters, so the call `srcMemNext rho e s idx m` in a step is a closure over `rho` (the
step's whole slot environment) and `s` (the previous state, whose images are such closures
in turn). A run then keeps every step's environment alive, and a memory read goes back
through every earlier step.

`srcMemNextVal` selects the same image as a `CertVal` (data), and `FastBV.memsOfVals` stores
its function itself. `srcMemNextVal_asMem` proves the selection is `srcMemNext`'s, and
`interpretDesign_eq_fast` gives `interpretDesign` (the reference, `route:ref`) the form that
stores the images this way; `DirectSemantics` (`ir`) and `ResidualFast` (`vc`) do the same for
their steps. No definition changes. Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.DesignSemantics

namespace Compiler

open FastBV

/-- `srcMemHeld` as a value. -/
def srcMemHeldVal (rho : Nat → CertVal) (s : RuntimeState) (idx : Nat) (m : MemoryDesc) :
    CertVal :=
  match m.asyncImg with
  | some a => rho a
  | none   => .mem (s.mems[idx]?.getD (fun _ => mk_bv 0 0))

/-- `srcMemNext` as a value: the slot (or held image) whose function is the next image. -/
def srcMemNextVal (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (m : MemoryDesc) : CertVal :=
  if m.commits.isEmpty then
    if fires e m.clock then rho m.nextImg
    else srcMemHeldVal rho s idx m
  else if m.allFire e then rho m.nextImg
  else
    match m.commits.find? (m.firedIs e) with
    | some c => rho c.nextImg
    | none   => srcMemHeldVal rho s idx m

theorem srcMemHeldVal_asMem (rho : Nat → CertVal) (s : RuntimeState) (idx : Nat) (m : MemoryDesc) :
    (srcMemHeldVal rho s idx m).asMem = srcMemHeld rho s idx m := by
  unfold srcMemHeldVal srcMemHeld
  cases m.asyncImg <;> rfl

theorem srcMemNextVal_asMem (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (m : MemoryDesc) : (srcMemNextVal rho e s idx m).asMem = srcMemNext rho e s idx m := by
  unfold srcMemNextVal srcMemNext
  split
  · split
    · rfl
    · exact srcMemHeldVal_asMem rho s idx m
  · split
    · rfl
    · cases m.commits.find? (m.firedIs e) with
      | none => exact srcMemHeldVal_asMem rho s idx m
      | some c => rfl

/-- A step's next memories, each image stored as the function its slot holds. -/
theorem srcMems_eq (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (ms : Array MemoryDesc) :
    memsOfVals (ms.mapIdx fun idx m => srcMemNextVal rho e s idx m) =
      ms.mapIdx fun idx m => srcMemNext rho e s idx m := by
  rw [memsOfVals_mapIdx]
  congr 1
  funext idx m
  exact srcMemNextVal_asMem rho e s idx m

/-- `interpretDesign`, with the next memories stored by `memsOfVals`. -/
def interpretDesignFast (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    RuntimeResult :=
  let G   := D.toGraphCert
  let rho := evalGraphG G.topo G (srcEnv D i s)
  { outputs   := D.outputs.map fun o => bv_resize o.width (rho o.slot).asBV
    nextState :=
      { flops := D.flops.mapIdx fun idx f => srcFlopNext rho e s idx f
        mems  := memsOfVals (D.memories.mapIdx fun idx m => srcMemNextVal rho e s idx m) } }

@[csimp] theorem interpretDesign_eq_fast : @interpretDesign = @interpretDesignFast := by
  funext D e i s
  simp only [interpretDesign, interpretDesignFast, srcMems_eq]

end Compiler
