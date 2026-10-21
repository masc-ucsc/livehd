/-
`Kit.Norm` -- normalized memory-free states, and state values built from leaves.

A refinement map gives the spec's state as words of the implementation: one word per spec flop.
The spec state it denotes is NORMALIZED, each flop `mk_bv w (bv_uint b)`, so equality of states
is equality of their normalized values, which is what SAT proves. After a spec step the next
state is normalized again (`rebuildS`: its flops re-read at their widths).

- `normBV`, `normSt`, `flopSt`: the concrete side.
- `leafState`: a memory-free state value from flop leaves.
- `outLeaves`: leaves taken from output words (`outLeaves_rel` with `outsVals`).
- `flopLeaves` / `rebuildS`: leaves re-read from a state value (`rebuildS_rel` with `normSt`).

Memory-free only: specs with memories are deferred (hook H7). Mathlib-free.
-/
import HDLProof.Sym.ConjT

open Projection
namespace HDLProof.Kit
open HDLProof.Sym Std.Tactic.BVDecide Compiler

/-! ## Concrete -/

def normBV (b : BV) : BV := mk_bv b.width (bv_uint b)

@[simp] theorem normBV_width (b : BV) : (normBV b).width = b.width := rfl

theorem bv_uint_normBV (b : BV) : bv_uint (normBV b) = bv_uint b := by
  simp only [normBV, mk_bv, bv_uint, Int.emod_emod]

theorem normBV_normBV (b : BV) : normBV (normBV b) = normBV b := by
  have h := bv_uint_normBV b
  unfold normBV at h ⊢
  rw [h]; rfl

/-- The memory-free state with these flops. -/
def flopSt (fl : List BV) : StateM := ⟨fl.toArray, #[]⟩

/-- The memory-free state with the first `n` flops of `s`, normalized. -/
def normSt (s : StateM) (n : Nat) : StateM := flopSt ((s.flops.toList.take n).map normBV)

/-! ## State values from leaves -/

/-- A leaf: a bit-vector expression of its width. -/
abbrev Leaf := Σ w, BVExpr w

/-- A leaf denotes `b` under `σ`: same width, same normalized value. -/
def LeafRel (σ : Asg) (p : Leaf) (b : BV) : Prop :=
  b.width = p.1 ∧ bv_uint b = ((p.2.eval σ).toNat : Int)

/-- Leaves denote a list of bit vectors, element by element. -/
inductive LeavesRel (σ : Asg) : List Leaf → List BV → Prop where
  | nil  : LeavesRel σ [] []
  | cons : LeafRel σ p b → LeavesRel σ ps bs → LeavesRel σ (p :: ps) (b :: bs)

theorem LeavesRel.length {σ : Asg} {ps : List Leaf} {bs : List BV} (h : LeavesRel σ ps bs) :
    ps.length = bs.length := by
  induction h with
  | nil => rfl
  | cons _ _ ih => simp [ih]

theorem leaf_rel {σ : Asg} {p : Leaf} {b : BV} (h : LeafRel σ p b) :
    SRel σ (.bv p.1 p.2) (ofBV b) :=
  .bv (asBV_ofBV b) h.1 h.2

theorem leaves_relLF {σ : Asg} :
    ∀ {ps : List Leaf} {bs : List BV}, LeavesRel σ ps bs →
      SRelLF σ ofBV (ps.map fun p => SVal.bv p.1 p.2) bs
  | _, _, .nil => .nil
  | _, _, .cons h hs => .cons (leaf_rel h) (leaves_relLF hs)

