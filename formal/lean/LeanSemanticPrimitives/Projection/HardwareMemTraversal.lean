/-
# The typed mixed slot invariant, and general source traversal

## What a slot holds

A slot is a bit vector OR a memory image -- never a coercion of one into the
other.  `SlotV` is that choice made explicit, with the two projections that
matter kept apart:

* `SlotV.enc`    -- what the OBJECT holds: `encBV b` or `MemImg.encMemImg m`;
* `SlotV.denote` -- what the REFERENCE holds: `CertVal.bv b` or
  `CertVal.mem (MemImg.denote m)`.

`SrcRel_enc` proves these two agree for every slot, so `SlotV` IS the typed
relation `SrcRel` of §63.31, in a form the traversal can carry as a list.

## The kinds are the CHECKER's, not a premise

`SlotV.isMem` is tied to the real `DirectCheck` predicates:
`srcSlotV_isMem` proves it equals `(Direct.sourceKind sd).isMem` for every
source form.  `DesignSemWF` -- what `checkDesign` establishes -- already carries
`nodeKinds`, `outputKinds`, `flopKinds` and `memKinds` in terms of
`slotIsMem`/`depIsMem`, so the kind discipline is taken FROM the checker rather
than added as a hypothesis.

## Scope of this module

Source traversal (`mkSourcesM`) in full generality: arbitrary source list,
arbitrary `StateM`, arbitrary input, source ORDER preserved, accumulator
reversal as `List.reverseAux` (the object's own recursion, not a separate
`reverse` step), under the ORIGINAL bounds premises only -- `SourceSupported` is
NOT required, which is the point of the memory route.  Node traversal follows in
the next module and reuses `SlotV` unchanged.
-/
import LeanSemanticPrimitives.Projection.HardwareMemSlots
import LeanSemanticPrimitives.Compiler.DirectCheck

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. The typed slot value -/

/-- What a slot can hold.  A memory slot carries a REPRESENTABLE image, not an
arbitrary `Int → BV`: that is what makes the object able to hold it at all. -/
inductive SlotV where
  | bv  (b : BV)
  | img (m : MemImg)
deriving Inhabited

/-- What the object holds. -/
def SlotV.enc : SlotV → Val
  | .bv b  => encBV b
  | .img m => MemImg.encMemImg m

/-- What the reference holds. -/
def SlotV.denote : SlotV → CertVal
  | .bv b  => .bv b
  | .img m => .mem (MemImg.denote m)

/-- The kind, in `DirectCheck`'s own vocabulary. -/
def SlotV.isMem : SlotV → Bool
  | .bv _  => false
  | .img _ => true

/-- **`SlotV` is the typed relation of §63.31.**  Every slot's object value is
related to its reference value, with the kind carried. -/
theorem SrcRel_enc (v : SlotV) : SrcRel v.enc v.denote := by
  cases v with
  | bv b  => rfl
  | img m => exact ⟨m, rfl, rfl⟩

@[simp] theorem SlotV.isMem_bv (b : BV) : (SlotV.bv b).isMem = false := rfl
@[simp] theorem SlotV.isMem_img (m : MemImg) : (SlotV.img m).isMem = true := rfl

/-- A slot that the checker says is NOT a memory holds a bit vector, and that
bit vector is the reference's `asBV`. -/
theorem SlotV.bv_of_not_isMem {v : SlotV} (h : v.isMem = false) :
    v = .bv v.denote.asBV := by
  cases v with
  | bv b  => rfl
  | img m => simp [SlotV.isMem] at h

/-! ## 2. The source slots

`srcSlotV` is the typed source value: an image for the two memory forms, a bit
vector for the four scalar ones.  `srcSlotV_denote` proves it denotes
`Compiler.sourceValue` exactly, and `srcSlotV_isMem` proves its kind is
`Direct.sourceKind`'s. -/

