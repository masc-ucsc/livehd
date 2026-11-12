/-
`Sym.IntDiv` -- the object language's integer division and remainder on EXACT symbolic
integers (`SInt`): `divI`/`modI` are `y = 0 ? 0 : x / y` and `y = 0 ? 0 : x % y` with Lean's
Euclidean `/` and `%` (`Int.ediv`/`Int.emod`). Built from an unsigned division of the
magnitudes (`x < 0 ? -x-1 : x` and `|y|`) followed by the sign fix-up of Euclidean division
(`ediv_parts`, `emod_parts`), so the bit-blaster sees one `udiv`/`umod`. Mathlib-free.
-/
import HDLProof.Sym.Kit

namespace HDLProof.Sym
open Std.Tactic.BVDecide

namespace IntDiv
/-- `a / b` for `b ≠ 0`, from `M := (a<0 ? -a-1 : a)` and `B := |b|`. -/
theorem ediv_parts {a b : Int} (hb : b ≠ 0) :
    a / b = (if b < 0 then -(if a < 0 then -((if a < 0 then -a - 1 else a) / b.natAbs + 1)
                               else (if a < 0 then -a - 1 else a) / b.natAbs)
             else (if a < 0 then -((if a < 0 then -a - 1 else a) / b.natAbs + 1)
                   else (if a < 0 then -a - 1 else a) / b.natAbs)) := by
  have hB : (0 : Int) < b.natAbs := by omega
  -- reduce to a positive divisor
  have key : a / (b.natAbs : Int) = (if a < 0 then -((-a - 1) / b.natAbs + 1) else a / b.natAbs) := by
    by_cases ha : a < 0
    · obtain ⟨m, rfl⟩ : ∃ m : Nat, a = Int.negSucc m := ⟨(-a - 1).toNat, by omega⟩
      rw [Int.negSucc_ediv m hB]
      simp only [ha, if_true]
      have : -Int.negSucc m - 1 = (m : Int) := by omega
      rw [this]; rfl
    · simp [ha]
  by_cases hbn : b < 0
  · have e : b = -(b.natAbs : Int) := by omega
    conv => lhs; rw [e]
    rw [Int.ediv_neg, key]
    by_cases ha : a < 0 <;> simp [ha, hbn]
  · have e : b = (b.natAbs : Int) := by omega
    conv => lhs; rw [e]
    rw [key]
    by_cases ha : a < 0 <;> simp [ha, hbn]

theorem emod_parts {a b : Int} (hb : b ≠ 0) :
    a % b = (if a < 0 then (b.natAbs : Int) - 1 - (-a - 1) % b.natAbs else a % b.natAbs) := by
  have hB : (0 : Int) < b.natAbs := by omega
  have key : a % (b.natAbs : Int) = (if a < 0 then (b.natAbs : Int) - 1 - (-a - 1) % b.natAbs else a % b.natAbs) := by
    by_cases ha : a < 0
    · obtain ⟨m, rfl⟩ : ∃ m : Nat, a = Int.negSucc m := ⟨(-a - 1).toNat, by omega⟩
      rw [Int.negSucc_emod m hB]
      simp only [ha, if_true]
      have : -Int.negSucc m - 1 = (m : Int) := by omega
      rw [this]
    · simp [ha]
  by_cases hbn : b < 0
  · have e : b = -(b.natAbs : Int) := by omega
    conv => lhs; rw [e]
    rw [Int.emod_neg, key]
    by_cases ha : a < 0 <;> simp [ha]
  · have e : b = (b.natAbs : Int) := by omega
    conv => lhs; rw [e]
    rw [key]
    by_cases ha : a < 0 <;> simp [ha]


open SInt

/-- `x < 0 ? -x-1 : x`: a value in `[0, 2^x.k)`. -/
def magS (x : SInt) : SInt := SInt.mux (isNeg x) ((neg x).sub (lit 1)) x
/-- `|y|`. -/
def absS (y : SInt) : SInt := SInt.mux (isNeg y) (neg y) y

theorem val_magS (σ : Asg) (x : SInt) :
    (magS x).val σ = if x.val σ < 0 then -x.val σ - 1 else x.val σ := by
  simp only [magS, val_mux, bit_isNeg, val_sub, val_neg, val_lit, decide_eq_true_eq]

