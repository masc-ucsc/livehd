/-
# vc's step, compiled after its operators' fast forms

`denoteResidual` (the vc route's step, `ResidualSemantics`) is compiled in its own module,
before `FastResidual`, so two of its operators keep their slow forms: `rshlV` computes
`bv_uint a * 2 ^ k` with an `Int` power, and `rnotV` loops over every bit. A `@[csimp]`
lemma only reaches code compiled after it. And every cycle, `denoteResidual` turns the
binding array into a list before it walks it.

This module changes neither. It gives the step a second, word-for-word spelling compiled
after `FastResidual`, and the binding walk as one fold over the array, with `@[csimp]`
theorems that each is EQUAL to the original. Compiled code that imports this module runs
these; every definition and every proof keeps meaning exactly what it did.

`CompileDesignDefs` imports it, so every vc user (`cert_trace route:vc`, the proof tools'
`sim:vc`) does. Mathlib-free.

The memory operators and the next memories take value forms (`rmemWriteVal`, `memNextVal`,
`FastBV.memsOfVals`): `rmemWriteV` tests its enable inside the function it returns, so every
step added a layer to every written memory whether it wrote or not, and the next images were
closures over the step's slots and the previous state (`Compiler/MemStepFast` says why). A
disabled write now returns the image it was given, an enabled one adds one layer with its
address and word computed once, and the state stores each image's function itself.
-/
import LeanSemanticPrimitives.Compiler.FastResidual

namespace Compiler

open Residual

/-! ## Memory operators as values -/

/-- `rmemReadV` on a `CertVal` image, without building `asMem`'s closure. -/
def rmemReadVal (w : Nat) (m : CertVal) (a en : BV) : BV :=
  match m with
  | .mem f => rmemReadV w f a en
  | .bv _ => rmemReadV w m.asMem a en

theorem rmemReadVal_eq (w : Nat) (m : CertVal) (a en : BV) :
    rmemReadVal w m a en = rmemReadV w m.asMem a en := by
  cases m <;> rfl

/-- `rmemWriteV` as a value: the given image when `en` is 0. -/
def rmemWriteVal (m : CertVal) (a d en : BV) : CertVal :=
  match m with
  | .mem f => if bv_uint en = 0 then .mem f else .mem (FastBV.writeAt f (bv_uint a) d)
  | .bv _ => .mem (rmemWriteV m.asMem a d en)

theorem rmemWriteVal_eq (m : CertVal) (a d en : BV) :
    rmemWriteVal m a d en = .mem (rmemWriteV m.asMem a d en) := by
  cases m with
  | bv _ => rfl
  | mem f =>
    simp only [rmemWriteVal, CertVal.asMem]
    by_cases h : bv_uint en = 0 <;> simp only [h, ↓reduceIte, CertVal.mem.injEq] <;> funext x <;>
      simp [rmemWriteV, FastBV.writeAt, h]

/-- `rmemWriteBEV` as a value; the merged word is computed once, when written. -/
def rmemWriteBEVal (w : Nat) (m : CertVal) (a d bev : BV) (byteW : Nat) : CertVal :=
  match m with
  | .mem f =>
    if bv_uint bev = 0 then .mem f
    else .mem (FastBV.writeAt f (bv_uint a) (rMaskedUpdateV w (f (bv_uint a)) d bev byteW))
  | .bv _ => .mem (rmemWriteBEV w m.asMem a d bev byteW)

theorem rmemWriteBEVal_eq (w : Nat) (m : CertVal) (a d bev : BV) (byteW : Nat) :
    rmemWriteBEVal w m a d bev byteW = .mem (rmemWriteBEV w m.asMem a d bev byteW) := by
  cases m with
  | bv _ => rfl
  | mem f =>
    simp only [rmemWriteBEVal, CertVal.asMem]
    by_cases h : bv_uint bev = 0 <;> simp only [h, ↓reduceIte, CertVal.mem.injEq] <;> funext x <;>
      simp [rmemWriteBEV, FastBV.writeAt, h]

/-- `rmemLoadV` as a value: the given image when `en` is 0. -/
def rmemLoadVal (w n : Nat) (m : CertVal) (bus en : BV) : CertVal :=
  match m with
  | .mem f => if bv_uint en = 0 then .mem f else .mem (rmemLoadV w n f bus en)
  | .bv _ => .mem (rmemLoadV w n m.asMem bus en)

theorem rmemLoadVal_eq (w n : Nat) (m : CertVal) (bus en : BV) :
    rmemLoadVal w n m bus en = .mem (rmemLoadV w n m.asMem bus en) := by
  cases m with
  | bv _ => rfl
  | mem f =>
    simp only [rmemLoadVal, CertVal.asMem]
    by_cases h : bv_uint en = 0 <;> simp only [h, ↓reduceIte, CertVal.mem.injEq] <;> funext x <;>
      simp [rmemLoadV, h]

/-- `denoteExpr`, word for word. Compiled here, its `rshlV` and `rnotV` calls take their
fast forms (`FastBV.rshlV_eq_fast`, `FastBV.rnotV_eq_fast`). -/
def denoteExprFast (env : SlotEnv) : ResidualExpr → CertVal
  | .rsum w nAdd args     => .bv (rsumV w nAdd (refBVs env args))
  | .rmult w args         => .bv (rmultV w (refBVs env args))
  | .rand w args          => .bv (randV w (refBVs env args))
  | .rorBits w args       => .bv (rorBitsV w (refBVs env args))
  | .rxor w args          => .bv (rxorV w (refBVs env args))
  | .rredOr w args        => .bv (rredOrV w (refBVs env args))
  | .req w args           => .bv (reqV w (refBVs env args))
  | .rshl w args          => .bv (rshlV w (refBVs env args))
  | .rmuxN w args         => .bv (rmuxNV w (refBVs env args))
  | .rnot w a             => .bv (rnotV w (refBV env a))
  | .rult w a b           => .bv (rultV w (refBV env a) (refBV env b))
  | .rugt w a b           => .bv (rugtV w (refBV env a) (refBV env b))
  | .rslt w a b           => .bv (rsltV w (refBV env a) (refBV env b))
  | .rsgt w a b           => .bv (rsgtV w (refBV env a) (refBV env b))
  | .rsra w a b           => .bv (rsraV w (refBV env a) (refBV env b))
  | .rsext w a amt        => .bv (rsextV w (refBV env a) (refBV env amt))
  | .rgetMask w a m       => .bv (rgetMaskV w (refBV env a) (refBV env m))
  | .rudiv w a b          => .bv (rudivV w (refBV env a) (refBV env b))
  | .rsdiv w a b          => .bv (rsdivV w (refBV env a) (refBV env b))
  | .rsetMask w a m v     => .bv (rsetMaskV w (refBV env a) (refBV env m) (refBV env v))
  | .rmux w sel fv tv     => .bv (rmuxV w (refBV env sel) (refBV env fv) (refBV env tv))
  | .rmemRead w m a en    => .bv (rmemReadVal w (denoteRef env m) (refBV env a) (refBV env en))
  | .rmemWrite m a d en   => rmemWriteVal (denoteRef env m) (refBV env a) (refBV env d)
                                          (refBV env en)
  | .rmemWriteBE w bw m a d be =>
      rmemWriteBEVal w (denoteRef env m) (refBV env a) (refBV env d) (refBV env be) bw
  | .rmemLoad w n m bus en =>
      rmemLoadVal w n (denoteRef env m) (refBV env bus) (refBV env en)
  | .rmemToBus w n m      => .bv (rmemToBusV w n (refMem env m))

@[csimp] theorem denoteExpr_eq_fast : @denoteExpr = @denoteExprFast := by
  funext env e
  cases e <;> first
    | rfl
    | (simp only [denoteExprFast, rmemReadVal_eq]; rfl)
    | (simp only [denoteExprFast, rmemWriteVal_eq]; rfl)
    | (simp only [denoteExprFast, rmemWriteBEVal_eq]; rfl)
    | (simp only [denoteExprFast, rmemLoadVal_eq]; rfl)

/-- `runBindings` is a left fold that pushes each binding's value. -/
theorem runBindings_eq_foldl : ∀ (bs : List ResidualBinding) (env : SlotEnv),
    runBindings bs env = bs.foldl (fun env b => env.push (denoteExpr env b.rhs)) env
  | [], _ => rfl
  | b :: bs, env => by
    simp only [runBindings, List.foldl_cons]
    exact runBindings_eq_foldl bs _

/-! ## The next memories as values -/

/-- `memHeld` as a value. -/
def memHeldVal (env : SlotEnv) (s : RuntimeState) (idx : Nat) (m : ResidualMemoryUpdate) :
    CertVal :=
  match m.asyncImg with
  | some a => denoteRef env a
  | none   => .mem (s.mems[idx]?.getD (fun _ => mk_bv 0 0))

/-- `memNext` as a value: the slot (or held image) whose function is the next image. -/
def memNextVal (env : SlotEnv) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (m : ResidualMemoryUpdate) : CertVal :=
  if m.commits.isEmpty then
    if fires e m.clock then denoteRef env m.nextImg
    else memHeldVal env s idx m
  else if m.toDesc.allFire e then denoteRef env m.nextImg
  else
    match m.commits.find? (m.toDesc.firedIs e) with
    | some c => denoteRef env c.nextImg
    | none   => memHeldVal env s idx m

theorem memHeldVal_asMem (env : SlotEnv) (s : RuntimeState) (idx : Nat) (m : ResidualMemoryUpdate) :
    (memHeldVal env s idx m).asMem = memHeld env s idx m := by
  unfold memHeldVal memHeld
  cases m.asyncImg <;> rfl

theorem memNextVal_asMem (env : SlotEnv) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (m : ResidualMemoryUpdate) : (memNextVal env e s idx m).asMem = memNext env e s idx m := by
  unfold memNextVal memNext
  split
  · split
    · rfl
    · exact memHeldVal_asMem env s idx m
  · split
    · rfl
    · cases m.commits.find? (m.toDesc.firedIs e) with
      | none => exact memHeldVal_asMem env s idx m
      | some c => rfl

theorem residualMems_eq (env : SlotEnv) (e : ClockEdges) (s : RuntimeState)
    (ms : Array ResidualMemoryUpdate) :
    FastBV.memsOfVals (ms.mapIdx fun idx m => memNextVal env e s idx m) =
      ms.mapIdx fun idx m => memNext env e s idx m := by
  rw [FastBV.memsOfVals_mapIdx]
  congr 1
  funext idx m
  exact memNextVal_asMem env e s idx m

/-- `denoteResidual`, with the binding walk as one fold over the array (no list built each
cycle) and the next memories stored by `FastBV.memsOfVals`. Compiled after
`denoteExpr_eq_fast`, the fold calls `denoteExprFast`. -/
def denoteResidualFast (R : ResidualProgram) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : RuntimeResult :=
  let env := R.bindings.foldl (fun env b => env.push (denoteExpr env b.rhs))
    (sourceEnvArr R.sources i s)
  { outputs   := R.outputs.map fun o => bv_resize o.width (refBV env o.slot)
    nextState :=
      { flops := R.flopUpdates.mapIdx fun idx f => flopNext env e s idx f
        mems  := FastBV.memsOfVals (R.memoryUpdates.mapIdx fun idx m => memNextVal env e s idx m) } }

@[csimp] theorem denoteResidual_eq_fast : @denoteResidual = @denoteResidualFast := by
  funext R e i s
  simp only [denoteResidual, denoteResidualFast, runBindings_eq_foldl, Array.foldl_toList,
    residualMems_eq]

end Compiler
