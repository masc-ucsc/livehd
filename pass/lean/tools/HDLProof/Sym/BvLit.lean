import HDLProof.Sym.Kit
import LeanSemanticPrimitives.Translation.LGraphModel
import LeanSemanticPrimitives.Projection.AxiomGate

/-!
`Sym.BvLit` -- two literal shapes the refinement-map obligations use, with their meaning.

- `eqBvE x y`: two bit vectors of any widths are equal as unsigned values (both
  zero-extended to the larger width); `eval_eqBvE`.
- `advLit c`: a one-bit expression is 1; `eval_advLit`.

They came from `Sym.Refine`, the checker of the removed `sym_refine` driver; `sym_refmap`
(`Check.RefMapSound`, `Check.SpecFacts`) is their only user.
-/

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide

/-- Normalized equality of two bit vectors of any widths. -/
def eqBvE (x y : Σ w, BVExpr w) : BVLogicalExpr :=
  let W := max x.1 y.1
  .literal (.bin (zextE W x.2) .eq (zextE W y.2))

theorem eval_eqBvE {σ : Asg} {x y : Σ w, BVExpr w} {bx by' : BV}
    (hx : bv_uint bx = ((x.2.eval σ).toNat : Int)) (hy : bv_uint by' = ((y.2.eval σ).toNat : Int)) :
    (eqBvE x y).eval σ = decide (bv_uint bx = bv_uint by') := by
  obtain ⟨wx, ex⟩ := x
  obtain ⟨wy, ey⟩ := y
  simp only [eqBvE, BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval]
  rw [hx, hy]
  have l1 := (ex.eval σ).isLt
  have l2 := (ey.eval σ).isLt
  have m1 : 2 ^ wx ≤ 2 ^ max wx wy := Nat.pow_le_pow_right (by decide) (by omega)
  have m2 : 2 ^ wy ≤ 2 ^ max wx wy := Nat.pow_le_pow_right (by decide) (by omega)
  apply Bool.eq_iff_iff.mpr
  simp only [beq_iff_eq, decide_eq_true_eq]
  constructor
  · intro h
    have := congrArg BitVec.toNat h
    rw [toNat_zextE, toNat_zextE, Nat.mod_eq_of_lt (by omega), Nat.mod_eq_of_lt (by omega)] at this
    omega
  · intro h
    apply BitVec.eq_of_toNat_eq
    rw [toNat_zextE, toNat_zextE, Nat.mod_eq_of_lt (by omega), Nat.mod_eq_of_lt (by omega)]
    omega

/-- A one-bit expression is 1. -/
def advLit (c : BVExpr 1) : BVLogicalExpr := .literal (.bin c .eq (.const 1#1))

theorem eval_advLit (σ : Asg) (c : BVExpr 1) : (advLit c).eval σ = bitOf (c.eval σ) := by
  simp only [advLit, BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval, BVExpr.eval_const,
    bitOf]
  generalize c.eval σ = x
  have : ∀ y : BitVec 1, y = 0#1 ∨ y = 1#1 := by
    intro y
    have := y.isLt
    rcases (show y.toNat = 0 ∨ y.toNat = 1 by omega) with h | h
    · left; exact BitVec.eq_of_toNat_eq (by simp [h])
    · right; exact BitVec.eq_of_toNat_eq (by simp [h])
  rcases this x with h | h <;> subst h <;> decide

#audit_axioms eval_eqBvE
#audit_axioms eval_advLit

end HDLProof.Sym
