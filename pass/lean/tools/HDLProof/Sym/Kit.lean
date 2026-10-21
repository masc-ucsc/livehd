/-
`Sym.Kit` -- smart constructors over `Std` `BVExpr` (M3 S1): width changes,
one-bit Booleans, a mux, and EXACT integers (`SInt`: two's complement at a
width that never wraps). Every constructor comes with its evaluation theorem.
Mathlib-free.
-/
import Std.Tactic.BVDecide

namespace HDLProof.Sym
open Std.Tactic.BVDecide

abbrev Asg := BVExpr.Assignment

/-! ## Casts -/

theorem toNat_cast_eq {a b : Nat} (h : a = b) (y : BitVec b) : (h ▸ y : BitVec a).toNat = y.toNat := by
  subst h; rfl

theorem getLsbD_cast_eq {a b : Nat} (h : a = b) (y : BitVec b) (i : Nat) :
    (h ▸ y : BitVec a).getLsbD i = y.getLsbD i := by
  subst h; rfl

theorem eval_append' (σ : Asg) {w l r : Nat} (x : BVExpr l) (y : BVExpr r) (h : w = l + r) :
    (BVExpr.append x y h).eval σ = h ▸ (x.eval σ ++ y.eval σ) := rfl

theorem eval_replicate' (σ : Asg) {w w' : Nat} (n : Nat) (x : BVExpr w) (h : w' = w * n) :
    (BVExpr.replicate n x h).eval σ = h ▸ BitVec.replicate n (x.eval σ) := rfl

/-! ## Width changes -/

/-- Zero-extend (or truncate) to `v`. -/
def zextE {w : Nat} (v : Nat) (e : BVExpr w) : BVExpr v :=
  if h : w ≤ v then .append (.const (0#(v - w))) e (Nat.sub_add_cancel h).symm else .extract 0 v e

theorem toNat_zextE (σ : Asg) {w : Nat} (v : Nat) (e : BVExpr w) :
    ((zextE v e).eval σ).toNat = (e.eval σ).toNat % 2 ^ v := by
  unfold zextE
  split
  · rename_i h
    rw [eval_append' σ _ _ (Nat.sub_add_cancel h).symm, toNat_cast_eq (Nat.sub_add_cancel h).symm,
      BitVec.toNat_append]
    simp only [BVExpr.eval_const, BitVec.toNat_ofNat, Nat.zero_mod, Nat.zero_shiftLeft, Nat.zero_or]
    have := (e.eval σ).isLt
    rw [Nat.mod_eq_of_lt (Nat.lt_of_lt_of_le this (Nat.pow_le_pow_right (by decide) h))]
  · simp [BitVec.extractLsb'_toNat]

theorem getLsbD_zextE (σ : Asg) {w : Nat} (v : Nat) (e : BVExpr w) (i : Nat) :
    ((zextE v e).eval σ).getLsbD i = (decide (i < v) && (e.eval σ).getLsbD i) := by
  unfold zextE
  split
  · rename_i h
    rw [eval_append' σ _ _ (Nat.sub_add_cancel h).symm, getLsbD_cast_eq (Nat.sub_add_cancel h).symm,
      BitVec.getLsbD_append]
    by_cases hi : i < w
    · simp [hi]; omega
    · simp [hi]
      by_cases hv : i < v <;> simp [hv, BitVec.getLsbD_of_ge _ _ (by omega : w ≤ i)]
  · simp [BitVec.getLsbD_extractLsb']

/-- Sign-extend (or truncate) to `v`: place `e` in the HIGH bits, then shift
right arithmetically back down. -/
def sextE {w : Nat} (v : Nat) (e : BVExpr w) : BVExpr v :=
  if h : w ≤ v then
    .un (.arithShiftRightConst (v - w)) (.append e (.const (0#(v - w))) (Nat.add_sub_cancel' h).symm)
  else .extract 0 v e

theorem getLsbD_sextE (σ : Asg) {w : Nat} (v : Nat) (e : BVExpr w) (i : Nat) :
    ((sextE v e).eval σ).getLsbD i = ((e.eval σ).signExtend v).getLsbD i := by
  rw [BitVec.getLsbD_signExtend]
  unfold sextE
  split
  · rename_i h
    simp only [BVExpr.eval_un, BVUnOp.eval_arithShiftRightConst]
    rw [BitVec.getLsbD_sshiftRight, eval_append' σ _ _ (Nat.add_sub_cancel' h).symm]
    have hc := (Nat.add_sub_cancel' h).symm
    by_cases hv : i < v
    · simp only [hv, decide_true, Bool.true_and, show ¬ (v ≤ i) by omega, decide_false, Bool.not_false]
      by_cases hi : i < w
      · simp only [hi, if_true, show v - w + i < v by omega, getLsbD_cast_eq hc, BitVec.getLsbD_append,
          BVExpr.eval_const, show ¬ (v - w + i < v - w) by omega, if_false, Nat.add_sub_cancel_left]
      · simp only [hi, if_false, show ¬ (v - w + i < v) by omega]
        rw [BitVec.msb_eq_getLsbD_last, BitVec.msb_eq_getLsbD_last, getLsbD_cast_eq hc, BitVec.getLsbD_append]
        by_cases hw : w = 0
        · subst hw; simp
        · simp only [show ¬ (v - 1 < v - w) by omega, if_false, BVExpr.eval_const]
          congr 1; omega
    · simp [hv, show v ≤ i by omega]
  · rename_i h
    simp only [BVExpr.eval_extract, BitVec.getLsbD_extractLsb']
    by_cases hv : i < v
    · simp [hv, show i < w by omega]
    · simp [hv]

theorem eval_sextE (σ : Asg) {w : Nat} (v : Nat) (e : BVExpr w) :
    (sextE v e).eval σ = (e.eval σ).signExtend v :=
  BitVec.eq_of_getLsbD_eq (fun i _ => getLsbD_sextE σ v e i)

theorem eval_zextE (σ : Asg) {w : Nat} (v : Nat) (e : BVExpr w) :
    (zextE v e).eval σ = (e.eval σ).setWidth v := by
  apply BitVec.eq_of_toNat_eq
  rw [toNat_zextE, BitVec.toNat_setWidth]

/-! ## One-bit booleans -/

/-- The Boolean a one-bit expression denotes. -/
@[inline] def bitOf (b : BitVec 1) : Bool := b.getLsbD 0

def trueE : BVExpr 1 := .const 1#1
def falseE : BVExpr 1 := .const 0#1
def boolE (b : Bool) : BVExpr 1 := if b then trueE else falseE
def notE (a : BVExpr 1) : BVExpr 1 := .un .not a
def andE (a b : BVExpr 1) : BVExpr 1 := .bin a .and b
def orE (a b : BVExpr 1) : BVExpr 1 := .bin a .or b

@[simp] theorem bit_boolE (σ : Asg) (b : Bool) : bitOf ((boolE b).eval σ) = b := by
  cases b <;> simp [boolE, trueE, falseE, bitOf]
@[simp] theorem bit_notE (σ : Asg) (a : BVExpr 1) : bitOf ((notE a).eval σ) = !bitOf (a.eval σ) := by
  simp [notE, bitOf, BVUnOp.eval]
@[simp] theorem bit_andE (σ : Asg) (a b : BVExpr 1) :
    bitOf ((andE a b).eval σ) = (bitOf (a.eval σ) && bitOf (b.eval σ)) := by
  simp [andE, bitOf, BVBinOp.eval]
@[simp] theorem bit_orE (σ : Asg) (a b : BVExpr 1) :
    bitOf ((orE a b).eval σ) = (bitOf (a.eval σ) || bitOf (b.eval σ)) := by
  simp [orE, bitOf, BVBinOp.eval]

/-- `c ? a : b` at width `w`: the select bit, sign-extended, as a mask. -/
def muxE {w : Nat} (c : BVExpr 1) (a b : BVExpr w) : BVExpr w :=
  .bin (.bin (sextE w c) .and a) .or (.bin (.un .not (sextE w c)) .and b)

theorem eval_muxE (σ : Asg) {w : Nat} (c : BVExpr 1) (a b : BVExpr w) :
    (muxE c a b).eval σ = if bitOf (c.eval σ) then a.eval σ else b.eval σ := by
  apply BitVec.eq_of_getLsbD_eq
  intro i hi
  simp only [muxE, BVExpr.eval_bin, BVExpr.eval_un, BVBinOp.eval, BVUnOp.eval, BitVec.getLsbD_or,
    BitVec.getLsbD_and, BitVec.getLsbD_not, getLsbD_sextE, BitVec.getLsbD_signExtend, hi,
    decide_true, Bool.true_and, BitVec.msb_eq_getLsbD_last, bitOf]
  by_cases h1 : i < 1
  · obtain rfl : i = 0 := by omega
    by_cases hc : (c.eval σ).getLsbD 0 = true <;> simp at hc <;> simp [hc]
  · simp only [h1, if_false, Nat.sub_self]
    by_cases hc : (c.eval σ).getLsbD 0 = true <;> simp at hc <;> simp [hc]

/-! ## Exact integers

An `SInt` denotes the INTEGER `toInt` of its expression: two's complement at
width `k + 1` (never zero). Arithmetic widens first, so nothing ever wraps. -/

structure SInt where
  k : Nat
  e : BVExpr (k + 1)

namespace SInt

def val (σ : Asg) (x : SInt) : Int := (x.e.eval σ).toInt

theorem val_range (σ : Asg) (x : SInt) : -2 ^ x.k ≤ x.val σ ∧ x.val σ < 2 ^ x.k := by
  have h1 := BitVec.le_toInt (x.e.eval σ)
  have h2 := BitVec.toInt_lt (x := x.e.eval σ)
  simp only [Nat.add_sub_cancel] at h1 h2
  exact ⟨h1, h2⟩

theorem pow_mono {a b : Nat} (h : a ≤ b) : (2 : Int) ^ a ≤ 2 ^ b := by
  have := Nat.pow_le_pow_right (show 0 < 2 by decide) h
  have e1 : ((2 ^ a : Nat) : Int) = (2 : Int) ^ a := by simp [Int.natCast_pow]
  have e2 : ((2 ^ b : Nat) : Int) = (2 : Int) ^ b := by simp [Int.natCast_pow]
  rw [← e1, ← e2]; exact Int.ofNat_le.mpr this

theorem pow_pos' (a : Nat) : (0 : Int) < 2 ^ a := Int.pow_pos (by decide)

/-- `bmod` at `2^(k+1)` is the identity on `[-2^k, 2^k)`. -/
theorem bmod_id {x : Int} {k : Nat} (h1 : -2 ^ k ≤ x) (h2 : x < 2 ^ k) :
    x.bmod (2 ^ (k + 1)) = x := by
  apply Int.bmod_eq_of_le_mul_two
  · have : ((2 ^ (k + 1) : Nat) : Int) = 2 * 2 ^ k := by simp [Int.natCast_pow, Int.pow_succ]; omega
    omega
  · have : ((2 ^ (k + 1) : Nat) : Int) = 2 * 2 ^ k := by simp [Int.natCast_pow, Int.pow_succ]; omega
    omega

/-- Widen to `k+1` bits, `k ≥ x.k`: the value is unchanged. -/
def widen (x : SInt) (k : Nat) : SInt := ⟨k, sextE (k + 1) x.e⟩

theorem val_widen (σ : Asg) (x : SInt) {k : Nat} (h : x.k ≤ k) : (x.widen k).val σ = x.val σ := by
  simp [widen, val, eval_sextE, BitVec.toInt_signExtend_of_le (show x.k + 1 ≤ k + 1 by omega)]

def ofUint {w : Nat} (x : BVExpr w) : SInt := ⟨w, zextE (w + 1) x⟩

theorem val_ofUint (σ : Asg) {w : Nat} (x : BVExpr w) : (ofUint x).val σ = ((x.eval σ).toNat : Int) := by
  simp only [ofUint, val, eval_zextE, BitVec.toInt_setWidth]
  have hlt := (x.eval σ).isLt
  have e : ((x.eval σ).toNat : Int) < 2 ^ w := by
    have : ((2 ^ w : Nat) : Int) = (2 : Int) ^ w := by simp [Int.natCast_pow]
    omega
  have := bmod_id (x := ((x.eval σ).toNat : Int)) (k := w) (by have := pow_pos' w; omega) e
  rw [← this]
  simp [Int.natCast_pow]

/-- The signed reading of a `w`-bit vector (a zero-width one is 0). -/
def ofSint {w : Nat} (x : BVExpr w) : SInt := ⟨w - 1, sextE (w - 1 + 1) x⟩

theorem val_ofSint (σ : Asg) {w : Nat} (x : BVExpr w) : (ofSint x).val σ = (x.eval σ).toInt := by
  simp [ofSint, val, eval_sextE, BitVec.toInt_signExtend_of_le (show w ≤ w - 1 + 1 by omega)]

def add (x y : SInt) : SInt :=
  let k := max x.k y.k + 1
  ⟨k, .bin (sextE (k + 1) x.e) .add (sextE (k + 1) y.e)⟩

theorem val_add (σ : Asg) (x y : SInt) : (x.add y).val σ = x.val σ + y.val σ := by
  have hx := val_range σ x
  have hy := val_range σ y
  simp only [add, val, BVExpr.eval_bin, BVBinOp.eval, eval_sextE, BitVec.toInt_add,
    BitVec.toInt_signExtend_of_le (show x.k + 1 ≤ max x.k y.k + 1 + 1 by omega),
    BitVec.toInt_signExtend_of_le (show y.k + 1 ≤ max x.k y.k + 1 + 1 by omega)]
  simp only [val] at hx hy
  have m1 := pow_mono (show x.k ≤ max x.k y.k by omega)
  have m2 := pow_mono (show y.k ≤ max x.k y.k by omega)
  have e : (2 : Int) ^ (max x.k y.k + 1) = 2 * 2 ^ (max x.k y.k) := by rw [Int.pow_succ]; omega
  apply bmod_id <;> omega

def neg (x : SInt) : SInt :=
  ⟨x.k + 1, .bin (.un .not (sextE (x.k + 1 + 1) x.e)) .add (.const 1#(x.k + 1 + 1))⟩

theorem val_neg (σ : Asg) (x : SInt) : x.neg.val σ = - x.val σ := by
  have hx := val_range σ x
  simp only [neg, val, BVExpr.eval_bin, BVExpr.eval_un, BVBinOp.eval, BVUnOp.eval, eval_sextE,
    BVExpr.eval_const, ← BitVec.neg_eq_not_add, BitVec.toInt_neg,
    BitVec.toInt_signExtend_of_le (show x.k + 1 ≤ x.k + 1 + 1 by omega)]
  simp only [val] at hx
  have e : (2 : Int) ^ (x.k + 1) = 2 * 2 ^ x.k := by rw [Int.pow_succ]; omega
  apply bmod_id <;> omega

def sub (x y : SInt) : SInt := x.add y.neg

theorem val_sub (σ : Asg) (x y : SInt) : (x.sub y).val σ = x.val σ - y.val σ := by
  simp [sub, val_add, val_neg, Int.sub_eq_add_neg]

/-- The sign bit: `1` exactly when the value is negative. -/
def isNeg (x : SInt) : BVExpr 1 := .extract x.k 1 x.e

theorem bit_isNeg (σ : Asg) (x : SInt) : bitOf ((isNeg x).eval σ) = decide (x.val σ < 0) := by
  have h := BitVec.msb_eq_toInt (x := x.e.eval σ)
  rw [BitVec.msb_eq_getLsbD_last] at h
  simp only [Nat.add_sub_cancel] at h
  simp only [isNeg, bitOf, BVExpr.eval_extract, BitVec.getLsbD_extractLsb', Nat.add_zero,
    show (0 : Nat) < 1 by decide, decide_true, Bool.true_and, val]
  exact h

def lt (x y : SInt) : BVExpr 1 := isNeg (x.sub y)
def le (x y : SInt) : BVExpr 1 := notE (lt y x)

/-- Some bit among the low `n` of `e` is set. -/
def anyBitE {w : Nat} (e : BVExpr w) : Nat → BVExpr 1
  | 0 => falseE
  | n + 1 => orE (.extract n 1 e) (anyBitE e n)

theorem bit_anyBitE (σ : Asg) {w : Nat} (e : BVExpr w) :
    ∀ n, bitOf ((anyBitE e n).eval σ) = (List.range n).any fun i => (e.eval σ).getLsbD i
  | 0 => by simp [anyBitE, falseE, bitOf]
  | n + 1 => by
      rw [anyBitE, bit_orE, bit_anyBitE σ e n, List.range_succ, List.any_append]
      simp only [bitOf, BVExpr.eval_extract, BitVec.getLsbD_extractLsb', Nat.zero_lt_one, decide_true,
        Nat.add_zero, Bool.true_and, List.any_cons, List.any_nil, Bool.or_false]
      rw [Bool.or_comm]

/-- Equality of two exact integers: both sign-extended to one width, then
"no bit of the xor is set" -- one xor and an or-tree, where the difference
form (subtract, then test both signs) took three adders. -/
def eq (x y : SInt) : BVExpr 1 :=
  let k := max x.k y.k
  notE (anyBitE (.bin (sextE (k + 1) x.e) .xor (sextE (k + 1) y.e)) (k + 1))

theorem bit_lt (σ : Asg) (x y : SInt) : bitOf ((lt x y).eval σ) = decide (x.val σ < y.val σ) := by
  simp only [lt, bit_isNeg, val_sub]
  by_cases h : x.val σ < y.val σ <;> simp [h] <;> omega

theorem bit_le (σ : Asg) (x y : SInt) : bitOf ((le x y).eval σ) = decide (x.val σ ≤ y.val σ) := by
  simp only [le, bit_notE, bit_lt]
  by_cases h : x.val σ ≤ y.val σ <;> simp [h] <;> omega

theorem bit_eq (σ : Asg) (x y : SInt) : bitOf ((eq x y).eval σ) = decide (x.val σ = y.val σ) := by
  simp only [eq, bit_notE, bit_anyBitE, BVExpr.eval_bin, BVBinOp.eval_xor]
  have ha : ((sextE (max x.k y.k + 1) x.e).eval σ).toInt = x.val σ := by
    rw [eval_sextE, BitVec.toInt_signExtend_of_le (show x.k + 1 ≤ max x.k y.k + 1 by omega)]; rfl
  have hb : ((sextE (max x.k y.k + 1) y.e).eval σ).toInt = y.val σ := by
    rw [eval_sextE, BitVec.toInt_signExtend_of_le (show y.k + 1 ≤ max x.k y.k + 1 by omega)]; rfl
  generalize (sextE (max x.k y.k + 1) x.e).eval σ = a at ha ⊢
  generalize (sextE (max x.k y.k + 1) y.e).eval σ = b at hb ⊢
  rw [← ha, ← hb]
  by_cases hab : a = b
  · subst hab
    have : ((List.range (max x.k y.k + 1)).any fun i => (a ^^^ a).getLsbD i) = false := by simp
    rw [this]; simp
  · have hne : a.toInt ≠ b.toInt := fun h => hab (BitVec.toInt_inj.mp h)
    have : ((List.range (max x.k y.k + 1)).any fun i => (a ^^^ b).getLsbD i) = true := by
      rw [List.any_eq_true]
      apply Classical.byContradiction
      intro hno
      apply hab
      apply BitVec.eq_of_getLsbD_eq
      intro i hi
      have h : ¬ ((a ^^^ b).getLsbD i = true) := fun ht => hno ⟨i, List.mem_range.mpr hi, ht⟩
      rw [BitVec.getLsbD_xor] at h
      cases ha' : a.getLsbD i <;> cases hb' : b.getLsbD i <;> simp [ha', hb'] at h ⊢
    rw [this]; simp [hne]

/-- A literal, at a width that holds it. -/
def lit (z : Int) : SInt :=
  let k := z.natAbs.log2 + 1
  ⟨k, .const (BitVec.ofInt (k + 1) z)⟩

theorem val_lit (σ : Asg) (z : Int) : (lit z).val σ = z := by
  simp only [lit, val, BVExpr.eval_const, BitVec.toInt_ofInt]
  have h := @Nat.lt_log2_self z.natAbs
  have e : (2 : Int) ^ (z.natAbs.log2 + 1) = ((2 ^ (z.natAbs.log2 + 1) : Nat) : Int) := by
    simp [Int.natCast_pow]
  apply bmod_id
  · rw [e]; omega
  · rw [e]; omega

/-- `c ? x : y`, at the wider of the two widths. -/
def mux (c : BVExpr 1) (x y : SInt) : SInt :=
  let k := max x.k y.k
  ⟨k, muxE c (sextE (k + 1) x.e) (sextE (k + 1) y.e)⟩

theorem val_mux (σ : Asg) (c : BVExpr 1) (x y : SInt) :
    (mux c x y).val σ = if bitOf (c.eval σ) then x.val σ else y.val σ := by
  simp only [mux, val, eval_muxE]
  split <;> simp [eval_sextE, BitVec.toInt_signExtend_of_le (show x.k + 1 ≤ max x.k y.k + 1 by omega),
    BitVec.toInt_signExtend_of_le (show y.k + 1 ≤ max x.k y.k + 1 by omega)]

/-- `mk_bv w`'s reading of an exact integer: its low `w` bits. -/
def toBits (x : SInt) (w : Nat) : BVExpr w := sextE w x.e

theorem toBits_eq (σ : Asg) (x : SInt) (w : Nat) :
    (x.toBits w).eval σ = BitVec.ofInt w (x.val σ) := by
  simp only [toBits, eval_sextE, val]
  rfl

end SInt

end HDLProof.Sym
