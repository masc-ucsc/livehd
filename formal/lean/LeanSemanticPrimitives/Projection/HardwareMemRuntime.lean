/-
# The executable runtime state / input / edge policy

`admitsB` (HardwareMemAdmit) is a STATIC check: it looks at the design only.
`admits_mainM_resolved` (HardwareMemSlotTy) still carries six RUNTIME premises —
the memory shape of the incoming state, how far the sources read into the input
and flop vectors, the flop-array size, and the two clock-ordinal bounds against
the edge vector.  This module makes those executable, and strengthens the memory
shape from a LENGTH to a word-level statement.

## What the shape says now

`StateShapeOK` says only `words.length = 2 ^ aw`.  That is what the address fit
needs, and nothing more, so it was all that was proved.  It is not what a real
state must satisfy: an image whose words are 7 bits wide against a `dw = 32`
memory has the right length and the wrong contents.  `MemShapeOK` adds the two
missing facts — EVERY word is `dw` wide, and so is the default, which is the
word every out-of-range read returns — and `MemShapeOK.toFullTable` shows it
still implies the length statement every existing proof consumes.

## What is NOT claimed here

Returned-state preservation at the WORD level is the next increment: it needs
the shape of a `tbl` slot across the dense order, which — unlike the scalar
width lift of §63.43 — is a genuine recurrence, because a write's result shape
depends on the shape of the image it wrote into.  `admits_run` below therefore
still concludes the LENGTH-level `StateShapeOK` for the next state, exactly as
`admits_mainM_resolved` does.  `SourceSupported`, `OpSupported` and `memFree`
are untouched and the public route still refuses memory-bearing designs.
-/
import LeanSemanticPrimitives.Projection.HardwareMemSlotTy

namespace Projection
namespace HwMem
open Surface Compiler Projection.Hw

/-! ## 1. The memory shape, at word level -/

/-- A declared memory's image: the full table, every word at the declared data
width, and the default — the word every out-of-range read returns — at that
width too. -/
def MemShapeOK (md : MemoryDesc) (m : MemImg) : Prop :=
  m.words.length = 2 ^ md.aw
  ∧ (∀ w ∈ m.words, w.width = md.dw)
  ∧ m.dflt.width = md.dw

theorem MemShapeOK.toFullTable {md : MemoryDesc} {m : MemImg}
    (h : MemShapeOK md m) : MemImg.FullTable md.aw m := h.1

def memImgOKB (md : MemoryDesc) (m : MemImg) : Bool :=
  (m.words.length == 2 ^ md.aw)
  && m.words.all (fun w => w.width == md.dw)
  && (m.dflt.width == md.dw)

theorem memImgOKB_sound {md : MemoryDesc} {m : MemImg}
    (h : memImgOKB md m = true) : MemShapeOK md m := by
  unfold memImgOKB at h
  simp only [Bool.and_eq_true, beq_iff_eq, List.all_eq_true] at h
  exact ⟨h.1.1, fun w hw => by simpa using h.1.2 w hw, h.2⟩

/-! ## 2. The policy, as four executable checks -/

/-- The incoming state: every DECLARED memory has an image of the declared
shape, and the flop vector is at least as long as the design's flop array. -/
def stateOKB (D : DesignCert) (st : StateM) : Bool :=
  decide (D.flops.size ≤ st.flops.size)
  && (List.range D.memories.size).all (fun idx =>
       match D.memories[idx]?, st.mems[idx]? with
       | some md, some m => memImgOKB md m
       | _,       _      => false)

/-- How far the sources read into the input vector. -/
def inputOKB (D : DesignCert) (n : Nat) : Bool :=
  D.sources.toList.all (fun sd => decide (SourceInputBound sd n))

/-- …and into the flop vector. -/
def flopBoundOKB (D : DesignCert) (n : Nat) : Bool :=
  D.sources.toList.all (fun sd => decide (SourceFlopBound sd n))

/-- Every flop and every memory commits on an ordinal the edge vector actually
carries, so `firesAt` is not reading past it. -/
def edgesOKB (D : DesignCert) (e : ClockEdges) : Bool :=
  D.flops.toList.all (fun f => decide (f.clock < e.size))
  && D.memories.toList.all (fun md => decide (md.clock < e.size))