/-- The memory-free state value with these flop leaves. -/
def leafState (ls : List Leaf) : SVal :=
  .ctor StateM.tagStateM [sListOf (ls.map fun p => SVal.bv p.1 p.2), .conc (encArr MemImg.encMemImg #[])]

theorem leafState_rel {σ : Asg} {ls : List Leaf} {fl : List BV} (h : LeavesRel σ ls fl) :
    SRel σ (leafState ls) (StateM.encStateM (flopSt fl)) := by
  unfold leafState StateM.encStateM flopSt encBVs encArr
  refine .ctor (.cons ?_ (.cons .conc .nil))
  exact sListOf_rel ofBV (leaves_relLF h)

/-- A leaf denotes the normalized copy too. -/
theorem LeafRel.norm {σ : Asg} {p : Leaf} {b : BV} (h : LeafRel σ p b) : LeafRel σ p (normBV b) :=
  ⟨h.1, by rw [bv_uint_normBV]; exact h.2⟩

/-! ## Leaves from output words -/

/-- Output words `os`, read at widths `ws`. -/
def outLeaves (so : SVal) : List Nat → List Nat → Option (List Leaf)
  | [], [] => some []
  | o :: os, w :: ws => do
      let ⟨w', e⟩ ← sNth so o >>= bvOfS
      if h : w' = w then
        let rest ← outLeaves so os ws
        pure (⟨w, h ▸ e⟩ :: rest)
      else none
  | _, _ => none

/-- The normalized values of output words `os` (a missing word reads as `default`). -/
def outsVals (outs : Array BV) (os : List Nat) : List BV :=
  os.map fun o => normBV (outs.getD o default)

theorem outLeaves_rel {σ : Asg} {so : SVal} {outs : Array BV} (hr : SRel σ so (encBVs outs)) :
    ∀ (os ws : List Nat) (ls : List Leaf), outLeaves so os ws = some ls →
      LeavesRel σ ls (outsVals outs os) ∧ ls.map Sigma.fst = ws
  | [], [], ls, h => by simp [outLeaves] at h; subst h; exact ⟨.nil, rfl⟩
  | o :: os, w :: ws, ls, h => by
      simp only [outLeaves, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨⟨w', e⟩, ⟨sk, hsk, hb⟩, h⟩ := h
      split at h
      · rename_i hw
        subst hw
        simp only [Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at h
        obtain ⟨rest, hrest, rfl⟩ := h
        obtain ⟨b, hbk, rb⟩ := sNth_sound so outs.toList o sk hr hsk
        obtain ⟨hw, hu⟩ := bvOfS_sound rb hb
        have hget : outs.getD o default = b := by
          simp only [Array.getD_eq_getD_getElem?]
          rw [show outs[o]? = outs.toList[o]? by simp, hbk]; rfl
        obtain ⟨ih1, ih2⟩ := outLeaves_rel hr os ws rest hrest
        refine ⟨?_, by simp [ih2]⟩
        simp only [outsVals, List.map_cons]
        rw [hget]
        exact .cons (LeafRel.norm ⟨hw, hu⟩) ih1
      · cases h
  | [], _ :: _, _, h => by simp [outLeaves] at h
  | _ :: _, [], _, h => by simp [outLeaves] at h

/-! ## Re-reading a state value at given widths -/

/-- The flops `k, k+1, ...` of a state value, at widths `ws`. -/
def flopLeaves (s : SVal) : List Nat → Nat → Option (List Leaf)
  | [], _ => some []
  | w :: ws, k => do
      let ⟨w', e⟩ ← flopE s k
      if h : w' = w then
        let rest ← flopLeaves s ws (k + 1)
        pure (⟨w, h ▸ e⟩ :: rest)
      else none

theorem flopLeaves_rel {σ : Asg} {s : SVal} {st : StateM} (hr : SRel σ s (StateM.encStateM st)) :
    ∀ (ws : List Nat) (k : Nat) (ls : List Leaf), flopLeaves s ws k = some ls →
      LeavesRel σ ls (((st.flops.toList.drop k).take ws.length).map normBV) ∧ ls.map Sigma.fst = ws
  | [], k, ls, h => by
      simp [flopLeaves] at h; subst h; exact ⟨by simpa using LeavesRel.nil, rfl⟩
  | w :: ws, k, ls, h => by
      simp only [flopLeaves, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨⟨w', e⟩, he, h⟩ := h
      split at h
      · rename_i hw
        subst hw
        simp only [Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at h
        obtain ⟨rest, hrest, rfl⟩ := h
        obtain ⟨b, hb, hbw, hbu⟩ := flopE_sound hr he
        have hb' : st.flops.toList[k]? = some b := by simpa using hb
        obtain ⟨hk, hbk⟩ := List.getElem?_eq_some_iff.mp hb'
        have hdrop : st.flops.toList.drop k = b :: st.flops.toList.drop (k + 1) := by
          rw [List.drop_eq_getElem_cons hk, hbk]
        obtain ⟨ih1, ih2⟩ := flopLeaves_rel hr ws (k + 1) rest hrest
        refine ⟨?_, by simp [ih2]⟩
        rw [hdrop, List.length_cons, List.take_succ_cons, List.map_cons]
        exact .cons (LeafRel.norm ⟨hbw, hbu⟩) ih1
      · cases h

/-- The state value re-read at widths `ws`: it denotes the normalized state. -/
def rebuildS (s : SVal) (ws : List Nat) : Option SVal := (flopLeaves s ws 0).map leafState

theorem rebuildS_rel {σ : Asg} {s s' : SVal} {st : StateM} {ws : List Nat}
    (hr : SRel σ s (StateM.encStateM st)) (h : rebuildS s ws = some s') :
    SRel σ s' (StateM.encStateM (normSt st ws.length)) := by
  simp only [rebuildS, Option.map_eq_some_iff] at h
  obtain ⟨ls, hls, rfl⟩ := h
  have := flopLeaves_rel hr ws 0 ls hls
  simp only [List.drop_zero] at this
  exact leafState_rel this.1

end HDLProof.Kit
