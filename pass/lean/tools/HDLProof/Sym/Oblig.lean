/-
`Sym.Oblig` -- the formulas a SAT solver is asked to refute (M3 S3).

  * `defsE st`     every cut variable equals its definition;
  * `baseBad`      some step `j` of the base window has every assumption up to
                   `j` and a FALSE property at `j`;
  * `stepConj`     the induction step: assumptions at every step, the
                   property at every step but the last, and NOT at the last.

Each comes with what its value means under an assignment.
-/
import HDLProof.Sym.Unroll

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide

/-! ## The definitions -/

def defLit (base : Nat) (p : (Σ w, BVExpr w) × Nat) : BVLogicalExpr :=
  .literal (.bin (.var (base + p.2) : BVExpr p.1.1) .eq p.1.2)

def defsE (st : SymSt) : BVLogicalExpr := conjE (st.defs.toList.zipIdx.map (defLit st.base))

theorem eval_defsE {σ : Asg} {st : SymSt} (hc : st.Consistent σ) : (defsE st).eval σ = true := by
  rw [defsE, eval_conjE, List.all_eq_true]
  intro x hx
  rw [List.mem_map] at hx
  obtain ⟨⟨d, k⟩, hdk, rfl⟩ := hx
  have hk := List.mem_zipIdx hdk
  have hkl : k < st.defs.size := by simpa using hk.2.1
  have hd : d = st.defs[k] := by
    have := hk.2.2
    simpa using this
  subst hd
  have := hc k hkl
  revert this
  generalize st.defs[k] = d
  obtain ⟨w, e⟩ := d
  intro this
  simp only [defLit, BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval]
  simp only [DefOK] at this
  simp [this]

/-! ## The base window -/

/-- `j`-th disjunct: the assumptions of steps `0 … j` and the property false at `j`. -/
def baseBadAux : List BVLogicalExpr → List StepS → List BVLogicalExpr
  | _,   []      => []
  | pre, s :: ss => conjE (pre ++ [s.2.1, .not s.1]) :: baseBadAux (pre ++ [s.2.1]) ss

def baseBad (steps : List StepS) : BVLogicalExpr := disjE (baseBadAux [] steps)

/-- If the prefix's assumptions hold, every step's assumption holds up to `j`,
and the property fails at `j`, the base formula is true. -/
theorem baseBad_true {σ : Asg} :
    ∀ (pre : List BVLogicalExpr) (steps : List StepS) (j : Nat) (hj : j < steps.length),
      pre.all (·.eval σ) = true →
      (∀ i (hi : i < steps.length), i ≤ j → (steps[i]'hi).2.1.eval σ = true) →
      (steps[j]'hj).1.eval σ = false →
      (disjE (baseBadAux pre steps)).eval σ = true
  | _, [], _, hj, _, _, _ => by simp at hj
  | pre, s :: ss, 0, _, hpre, hasm, hok => by
      rw [baseBadAux, disjE, eval_or]
      have : (conjE (pre ++ [s.2.1, .not s.1])).eval σ = true := by
        rw [eval_conjE, List.all_append, hpre]
        have h0 := hasm 0 (by simp) (Nat.le_refl 0)
        simp only [List.getElem_cons_zero] at h0 hok
        simp [h0, eval_not, hok]
      simp [this]
  | pre, s :: ss, j + 1, hj, hpre, hasm, hok => by
      rw [baseBadAux, disjE, eval_or]
      have h0 := hasm 0 (by simp) (by omega)
      have := baseBad_true (pre ++ [s.2.1]) ss j (by simpa using hj)
        (by rw [List.all_append, hpre]; simpa using h0)
        (fun i hi hij => by
          have h := hasm (i + 1) (by simp; omega) (by omega)
          rwa [List.getElem_cons_succ] at h)
        (by simpa using hok)
      simp [this]

/-! ## The induction step -/

def stepConj : List StepS → List BVLogicalExpr
  | []      => []
  | s :: ss =>
      match ss with
      | []     => [s.2.1, .not s.1]
      | _ :: _ => s.2.1 :: s.1 :: stepConj ss

theorem stepConj_true {σ : Asg} :
    ∀ (steps : List StepS) (hne : 0 < steps.length),
      (∀ i (hi : i < steps.length), (steps[i]'hi).2.1.eval σ = true) →
      (∀ i (hi : i < steps.length), i + 1 < steps.length → (steps[i]'hi).1.eval σ = true) →
      (steps[steps.length - 1]'(by omega)).1.eval σ = false →
      (conjE (stepConj steps)).eval σ = true
  | [], hne, _, _, _ => by simp at hne
  | [s], _, hasm, _, hlast => by
      rw [stepConj, eval_conjE]
      have := hasm 0 (by simp)
      simp only [List.getElem_cons_zero, List.length_singleton, Nat.sub_self] at this hlast
      simp [this, eval_not, hlast]
  | s :: s' :: ss, _, hasm, hoks, hlast => by
      rw [stepConj, conjE, conjE, eval_and, eval_and]
      have h0 := hasm 0 (by simp)
      have h1 := hoks 0 (by simp) (by simp)
      have := stepConj_true (s' :: ss) (by simp)
        (fun i hi => by
          have h := hasm (i + 1) (by simp at hi ⊢; omega)
          rwa [List.getElem_cons_succ] at h)
        (fun i hi hi' => by
          have h := hoks (i + 1) (by simp at hi ⊢; omega) (by simp at hi' ⊢; omega)
          rwa [List.getElem_cons_succ] at h)
        (by simpa using hlast)
      simp only [List.getElem_cons_zero] at h0 h1
      simp [h0, h1, this]

end HDLProof.Sym
