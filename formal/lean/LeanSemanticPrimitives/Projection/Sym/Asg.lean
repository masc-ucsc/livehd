/-
`Sym.Asg` -- every concrete run has an assignment that satisfies the cut
definitions (M3 S2).

An obligation is `defs ∧ ¬goal` over primary variables (inputs, state) and
cut variables (`SymSt.defs`). Its unsatisfiability says something about every
CONCRETE run only if each run's primary values extend to an assignment that
satisfies the definitions. That holds when every definition mentions only
variables below its own index -- checked at run time by `scopedB`, a cheap
traversal -- and the extension is then built by evaluating the definitions in
order (`extendAsg`; proofs only, never executed).
-/
import LeanSemanticPrimitives.Projection.Sym.Eval
import Lean.Data.RArray

namespace Projection.Sym
open Std.Tactic.BVDecide

/-! ## Locality: an expression reads only the variables below its bound -/

/-- One past the largest variable index `e` mentions (0 for none). -/
def varBound : {w : Nat} → BVExpr w → Nat
  | _, .var i => i + 1
  | _, .const _ => 0
  | _, .extract _ _ e => varBound e
  | _, .bin l _ r => max (varBound l) (varBound r)
  | _, .un _ e => varBound e
  | _, .append l r _ => max (varBound l) (varBound r)
  | _, .replicate _ e _ => varBound e
  | _, .shiftLeft l r => max (varBound l) (varBound r)
  | _, .shiftRight l r => max (varBound l) (varBound r)
  | _, .arithShiftRight l r => max (varBound l) (varBound r)

theorem eval_congr {σ₁ σ₂ : Asg} :
    ∀ {w : Nat} (e : BVExpr w), (∀ i, i < varBound e → σ₁.get i = σ₂.get i) → e.eval σ₁ = e.eval σ₂
  | _, .var i, h => by
      have := h i (by simp [varBound])
      simp only [BVExpr.eval_var]; rw [this]
  | _, .const _, _ => rfl
  | _, .extract _ _ e, h => by
      simp only [BVExpr.eval_extract, eval_congr e (fun i hi => h i (by simpa [varBound] using hi))]
  | _, .bin l _ r, h => by
      simp only [BVExpr.eval_bin,
        eval_congr l (fun i hi => h i (by simp [varBound]; omega)),
        eval_congr r (fun i hi => h i (by simp [varBound]; omega))]
  | _, .un _ e, h => by
      simp only [BVExpr.eval_un, eval_congr e (fun i hi => h i (by simpa [varBound] using hi))]
  | _, .append l r _, h => by
      show (BVExpr.append l r _).eval σ₁ = (BVExpr.append l r _).eval σ₂
      rw [eval_append', eval_append',
        eval_congr l (fun i hi => h i (by simp [varBound]; omega)),
        eval_congr r (fun i hi => h i (by simp [varBound]; omega))]
  | _, .replicate _ e _, h => by
      show (BVExpr.replicate _ e _).eval σ₁ = (BVExpr.replicate _ e _).eval σ₂
      rw [eval_replicate', eval_replicate', eval_congr e (fun i hi => h i (by simpa [varBound] using hi))]
  | _, .shiftLeft l r, h => by
      simp only [BVExpr.eval_shiftLeft,
        eval_congr l (fun i hi => h i (by simp [varBound]; omega)),
        eval_congr r (fun i hi => h i (by simp [varBound]; omega))]
  | _, .shiftRight l r, h => by
      simp only [BVExpr.eval_shiftRight,
        eval_congr l (fun i hi => h i (by simp [varBound]; omega)),
        eval_congr r (fun i hi => h i (by simp [varBound]; omega))]
  | _, .arithShiftRight l r, h => by
      simp only [BVExpr.eval_arithShiftRight,
        eval_congr l (fun i hi => h i (by simp [varBound]; omega)),
        eval_congr r (fun i hi => h i (by simp [varBound]; omega))]

/-! ## The scope check -/

/-- Every definition reads only variables below its own index. -/
def scopedB (st : SymSt) : Bool :=
  (List.range st.defs.size).all fun k =>
    match st.defs[k]? with
    | some d => decide (varBound d.2 ≤ st.base + k)
    | none   => false