def srcSlotV (i : RuntimeInput) (st : StateM) : SourceDesc → SlotV
  | .memImg idx _ _   => .img (Hardware.srcMemImg st idx)
  | .memConst _ dw ct => .img (MemImg.ofMemConst dw ct)
  | .input idx w      => .bv (sourceValue i (StateM.denote st) (.input idx w)).asBV
  | .const w v        => .bv (sourceValue i (StateM.denote st) (.const w v)).asBV
  | .flopQ idx w      => .bv (sourceValue i (StateM.denote st) (.flopQ idx w)).asBV
  | .flopQAsync idx w ri rv al =>
      .bv (sourceValue i (StateM.denote st) (.flopQAsync idx w ri rv al)).asBV

/-- **The typed source value denotes the reference source value**, on all six
forms, with no supportedness premise. -/
theorem srcSlotV_denote (i : RuntimeInput) (st : StateM) (sd : SourceDesc) :
    (srcSlotV i st sd).denote = sourceValue i (StateM.denote st) sd := by
  cases sd with
  | input idx w      => rfl
  | const w v        => rfl
  | flopQ idx w      => rfl
  | flopQAsync _ _ _ _ _ => rfl
  | memImg idx aw dw =>
      simpa [srcSlotV, SlotV.denote, sourceValue]
        using congrArg CertVal.mem (Hardware.srcMemImg_denote st idx)
  | memConst aw dw ct =>
      refine congrArg CertVal.mem (funext fun x => ?_)
      simpa using Hardware.ofMemConst_denote dw ct x

/-- **The kind is the checker's kind**, on all six forms. -/
theorem srcSlotV_isMem (i : RuntimeInput) (st : StateM) (sd : SourceDesc) :
    (srcSlotV i st sd).isMem = (Direct.sourceKind sd).isMem := by
  cases sd <;> rfl

/-! ## 3. `srcValM` produces exactly the typed source value -/

theorem srcValM_slotV {sd : SourceDesc} {i : RuntimeInput} {st : StateM}
    {σ : SEnv} {esd einp efq ems : SExp}
    (hin : SourceInputBound sd i.size) (hst : SourceFlopBound sd st.flops.size)
    (h1 : SEval hwMemS σ esd (encSource sd))
    (h2 : SEval hwMemS σ einp (encInput i))
    (h3 : SEval hwMemS σ efq (encBVs st.flops))
    (h4 : SEval hwMemS σ ems (encListG MemImg.encMemImg st.mems.toList)) :
    SEval hwMemS σ (.call "srcValM" [esd, einp, efq, ems]) (srcSlotV i st sd).enc := by
  cases sd with
  | input idx w =>
      exact srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4
  | const w v =>
      exact srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4
  | flopQ idx w =>
      exact srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4
  | flopQAsync idx w ri rv al =>
      exact srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4
  | memImg idx aw dw =>
      have h := srcValM_memImg st.mems.toList idx aw dw h1 h2 h3 h4
      have hx : st.mems.toList[idx]?.getD Hardware.absentImg
          = Hardware.srcMemImg st idx := by simp [Hardware.srcMemImg]
      rw [hx] at h
      exact h
  | memConst aw dw ct =>
      exact srcValM_memConst aw dw ct h1 h2 h3 h4

/-! ## 4. General `mkSourcesM` preservation

Source ORDER is preserved and the accumulator reversal is `List.reverseAux` --
the object's own recursion, not a separate `reverse` step proved equal to it.
`SourceSupported` is NOT a premise; only the original input and flop bounds
are, and they are demanded per source exactly as the memory-free route demands
them. -/