def runtimeOKB (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (e : ClockEdges) : Bool :=
  stateOKB D st && inputOKB D i.size && flopBoundOKB D st.flops.size
  && edgesOKB D e

/-! ## 3. Each check discharges its premise -/

private theorem mem_lt_of_getElem? {α : Type _} {a : Array α} {idx : Nat} {v : α}
    (h : a[idx]? = some v) : idx < a.size := by
  rcases Nat.lt_or_ge idx a.size with hlt | hge
  · exact hlt
  · rw [Array.getElem?_eq_none hge] at h; exact absurd h (by simp)

theorem stateOKB_shapeW {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    ∀ (idx : Nat) (md : MemoryDesc), D.memories[idx]? = some md →
      ∃ m, st.mems[idx]? = some m ∧ MemShapeOK md m := by
  unfold stateOKB at h
  simp only [Bool.and_eq_true, List.all_eq_true] at h
  intro idx md hmd
  have hlt : idx < D.memories.size := mem_lt_of_getElem? hmd
  have hr : idx ∈ List.range D.memories.size := List.mem_range.mpr hlt
  have := h.2 idx hr
  rw [hmd] at this
  cases hm : st.mems[idx]? with
  | none   => rw [hm] at this; simp at this
  | some m =>
      rw [hm] at this
      exact ⟨m, rfl, memImgOKB_sound this⟩

theorem stateOKB_shape {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    StateShapeOK D st := by
  intro idx md hmd
  obtain ⟨m, hm, hsh⟩ := stateOKB_shapeW h idx md hmd
  exact ⟨m, hm, hsh.toFullTable⟩

theorem stateOKB_flopSize {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    D.flops.size ≤ st.flops.size := by
  unfold stateOKB at h
  simp only [Bool.and_eq_true, decide_eq_true_eq] at h
  exact h.1

theorem inputOKB_sound {D : DesignCert} {n : Nat} (h : inputOKB D n = true) :
    ∀ sd ∈ D.sources.toList, SourceInputBound sd n := by
  unfold inputOKB at h
  simp only [List.all_eq_true, decide_eq_true_eq] at h
  exact h

theorem flopBoundOKB_sound {D : DesignCert} {n : Nat} (h : flopBoundOKB D n = true) :
    ∀ sd ∈ D.sources.toList, SourceFlopBound sd n := by
  unfold flopBoundOKB at h
  simp only [List.all_eq_true, decide_eq_true_eq] at h
  exact h

theorem edgesOKB_flops {D : DesignCert} {e : ClockEdges} (h : edgesOKB D e = true) :
    ∀ f ∈ D.flops.toList, f.clock < e.size := by
  unfold edgesOKB at h
  simp only [Bool.and_eq_true, List.all_eq_true, decide_eq_true_eq] at h
  exact h.1

theorem edgesOKB_mems {D : DesignCert} {e : ClockEdges} (h : edgesOKB D e = true) :
    ∀ md ∈ D.memories.toList, md.clock < e.size := by
  unfold edgesOKB at h
  simp only [Bool.and_eq_true, List.all_eq_true, decide_eq_true_eq] at h
  exact h.2

/-! ## 4. Two Booleans give the whole resolved cycle

Nothing is assumed: `admitsB` is decided from the design and `runtimeOKB` from
the state, the input and the edge vector, and between them they discharge every
antecedent of `admits_mainM_resolved`. -/

theorem admits_run {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsB D = true) (hR : runtimeOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ StateShapeOK D r.nextState := by
  unfold runtimeOKB at hR
  simp only [Bool.and_eq_true] at hR
  obtain ⟨⟨⟨hst, hin⟩, hfb⟩, hed⟩ := hR
  exact admits_mainM_resolved hA (stateOKB_shape hst) (inputOKB_sound hin)
    (flopBoundOKB_sound hfb) (edgesOKB_flops hed) (edgesOKB_mems hed)
    (stateOKB_flopSize hst)

/-! ## 5. Executed: the policy accepts the fixtures and REFUSES the near misses

`mixedDesign` declares one `aw = 5`, `dw = 32` memory on clock 0 and reads
`input 0 … 3`; `twoMemDesign` declares two, on clocks 0 and 1. -/

#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } initImg == true
#guard stateOKB mixedDesign initState == true
#guard stateOKB mixedDesign zeroState == true
#guard stateOKB twoMemDesign twoMemState == true
#guard inputOKB mixedDesign 4 == true
#guard flopBoundOKB mixedDesign 0 == true
#guard edgesOKB mixedDesign #[true] == true
#guard edgesOKB twoMemDesign #[true, false] == true
#guard runtimeOKB mixedDesign #[mk_bv 5 20, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
         initState #[true] == true
#guard runtimeOKB twoMemDesign #[mk_bv 5 3, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
         twoMemState #[true, true] == true

/-- A table of the right LENGTH whose words are the wrong width: `FullTable`
cannot see this, `MemShapeOK` refuses it. -/
private def narrowImg : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 7 (Int.ofNat k)), mk_bv 32 0⟩
/-- …and one whose words are right but whose DEFAULT is not. -/
private def badDfltImg : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 32 (Int.ofNat k)), mk_bv 7 0⟩
/-- …and one that is simply too short. -/
private def shortImg : MemImg :=
  ⟨(List.range 16).map (fun k => mk_bv 32 (Int.ofNat k)), mk_bv 32 0⟩

#guard MemImg.FullTable 5 narrowImg       -- the OLD shape accepts it
#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } narrowImg == false
#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } badDfltImg == false
#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } shortImg == false
#guard stateOKB mixedDesign { flops := #[], mems := #[narrowImg] } == false
#guard stateOKB mixedDesign { flops := #[], mems := #[] } == false        -- image ABSENT
#guard stateOKB twoMemDesign { flops := #[], mems := #[imgA] } == false   -- only one of two
#guard inputOKB mixedDesign 3 == false                                   -- `input 3` reads past
#guard edgesOKB twoMemDesign #[true] == false                            -- clock 1 undeclared
#guard edgesOKB mixedDesign #[] == false
#guard runtimeOKB mixedDesign #[mk_bv 5 20, mk_bv 32 0, mk_bv 1 1] initState #[true] == false
#guard runtimeOKB twoMemDesign #[mk_bv 5 3, mk_bv 32 0, mk_bv 1 1, mk_bv 32 0]
         twoMemState #[true] == false

#print axioms memImgOKB_sound
#print axioms stateOKB_shape
#print axioms admits_run

end HwMem
end Projection