theorem scopedB_sound {st : SymSt} (h : scopedB st = true) :
    ∀ k (hk : k < st.defs.size), varBound (st.defs[k]'hk).2 ≤ st.base + k := by
  intro k hk
  unfold scopedB at h
  rw [List.all_eq_true] at h
  have := h k (List.mem_range.mpr hk)
  rw [Array.getElem?_eq_getElem hk] at this
  simpa using this

/-! ## Assignments from arrays -/

abbrev PBV := BVExpr.PackedBitVec

/-- An assignment holding `xs` at indices `0 … xs.size-1`. -/
def asgOf (xs : Array PBV) : Asg :=
  if h : 0 < xs.size then Lean.RArray.ofArray xs h else .leaf ⟨0#0⟩

theorem asgOf_get {xs : Array PBV} {i : Nat} {x : PBV} (hi : xs[i]? = some x) :
    (asgOf xs).get i = x := by
  have hlt : i < xs.size := by
    rcases Nat.lt_or_ge i xs.size with h | h
    · exact h
    · rw [Array.getElem?_eq_none h] at hi; cases hi
  rw [Array.getElem?_eq_getElem hlt] at hi
  cases hi
  have h0 : 0 < xs.size := Nat.lt_of_le_of_lt (Nat.zero_le _) hlt
  simp only [asgOf, h0, dif_pos, BVExpr.Assignment.get]
  exact Lean.RArray.get_ofFn (fun j : Fin xs.size => xs[j]) h0 ⟨i, hlt⟩

/-- Append the definitions' values, each evaluated under the prefix built so far. -/
def extendAsg (xs : Array PBV) : List (Σ w, BVExpr w) → Array PBV
  | [] => xs
  | d :: ds => extendAsg (xs.push ⟨d.2.eval (asgOf xs)⟩) ds

theorem extendAsg_size (xs : Array PBV) :
    ∀ ds : List (Σ w, BVExpr w), (extendAsg xs ds).size = xs.size + ds.length
  | [] => rfl
  | d :: ds => by simp [extendAsg, extendAsg_size _ ds]; omega

theorem extendAsg_prefix (xs : Array PBV) :
    ∀ (ds : List (Σ w, BVExpr w)) (i : Nat), i < xs.size → (extendAsg xs ds)[i]? = xs[i]?
  | [], _, _ => rfl
  | d :: ds, i, hi => by
      simp only [extendAsg]
      rw [extendAsg_prefix _ ds i (by simp; omega), Array.getElem?_push_lt hi, Array.getElem?_eq_getElem hi]

/-- The value at a definition's own index is that definition, evaluated under
the prefix before it. -/
theorem extendAsg_at (xs : Array PBV) :
    ∀ (ds : List (Σ w, BVExpr w)) (k : Nat) (d : Σ w, BVExpr w), ds[k]? = some d →
      ∃ pre : Array PBV, pre.size = xs.size + k ∧
        (∀ i, i < pre.size → (extendAsg xs ds)[i]? = pre[i]?) ∧
        (extendAsg xs ds)[xs.size + k]? = some ⟨d.2.eval (asgOf pre)⟩
  | [], _, _, hk => by simp at hk
  | d :: ds, 0, d', hk => by
      simp at hk; subst hk
      refine ⟨xs, by simp, fun i hi => ?_, ?_⟩
      · simp only [extendAsg]
        rw [extendAsg_prefix _ ds i (by simp; omega), Array.getElem?_push_lt hi, Array.getElem?_eq_getElem hi]
      · simp only [extendAsg, Nat.add_zero]
        rw [extendAsg_prefix _ ds xs.size (by simp), Array.getElem?_push_size]
  | d :: ds, k + 1, d', hk => by
      simp only [List.getElem?_cons_succ] at hk
      obtain ⟨pre, hsz, hpre, hat⟩ := extendAsg_at (xs.push ⟨d.2.eval (asgOf xs)⟩) ds k d' hk
      refine ⟨pre, by simp at hsz; omega, fun i hi => ?_, ?_⟩
      · simp only [extendAsg]; exact hpre i hi
      · simp only [extendAsg]
        have e : xs.size + (k + 1) = (xs.push ⟨d.2.eval (asgOf xs)⟩).size + k := by simp; omega
        rw [e]; exact hat

/-- The extended assignment: primary values `xs` (exactly `st.base` of them),
then each cut variable. -/
def extendSt (xs : Array PBV) (st : SymSt) : Asg := asgOf (extendAsg xs st.defs.toList)

/-- **Every concrete run has a consistent assignment.** With the definitions
scoped, extending the primary values `xs` satisfies every definition and
keeps the primary values. -/
theorem extendSt_consistent {xs : Array PBV} {st : SymSt} (hb : xs.size = st.base)
    (hs : scopedB st = true) :
    st.Consistent (extendSt xs st) ∧ ∀ i (x : PBV), xs[i]? = some x → (extendSt xs st).get i = x := by
  have hscope := scopedB_sound hs
  have hsz := extendAsg_size xs st.defs.toList
  refine ⟨fun k hk => ?_, fun i x hi => ?_⟩
  · have hd : st.defs.toList[k]? = some (st.defs[k]'hk) := by simp [hk]
    obtain ⟨pre, hpsz, hpre, hat⟩ := extendAsg_at xs st.defs.toList k _ hd
    have hget : (extendSt xs st).get (st.base + k) = ⟨(st.defs[k]'hk).2.eval (asgOf pre)⟩ := by
      unfold extendSt
      rw [← hb]
      exact asgOf_get hat
    -- the definition reads only the prefix, where the two assignments agree
    have hcong : (st.defs[k]'hk).2.eval (asgOf pre) = (st.defs[k]'hk).2.eval (extendSt xs st) := by
      apply eval_congr
      intro i hi
      have hip : i < pre.size := by have := hscope k hk; omega
      have hx : pre[i]? = some (pre[i]'hip) := Array.getElem?_eq_getElem hip
      unfold extendSt
      rw [asgOf_get hx, asgOf_get (by rw [hpre i hip]; exact hx)]
    revert hget hcong
    generalize st.defs[k]'hk = d
    obtain ⟨w, e⟩ := d
    intro hget hcong
    show (BVExpr.var (st.base + k) : BVExpr w).eval (extendSt xs st) = e.eval (extendSt xs st)
    rw [BVExpr.eval_var, hget, ← hcong]
    simp
  · unfold extendSt
    exact asgOf_get (by rw [extendAsg_prefix _ _ i (by
      rcases Nat.lt_or_ge i xs.size with h | h
      · exact h
      · rw [Array.getElem?_eq_none h] at hi; cases hi)]; exact hi)

end Projection.Sym