theorem mkSourcesM_agree {i : RuntimeInput} {st : StateM} :
    ∀ (srcs : List SourceDesc) (acc : List Val) (σ : SEnv) (es ei ef ems ea : SExp),
      (∀ sd ∈ srcs, SourceInputBound sd i.size) →
      (∀ sd ∈ srcs, SourceFlopBound sd st.flops.size) →
      SEval hwMemS σ es (encListG encSource srcs) →
      SEval hwMemS σ ei (encInput i) →
      SEval hwMemS σ ef (encBVs st.flops) →
      SEval hwMemS σ ems (encListG MemImg.encMemImg st.mems.toList) →
      SEval hwMemS σ ea (encListG (fun v => v) acc) →
      SEval hwMemS σ (.call "mkSourcesM" [es, ei, ef, ems, ea])
        (encListG (fun v => v)
          (List.reverseAux (srcs.map (fun sd => (srcSlotV i st sd).enc)) acc))
  | [],         _, _, _, _, _, _, _, _,   _,   h1, h2, h3, h4, h5 => by
      refine SEval_call5 h1 h2 h3 h4 h5 rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | sd :: rest, _, _, _, _, _, _, _, hin, hst, h1, h2, h3, h4, h5 => by
      refine SEval_call5 h1 h2 h3 h4 h5 rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact mkSourcesM_agree rest _ _ _ _ _ _ _
        (fun x hx => hin x (List.mem_cons_of_mem _ hx))
        (fun x hx => hst x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl)
        (SEval_consP
          (srcValM_slotV (hin sd List.mem_cons_self) (hst sd List.mem_cons_self)
            (SEval_hd (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl))
          (.ref rfl))

/-- The source environment the object ends up holding IS `objEnv` of the typed
source slots, in slot order. -/
theorem mkSourcesM_objEnv {i : RuntimeInput} {st : StateM} (srcs : List SourceDesc)
    {σ : SEnv} {es ei ef ems ea : SExp}
    (hin : ∀ sd ∈ srcs, SourceInputBound sd i.size)
    (hst : ∀ sd ∈ srcs, SourceFlopBound sd st.flops.size)
    (h1 : SEval hwMemS σ es (encListG encSource srcs))
    (h2 : SEval hwMemS σ ei (encInput i))
    (h3 : SEval hwMemS σ ef (encBVs st.flops))
    (h4 : SEval hwMemS σ ems (encListG MemImg.encMemImg st.mems.toList))
    (h5 : SEval hwMemS σ ea (encListG (fun v => v) [])) :
    SEval hwMemS σ (.call "mkSourcesM" [es, ei, ef, ems, ea])
      (objEnv (srcs.map (fun sd => (srcSlotV i st sd).enc))) := by
  have h := mkSourcesM_agree srcs [] σ es ei ef ems ea hin hst h1 h2 h3 h4 h5
  simpa [objEnv, List.reverseAux_eq] using h

/-! ## 5. Every source slot is related to the reference, by kind

The two facts the node traversal consumes: the slot list is `SlotV.enc` of a
typed list, and that typed list denotes `Compiler.sourceEnvArr`. -/

theorem srcSlots_denote (i : RuntimeInput) (st : StateM) (srcs : List SourceDesc)
    (k : Nat) (h : k < srcs.length) :
    (srcSlotV i st (srcs[k]'h)).denote = sourceValue i (StateM.denote st) (srcs[k]'h) :=
  srcSlotV_denote i st _

theorem srcSlots_isMem (i : RuntimeInput) (st : StateM) (srcs : List SourceDesc)
    (k : Nat) (h : k < srcs.length) :
    (srcSlotV i st (srcs[k]'h)).isMem = (Direct.sourceKind (srcs[k]'h)).isMem :=
  srcSlotV_isMem i st _

/-- …and that is exactly `Direct.slotKind` on a source index. -/
theorem srcSlots_slotKind {D : DesignCert} (i : RuntimeInput) (st : StateM)
    {k : Nat} (h : k < D.sources.size) :
    Direct.slotIsMem D k
      = some (srcSlotV i st (D.sources.toList[k]'(by simpa using h))).isMem := by
  have hs : D.sources[k]? = some (D.sources.toList[k]'(by simpa using h)) := by
    rw [Array.getElem?_eq_getElem h]; simp
  simp [Direct.slotIsMem, Direct.slotKind, hs, srcSlotV_isMem]

#print axioms SrcRel_enc
#print axioms srcSlotV_denote
#print axioms srcSlotV_isMem
#print axioms srcValM_slotV
#print axioms mkSourcesM_agree
#print axioms mkSourcesM_objEnv
#print axioms srcSlots_slotKind

end HwMem
end Projection