theorem val_absS (σ : Asg) (y : SInt) :
    (absS y).val σ = ((y.val σ).natAbs : Int) := by
  simp only [absS, val_mux, bit_isNeg, val_neg, decide_eq_true_eq]
  split <;> omega

/-- Unsigned quotient and remainder of two NON-NEGATIVE exact integers. -/
def udivS (m b : SInt) : SInt × SInt :=
  let n := max m.k b.k + 1
  (ofUint (.bin (m.toBits n) .udiv (b.toBits n)), ofUint (.bin (m.toBits n) .umod (b.toBits n)))

theorem toNat_toBits (σ : Asg) (m : SInt) (n : Nat) (h0 : 0 ≤ m.val σ) (hn : m.k < n) :
    (((m.toBits n).eval σ).toNat : Int) = m.val σ := by
  rw [toBits_eq, BitVec.toNat_ofInt]
  have hr := val_range σ m
  have hp : (2 : Int) ^ m.k < ((2 ^ n : Nat) : Int) := by
    have := Nat.pow_lt_pow_right (show 1 < 2 by decide) hn
    have e : ((2 ^ m.k : Nat) : Int) = (2 : Int) ^ m.k := by simp [Int.natCast_pow]
    omega
  rw [Int.emod_eq_of_lt h0 (by omega)]
  omega

theorem val_udivS (σ : Asg) (m b : SInt) (hm : 0 ≤ m.val σ) (hb : 0 ≤ b.val σ) :
    (udivS m b).1.val σ = m.val σ / b.val σ ∧ (udivS m b).2.val σ = m.val σ % b.val σ := by
  have tm := toNat_toBits σ m (max m.k b.k + 1) hm (by omega)
  have tb := toNat_toBits σ b (max m.k b.k + 1) hb (by omega)
  simp only [udivS, val_ofUint, BVExpr.eval_bin, BVBinOp.eval, BitVec.toNat_udiv, BitVec.toNat_umod]
  constructor
  · rw [Int.natCast_ediv, tm, tb]
  · rw [Int.natCast_emod, tm, tb]

/-- `divI`: `y = 0 ? 0 : x / y` (Euclidean). -/
def edivS (x y : SInt) : SInt :=
  let q0 := (udivS (magS x) (absS y)).1
  let qE := SInt.mux (isNeg x) (neg (q0.add (lit 1))) q0
  SInt.mux (SInt.eq y (lit 0)) (lit 0) (SInt.mux (isNeg y) (neg qE) qE)

/-- `modI`: `y = 0 ? 0 : x % y` (Euclidean). -/
def emodS (x y : SInt) : SInt :=
  let B := absS y
  let r0 := (udivS (magS x) B).2
  SInt.mux (SInt.eq y (lit 0)) (lit 0) (SInt.mux (isNeg x) ((B.sub (lit 1)).sub r0) r0)

theorem val_edivS (σ : Asg) (x y : SInt) :
    (edivS x y).val σ = if y.val σ = 0 then 0 else x.val σ / y.val σ := by
  have hm : 0 ≤ (magS x).val σ := by rw [val_magS]; split <;> omega
  have hb : 0 ≤ (absS y).val σ := by rw [val_absS]; omega
  have hq := (val_udivS σ (magS x) (absS y) hm hb).1
  simp only [edivS, val_mux, bit_eq, bit_isNeg, val_lit, val_neg, val_add, hq, val_magS, val_absS,
    decide_eq_true_eq]
  by_cases hy : y.val σ = 0
  · simp [hy]
  · rw [if_neg hy, if_neg hy, ediv_parts hy]

theorem val_emodS (σ : Asg) (x y : SInt) :
    (emodS x y).val σ = if y.val σ = 0 then 0 else x.val σ % y.val σ := by
  have hm : 0 ≤ (magS x).val σ := by rw [val_magS]; split <;> omega
  have hb : 0 ≤ (absS y).val σ := by rw [val_absS]; omega
  have hr := (val_udivS σ (magS x) (absS y) hm hb).2
  simp only [emodS, val_mux, bit_eq, bit_isNeg, val_lit, val_sub, hr, val_magS, val_absS,
    decide_eq_true_eq]
  by_cases hy : y.val σ = 0
  · simp [hy]
  · rw [if_neg hy, if_neg hy, emod_parts hy]
    by_cases hx : x.val σ < 0 <;> simp [hx]

end IntDiv

end HDLProof.Sym
