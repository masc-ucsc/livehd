/-
  Operator specification for the certificate semantics.

  This file proves that `denote_op`, the certificate's reference meaning of each
  LGraph operator, is the standard bit-vector operation of Lean's core `BitVec`.
  The theorems quantify over all operand and result widths.
  Outside memory, a width hypothesis only ever names a common width `cw` that holds the operands
  (and, for mixed-width signed division, the result).
  The memory operators live only in `eval_op_cert`, so their theorems are stated there.
  The file does not import Mathlib.

  Encoding.  A sized value `x : BitVec w` enters the certificate as `enc x`.
  This is the same encoding as `OpBridge.bvenc`.
  `enc` is injective (`enc_inj`), so each theorem pins down one bit-vector.
  Each operand keeps its own width.  The result has the node width `w`.
  An operand list of mixed widths is a `List (Σ k, BitVec k)`.

  Standard.  Core `BitVec` follows SMT-LIB.
  On sized operands this is the IEEE 1800 2-state semantics.
  * An unsigned operand is zero-extended or truncated to the result width (`setWidth`).
  * Sum, Sub and Mult wrap modulo `2 ^ w`.
  * Unsigned division and unsigned comparison work at a common width `cw` that holds
    both operands.  The result is then resized to `w`.
  * Signed division and signed comparison sign-extend each operand from its own width
    (`signExtend`) to the common width.
  * Signed overflow wraps.  `MIN / -1` at the operand width is `MIN`.
  * SRA shifts at the operand width.  The result is then sign-extended or truncated to `w`.
  * Shift amounts at or past the width give all zero bits (SHL) or all sign bits (SRA).

  Values the certificate picks where IEEE 1800 gives X.
  * Division by zero (UDiv, Div, SDiv) gives 0.
  * A MuxN select past the last option gives 0.
  * A bit read past the top of an operand (GetMask, Sext, MemLoad) reads 0.
    IEEE 1800 reads X there for 4-state types and 0 for 2-state types.
  In each case a 2-state variable would store 0, which is the certificate's value.

  Operators with no IEEE 1800 counterpart are stated in core terms.
  * GetMask is a parallel bit extract (`sem_get_mask`).  A contiguous mask is a part-select.
  * SetMask is a parallel bit deposit into the first operand (`sem_set_mask`).
    A contiguous mask is a part-select assignment.
  * Ror is the reduction OR.
  * Sext sign-extends from a run-time bit position.
  * SHL with several amounts is the XOR of the shifted copies.
    The reference-only `sem_shl_many` ORs them instead.  The two differ when the copies
    overlap (`shl_xor_ne_or`).  The certificate gate `simpleOpCertWfBool` admits SHL only
    with two operands, so no certificate reaches this case today.
  * An operator given the wrong number of operands gives 0 (`default_spec`).

  The `#guard` checks at the end evaluate both sides on corner values.
  They add no axioms.
-/

import LeanSemanticPrimitives.Translation.LGraphModel
import LeanSemanticPrimitives.Projection.AxiomGate

namespace OpSpec

/-- Encode a sized bit-vector as a certificate value.  Same as `OpBridge.bvenc`. -/
def enc {w : Nat} (x : BitVec w) : BV := mk_bv w (Int.ofNat x.toNat)

/-- Encode one operand of a mixed-width list. -/
def encS (p : Σ k, BitVec k) : BV := enc p.2

--------------------------------------------------------------------------------
-- Encoding facts
--------------------------------------------------------------------------------

theorem pow_cast (w : Nat) : ((2 ^ w : Nat) : Int) = (2 : Int) ^ w := by
  rw [Int.natCast_pow]; rfl

theorem pow_pos' (w : Nat) : (0 : Int) < 2 ^ w := by
  rw [← pow_cast]; exact Int.ofNat_lt.mpr (Nat.two_pow_pos w)

theorem mk_bv_congr {w : Nat} {x y : Int} (h : x % 2 ^ w = y % 2 ^ w) :
    mk_bv w x = mk_bv w y := by
  unfold mk_bv; rw [h]

theorem mk_bv_emod (w : Nat) (x : Int) : mk_bv w (x % 2 ^ w) = mk_bv w x :=
  mk_bv_congr (Int.emod_emod _ _)

theorem toNat_lt_cast {w : Nat} (x : BitVec w) : ((x.toNat : Nat) : Int) < 2 ^ w := by
  rw [← pow_cast]; exact Int.ofNat_lt.mpr x.isLt

theorem toNat_emod_cast {w : Nat} (x : BitVec w) : ((x.toNat : Nat) : Int) % 2 ^ w = x.toNat :=
  Int.emod_eq_of_lt (Int.natCast_nonneg _) (toNat_lt_cast x)

theorem enc_eq {w : Nat} (x : BitVec w) : enc x = ⟨w, (x.toNat : Int)⟩ := by
  unfold enc mk_bv
  simp only [Int.ofNat_eq_natCast, toNat_emod_cast]

theorem enc_width {w : Nat} (x : BitVec w) : (enc x).width = w := rfl

theorem bv_uint_enc {w : Nat} (x : BitVec w) : bv_uint (enc x) = (x.toNat : Int) := by
  rw [enc_eq]; unfold bv_uint; exact toNat_emod_cast x

/-- The encoding is injective, so a certificate value determines the bit-vector. -/
theorem enc_inj {w : Nat} {x y : BitVec w} (h : enc x = enc y) : x = y := by
  rw [enc_eq, enc_eq] at h
  have := congrArg BV.value h
  simp only [Int.natCast_inj] at this
  exact BitVec.eq_of_toNat_eq this

/-- Every certificate word is the encoding of `BitVec.ofInt`. -/
theorem mk_bv_ofInt (w : Nat) (y : Int) : mk_bv w y = enc (BitVec.ofInt w y) := by
  have hnn : 0 ≤ y % 2 ^ w := Int.emod_nonneg _ (Int.ne_of_gt (pow_pos' w))
  unfold enc
  rw [BitVec.toNat_ofInt, pow_cast, Int.ofNat_eq_natCast, Int.toNat_of_nonneg hnn, mk_bv_emod]

theorem mk_bv_ofNat (w n : Nat) : mk_bv w (n : Int) = enc (BitVec.ofNat w n) := by
  rw [mk_bv_ofInt, BitVec.ofInt_natCast]

theorem enc_zero (w : Nat) : enc (0#w) = mk_bv w 0 := by
  rw [mk_bv_ofInt]; rfl

theorem ofInt_toNat {wa : Nat} (w : Nat) (a : BitVec wa) :
    BitVec.ofInt w (a.toNat : Int) = a.setWidth w := by
  rw [BitVec.ofInt_natCast, BitVec.ofNat_toNat]

theorem bv_resize_enc {wa : Nat} (w : Nat) (a : BitVec wa) :
    bv_resize w (enc a) = enc (a.setWidth w) := by
  unfold bv_resize; rw [bv_uint_enc, mk_bv_ofInt, ofInt_toNat]

/-- The certificate's signed value of an encoded word is `BitVec.toInt`. -/
theorem bv_sint_enc {w : Nat} (a : BitVec w) : bv_sint (enc a) = a.toInt := by
  unfold bv_sint
  simp only [enc_width, bv_uint_enc]
  rw [BitVec.toInt_eq_toNat_cond]
  rcases Nat.eq_zero_or_pos w with hw | hw
  · subst hw
    have : a.toNat = 0 := by have := a.isLt; omega
    simp [this]
  · rw [ite_eq_right (by omega)]
    have hpow : (2 : Nat) ^ w = 2 * 2 ^ (w - 1) := by
      rw [← Nat.pow_succ']; congr 1; omega
    have hc : ((a.toNat : Int) < 2 ^ (w - 1)) ↔ (2 * a.toNat < 2 ^ w) := by
      rw [← pow_cast, Int.ofNat_lt]; omega
    by_cases h : 2 * a.toNat < 2 ^ w
    · rw [ite_eq_left (hc.mpr h), ite_eq_left h]
    · rw [ite_eq_right (fun h' => h (hc.mp h')), ite_eq_right h, pow_cast]

--------------------------------------------------------------------------------
-- Bits of encoded values
--------------------------------------------------------------------------------

theorem bv_bit_eq_testBit (x : BV) (i : Nat) : bv_bit x i = (bv_uint x).toNat.testBit i := by
  unfold bv_bit
  have h := Nat.testBit_shiftRight (i := i) (j := 0) (bv_uint x).toNat
  rw [Nat.add_zero] at h
  rw [← h, Nat.testBit_zero]

theorem bv_bit_enc {w : Nat} (x : BitVec w) (i : Nat) : bv_bit (enc x) i = x.getLsbD i := by
  rw [bv_bit_eq_testBit, bv_uint_enc, Int.toNat_natCast, BitVec.testBit_toNat]

theorem bv_nonzero_enc {w : Nat} (x : BitVec w) : bv_nonzero (enc x) = decide (x ≠ 0#w) := by
  unfold bv_nonzero
  rw [bv_uint_enc]
  have : ((x.toNat : Int) ≠ 0) ↔ (x ≠ 0#w) := by
    rw [Ne, Ne, BitVec.toNat_eq, BitVec.toNat_ofNat, Nat.zero_mod]; omega
  exact decide_eq_decide.mpr this

--------------------------------------------------------------------------------
-- Words the certificate builds bit by bit (`bits_to_int`)
--------------------------------------------------------------------------------

theorem bits_to_int_succ (w : Nat) (f : Nat → Bool) :
    bits_to_int (w + 1) f = bits_to_int w f + (if f w then (2 : Int) ^ w else 0) := by
  unfold bits_to_int
  rw [List.range_succ, List.filterMap_append, List.sum_append]
  by_cases hf : f w <;> simp [hf]

/-- `bits_to_int` as a natural number. -/
def bitsNat : Nat → (Nat → Bool) → Nat
  | 0, _ => 0
  | w + 1, f => 2 ^ w * (f w).toNat + bitsNat w f

theorem bits_to_int_eq_bitsNat (w : Nat) (f : Nat → Bool) :
    bits_to_int w f = (bitsNat w f : Int) := by
  induction w with
  | zero => rfl
  | succ w ih =>
    rw [bits_to_int_succ, ih, bitsNat]
    cases f w <;> simp [Int.natCast_add] <;> omega

theorem bitsNat_lt (w : Nat) (f : Nat → Bool) : bitsNat w f < 2 ^ w := by
  induction w with
  | zero => simp [bitsNat]
  | succ w ih =>
    rw [bitsNat, Nat.pow_succ]
    cases f w <;> simp <;> omega

theorem bitsNat_testBit (w : Nat) (f : Nat → Bool) (j : Nat) :
    (bitsNat w f).testBit j = (decide (j < w) && f j) := by
  induction w generalizing j with
  | zero => simp [bitsNat]
  | succ w ih =>
    rw [bitsNat, Nat.testBit_two_pow_mul_add _ (bitsNat_lt w f)]
    by_cases hj : j < w
    · rw [ite_eq_left hj, ih]
      simp [hj, Nat.lt_succ_of_lt hj]
    · rw [ite_eq_right hj]
      by_cases hjw : j = w
      · subst hjw; cases f j <;> simp
      · have h1 : j - w ≠ 0 := by omega
        have h2 : ¬ j < w + 1 := by omega
        have h3 : Nat.testBit 1 (j - w) = false :=
          Nat.testBit_lt_two_pow (Nat.lt_of_lt_of_le (by decide : 1 < 2 ^ 1)
            (Nat.pow_le_pow_right (by decide) (by omega)))
        cases f w <;> simp [h2, h3]

theorem bitsNat_eq_toNat {w : Nat} (f : Nat → Bool) (X : BitVec w)
    (h : ∀ i, i < w → f i = X.getLsbD i) : bitsNat w f = X.toNat := by
  apply Nat.eq_of_testBit_eq
  intro j
  rw [bitsNat_testBit, BitVec.testBit_toNat]
  by_cases hj : j < w
  · simp [hj, h j hj]
  · simp [hj, BitVec.getLsbD_of_ge X j (by omega)]

/-- A word the certificate builds bit by bit is the bit-vector with those bits. -/
theorem mk_bv_bits {w : Nat} (f : Nat → Bool) (X : BitVec w)
    (h : ∀ i, i < w → f i = X.getLsbD i) : mk_bv w (bits_to_int w f) = enc X := by
  rw [bits_to_int_eq_bitsNat, bitsNat_eq_toNat f X h]; rfl

theorem bv_bitwise_enc {w : Nat} (f : Bool → Bool → Bool) (A B : BV) (Z : BitVec w)
    (h : ∀ i, i < w → f (bv_bit A i) (bv_bit B i) = Z.getLsbD i) :
    bv_bitwise w f A B = enc Z :=
  mk_bv_bits _ Z h

--------------------------------------------------------------------------------
-- From certificate integers to bit-vectors
--------------------------------------------------------------------------------

theorem mk_bv_eq_enc_iff {w : Nat} (V : Int) (X : BitVec w) :
    mk_bv w V = enc X ↔ BitVec.ofInt w V = X := by
  rw [mk_bv_ofInt]
  exact ⟨enc_inj, fun h => h ▸ rfl⟩

theorem ofInt_of_mk_bv {w : Nat} {V : Int} {X : BitVec w} (h : mk_bv w V = enc X) :
    BitVec.ofInt w V = X := (mk_bv_eq_enc_iff V X).mp h

theorem mk_bv_bool (w : Nat) (P : Prop) [Decidable P] :
    mk_bv w (if P then 1 else 0) = enc ((BitVec.ofBool (decide P)).setWidth w) := by
  rw [mk_bv_ofInt]
  congr 1
  by_cases hP : P
  · simp only [hP, ite_true, decide_true]
    rw [← BitVec.ofNat_toNat, BitVec.toNat_ofBool]; rfl
  · simp only [hP, ite_false, decide_false]
    rw [← BitVec.ofNat_toNat, BitVec.toNat_ofBool]; rfl

theorem mk_bv_boolB (w : Nat) (c : Bool) :
    mk_bv w (if c then 1 else 0) = enc ((BitVec.ofBool c).setWidth w) := by
  have := mk_bv_bool w (c = true)
  rwa [Bool.decide_eq_true] at this

--------------------------------------------------------------------------------
-- Op_Const, Op_Sum, Op_Sub, Op_Mult
--------------------------------------------------------------------------------

/-- `Op_Const c` is `c` wrapped to `w`, whatever its operands. -/
theorem const_spec (c : Int) (w : Nat) (args : List BV) :
    denote_op (LGraphOp.Op_Const c) w args = enc (BitVec.ofInt w c) := by
  simp only [denote_op]; exact mk_bv_ofInt w c

/-- Wrapping sum of bit-vectors of one width. -/
def bvSum {w : Nat} : List (BitVec w) → BitVec w
  | [] => 0#w
  | x :: xs => x + bvSum xs

/-- Wrapping product of bit-vectors of one width; the empty product is 1. -/
def bvProd {w : Nat} : List (BitVec w) → BitVec w
  | [] => 1#w
  | x :: xs => x * bvProd xs

/-- Every operand of a mixed-width list resized (zero-extended or truncated) to `w`. -/
def resizeAll (w : Nat) (l : List (Σ k, BitVec k)) : List (BitVec w) :=
  l.map fun p => p.2.setWidth w

theorem map_uint_encS (l : List (Σ k, BitVec k)) :
    (l.map encS).map bv_uint = l.map fun p => (p.2.toNat : Int) := by
  rw [List.map_map]; apply List.map_congr_left; intro p _; exact bv_uint_enc p.2

theorem ofInt_sum (w : Nat) (l : List (Σ k, BitVec k)) :
    BitVec.ofInt w (l.map fun p => (p.2.toNat : Int)).sum = bvSum (resizeAll w l) := by
  induction l with
  | nil => rfl
  | cons p l ih =>
    simp only [List.map_cons, List.sum_cons, resizeAll, bvSum]
    rw [BitVec.ofInt_add, ofInt_toNat, ih]; rfl

theorem ofInt_prod (w : Nat) (l : List (Σ k, BitVec k)) :
    BitVec.ofInt w (l.map fun p => (p.2.toNat : Int)).prod = bvProd (resizeAll w l) := by
  induction l with
  | nil => rfl
  | cons p l ih =>
    simp only [List.map_cons, List.prod_cons, resizeAll, bvProd]
    rw [BitVec.ofInt_mul, ofInt_toNat, ih]; rfl

/-- `Op_Sum n`: the first `n` operands are added and the rest subtracted, each resized to
`w`, modulo `2 ^ w`. -/
theorem sum_spec (n w : Nat) (args : List (Σ k, BitVec k)) :
    denote_op (LGraphOp.Op_Sum n) w (args.map encS)
      = enc (bvSum (resizeAll w (args.take n)) - bvSum (resizeAll w (args.drop n))) := by
  simp only [denote_op, map_uint_encS]
  rw [← List.map_take, ← List.map_drop, mk_bv_ofInt, Int.sub_eq_add_neg, BitVec.ofInt_add,
    BitVec.ofInt_neg, ofInt_sum, ofInt_sum, BitVec.sub_eq_add_neg]

/-- Binary add. -/
theorem sum2_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op (LGraphOp.Op_Sum 2) w [enc a, enc b] = enc (a.setWidth w + b.setWidth w) := by
  have := sum_spec 2 w [⟨wa, a⟩, ⟨wb, b⟩]
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]
  simp [bvSum, resizeAll]

/-- Binary subtract written as `Op_Sum 1`. -/
theorem sum1_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op (LGraphOp.Op_Sum 1) w [enc a, enc b] = enc (a.setWidth w - b.setWidth w) := by
  have := sum_spec 1 w [⟨wa, a⟩, ⟨wb, b⟩]
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]
  simp [bvSum, resizeAll]

/-- `Op_Sub`: wrapping subtraction at the result width. -/
theorem sub_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_Sub w [enc a, enc b] = enc (a.setWidth w - b.setWidth w) := by
  simp only [denote_op, bv_uint_enc]
  rw [mk_bv_ofInt, Int.sub_eq_add_neg, BitVec.ofInt_add, BitVec.ofInt_neg, ofInt_toNat,
    ofInt_toNat, BitVec.sub_eq_add_neg]

/-- `Op_Mult`: wrapping product at the result width.  The empty product is 1. -/
theorem mult_spec (w : Nat) (args : List (Σ k, BitVec k)) :
    denote_op LGraphOp.Op_Mult w (args.map encS) = enc (bvProd (resizeAll w args)) := by
  simp only [denote_op, map_uint_encS]
  rw [mk_bv_ofInt, ofInt_prod]

/-- Binary multiply. -/
theorem mult2_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_Mult w [enc a, enc b] = enc (a.setWidth w * b.setWidth w) := by
  have := mult_spec w [⟨wa, a⟩, ⟨wb, b⟩]
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]
  simp [bvProd, resizeAll]

--------------------------------------------------------------------------------
-- Op_UDiv, Op_Div, Op_SDiv
--------------------------------------------------------------------------------

theorem intAbs_toNat (x : Int) : (intAbs x).toNat = x.natAbs := by
  unfold intAbs; split <;> omega

/-- The certificate's `trunc_div_int` is core `Int.tdiv` (rounds toward zero, `x / 0 = 0`). -/
theorem trunc_div_int_eq_tdiv (x y : Int) : trunc_div_int x y = x.tdiv y := by
  have hq : ∀ an bn : Nat, (if bn = 0 then 0 else an / bn) = an / bn := by
    intro an bn; split <;> simp_all
  unfold trunc_div_int
  simp only [intAbs_toNat, hq]
  rcases x with m | m <;> rcases y with n | n
  · have h1 : ¬ (Int.ofNat m < 0) := by simp only [Int.ofNat_eq_natCast]; omega
    have h2 : ¬ (Int.ofNat n < 0) := by simp only [Int.ofNat_eq_natCast]; omega
    simp only [h1, h2, ne_eq, not_true_eq_false, ite_false]
    rfl
  · have h1 : ¬ (Int.ofNat m < 0) := by simp only [Int.ofNat_eq_natCast]; omega
    have h2 : Int.negSucc n < 0 := Int.negSucc_lt_zero n
    simp only [h1, h2, ne_eq, Int.natAbs_negSucc]
    simp; rfl
  · have h1 : Int.negSucc m < 0 := Int.negSucc_lt_zero m
    have h2 : ¬ (Int.ofNat n < 0) := by simp only [Int.ofNat_eq_natCast]; omega
    simp only [h1, h2, Int.natAbs_negSucc]
    simp; rfl
  · have h1 : Int.negSucc m < 0 := Int.negSucc_lt_zero m
    have h2 : Int.negSucc n < 0 := Int.negSucc_lt_zero n
    simp only [h1, h2, Int.natAbs_negSucc]
    simp; rfl

theorem toNat_setWidth_le {wa cw : Nat} (a : BitVec wa) (h : wa ≤ cw) :
    (a.setWidth cw).toNat = a.toNat := BitVec.toNat_setWidth_of_le h

theorem setWidth_ofInt_of_le {w cw : Nat} (T : Int) (h : w ≤ cw) :
    (BitVec.ofInt cw T).setWidth w = BitVec.ofInt w T := by
  apply BitVec.eq_of_toNat_eq
  rw [BitVec.toNat_setWidth, BitVec.toNat_ofInt, BitVec.toNat_ofInt]
  have hc : (0 : Int) < ((2 ^ cw : Nat) : Int) := by rw [pow_cast]; exact pow_pos' cw
  have hnn : 0 ≤ T % ((2 ^ cw : Nat) : Int) := Int.emod_nonneg _ (Int.ne_of_gt hc)
  have hw : (((2 ^ w : Nat) : Int)).toNat = 2 ^ w := Int.toNat_natCast _
  rw [← hw, ← Int.toNat_emod hnn (Int.natCast_nonneg _), hw]
  rw [Int.emod_emod_of_dvd _ (Int.natCast_dvd_natCast.mpr (Nat.pow_dvd_pow 2 h))]

/-- The unsigned quotient the certificate computes, with its divisor-0 guard. -/
theorem uint_quot (a b : Nat) :
    (if (b : Int) = 0 then 0 else (a : Int) / (b : Int)) = ((a / b : Nat) : Int) := by
  by_cases hb : b = 0
  · subst hb; simp
  · rw [ite_eq_right (by omega), Int.natCast_ediv]

/-- `Op_UDiv` for every width: the natural-number quotient, wrapped to `w`.
Division by zero gives 0, as `Nat` division does. -/
theorem udiv_spec_nat {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_UDiv w [enc a, enc b] = enc (BitVec.ofNat w (a.toNat / b.toNat)) := by
  simp only [denote_op, bv_uint_enc]
  rw [uint_quot, mk_bv_ofNat]

/-- `Op_Div` has the same body as `Op_UDiv`. -/
theorem div_spec_nat {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_Div w [enc a, enc b] = enc (BitVec.ofNat w (a.toNat / b.toNat)) := by
  simp only [denote_op, bv_uint_enc]
  rw [uint_quot, mk_bv_ofNat]

theorem udiv_common {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    BitVec.ofNat w (a.toNat / b.toNat) = ((a.setWidth cw) / (b.setWidth cw)).setWidth w := by
  rw [← BitVec.ofNat_toNat, BitVec.toNat_udiv, toNat_setWidth_le a ha, toNat_setWidth_le b hb]

/-- `Op_UDiv` is core unsigned division at any common width `cw` that holds both operands,
resized to `w`.  Core `x / 0 = 0`. -/
theorem udiv_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_UDiv w [enc a, enc b]
      = enc (((a.setWidth cw) / (b.setWidth cw)).setWidth w) := by
  rw [udiv_spec_nat, udiv_common w a b ha hb]

/-- `Op_Div` is core unsigned division, as `Op_UDiv`. -/
theorem div_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_Div w [enc a, enc b]
      = enc (((a.setWidth cw) / (b.setWidth cw)).setWidth w) := by
  rw [div_spec_nat, udiv_common w a b ha hb]

/-- Division by zero.  IEEE 1800 gives X.  The certificate picks 0. -/
theorem udiv_by_zero {wa wb : Nat} (w : Nat) (a : BitVec wa) :
    denote_op LGraphOp.Op_UDiv w [enc a, enc (0#wb)] = enc (0#w) := by
  rw [udiv_spec_nat]; simp

/-- Division by zero.  IEEE 1800 gives X.  The certificate picks 0. -/
theorem div_by_zero {wa wb : Nat} (w : Nat) (a : BitVec wa) :
    denote_op LGraphOp.Op_Div w [enc a, enc (0#wb)] = enc (0#w) := by
  rw [div_spec_nat]; simp

theorem sdiv_eq_ofInt_tdiv {w : Nat} (x y : BitVec w) :
    x.sdiv y = BitVec.ofInt w (x.toInt.tdiv y.toInt) := by
  apply BitVec.eq_of_toInt_eq
  rw [BitVec.toInt_ofInt, BitVec.toInt_sdiv]

/-- `Op_SDiv` for every width: the truncated quotient of the signed operand values,
wrapped to `w`.  Divisor 0 gives 0. -/
theorem sdiv_spec_int {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_SDiv w [enc a, enc b] = enc (BitVec.ofInt w (a.toInt.tdiv b.toInt)) := by
  show bv_sdiv w (enc a) (enc b) = _
  unfold bv_sdiv
  rw [bv_uint_enc, bv_sint_enc, bv_sint_enc, trunc_div_int_eq_tdiv, mk_bv_ofInt]
  by_cases hb : (b.toNat : Int) = 0
  · have hb0 : b = 0#wb := BitVec.eq_of_toNat_eq (by simp only [BitVec.toNat_ofNat, Nat.zero_mod]; omega)
    rw [ite_eq_left hb, hb0, BitVec.toInt_zero, Int.tdiv_zero]
  · rw [ite_eq_right hb]

/-- `Op_SDiv` at one width is core `BitVec.sdiv`.  This covers `MIN / -1 = MIN` and divisor 0. -/
theorem sdiv_spec {w : Nat} (a b : BitVec w) :
    denote_op LGraphOp.Op_SDiv w [enc a, enc b] = enc (a.sdiv b) := by
  rw [sdiv_spec_int, sdiv_eq_ofInt_tdiv]

/-- `Op_SDiv` with mixed widths: sign-extend both operands to a common width `cw` that holds
the operands and the result, divide, and truncate to `w`.  This is IEEE 1800's
context-determined signed division. -/
theorem sdiv_spec_common {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) (hw : w ≤ cw) :
    denote_op LGraphOp.Op_SDiv w [enc a, enc b]
      = enc (((a.signExtend cw).sdiv (b.signExtend cw)).setWidth w) := by
  rw [sdiv_spec_int, sdiv_eq_ofInt_tdiv, BitVec.toInt_signExtend_of_le ha,
    BitVec.toInt_signExtend_of_le hb, setWidth_ofInt_of_le _ hw]

/-- Signed division by zero.  IEEE 1800 gives X.  The certificate picks 0. -/
theorem sdiv_by_zero {wa wb : Nat} (w : Nat) (a : BitVec wa) :
    denote_op LGraphOp.Op_SDiv w [enc a, enc (0#wb)] = enc (0#w) := by
  rw [sdiv_spec_int, BitVec.toInt_zero, Int.tdiv_zero]; rfl

/-- Signed overflow wraps: `MIN / -1 = MIN` at the operand width. -/
theorem sdiv_intMin_neg_one (w : Nat) :
    denote_op LGraphOp.Op_SDiv w [enc (BitVec.intMin w), enc (-1#w)] = enc (BitVec.intMin w) := by
  rw [sdiv_spec, BitVec.intMin_sdiv_neg_one]

--------------------------------------------------------------------------------
-- Op_And, Op_Or, Op_Xor, Op_Not, Op_Ror
--------------------------------------------------------------------------------

/-- One step of a certificate bitwise fold, when the accumulator is encoded and the
next operand agrees bit by bit (below `w`) with a bit-vector `Y`. -/
theorem bv_bitwise_step {w : Nat} (f : Bool → Bool → Bool) (g : BitVec w → BitVec w → BitVec w)
    (hg : ∀ x y : BitVec w, ∀ i, i < w → (g x y).getLsbD i = f (x.getLsbD i) (y.getLsbD i))
    (acc Y : BitVec w) (B : BV) (hB : ∀ i, i < w → bv_bit B i = Y.getLsbD i) :
    bv_bitwise w f (enc acc) B = enc (g acc Y) := by
  apply bv_bitwise_enc
  intro i hi
  rw [bv_bit_enc, hB i hi, hg acc Y i hi]

theorem foldl_bitwise {α : Type} {w : Nat} (f : Bool → Bool → Bool)
    (g : BitVec w → BitVec w → BitVec w)
    (hg : ∀ x y : BitVec w, ∀ i, i < w → (g x y).getLsbD i = f (x.getLsbD i) (y.getLsbD i))
    (F : α → BV) (T : α → BitVec w) (hT : ∀ x, ∀ i, i < w → bv_bit (F x) i = (T x).getLsbD i) :
    ∀ (l : List α) (acc : BitVec w),
      l.foldl (fun acc x => bv_bitwise w f acc (F x)) (enc acc) = enc ((l.map T).foldl g acc) := by
  intro l
  induction l with
  | nil => intro acc; rfl
  | cons x l ih =>
    intro acc
    simp only [List.foldl_cons, List.map_cons]
    rw [bv_bitwise_step f g hg acc (T x) (F x) (hT x), ih]

theorem bv_bit_encS_setWidth (w : Nat) (p : Σ k, BitVec k) (i : Nat) (hi : i < w) :
    bv_bit (encS p) i = (p.2.setWidth w).getLsbD i := by
  unfold encS
  rw [bv_bit_enc, BitVec.getLsbD_setWidth]; simp [hi]

/-- `Op_And`: the first operand resized to `w`, then AND with each further operand resized
to `w`. -/
theorem and_spec (w : Nat) (a : Σ k, BitVec k) (rest : List (Σ k, BitVec k)) :
    denote_op LGraphOp.Op_And w ((a :: rest).map encS)
      = enc ((resizeAll w rest).foldl (· &&& ·) (a.2.setWidth w)) := by
  simp only [List.map_cons, denote_op, encS]
  rw [bv_resize_enc, List.foldl_map]
  exact foldl_bitwise _ _ (fun x y i _ => BitVec.getLsbD_and) encS _
    (bv_bit_encS_setWidth w) rest _

/-- `Op_And` of no operands is 0 (not all ones). -/
theorem and_nil_spec (w : Nat) : denote_op LGraphOp.Op_And w [] = enc (0#w) := by
  simp only [denote_op, enc_zero]

/-- `Op_Or`: OR of all operands resized to `w`.  No operands give 0. -/
theorem or_spec (w : Nat) (args : List (Σ k, BitVec k)) :
    denote_op LGraphOp.Op_Or w (args.map encS) = enc ((resizeAll w args).foldl (· ||| ·) 0#w) := by
  simp only [denote_op]
  rw [← enc_zero, List.foldl_map]
  exact foldl_bitwise _ _ (fun x y i _ => BitVec.getLsbD_or) encS _
    (bv_bit_encS_setWidth w) args _

/-- `Op_Xor`: XOR of all operands resized to `w`.  No operands give 0. -/
theorem xor_spec (w : Nat) (args : List (Σ k, BitVec k)) :
    denote_op LGraphOp.Op_Xor w (args.map encS) = enc ((resizeAll w args).foldl (· ^^^ ·) 0#w) := by
  simp only [denote_op]
  rw [← enc_zero, List.foldl_map]
  exact foldl_bitwise _ _ (fun x y i _ => BitVec.getLsbD_xor) encS _
    (bv_bit_encS_setWidth w) args _

/-- Binary AND. -/
theorem and2_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_And w [enc a, enc b] = enc (a.setWidth w &&& b.setWidth w) :=
  and_spec w ⟨wa, a⟩ [⟨wb, b⟩]

/-- Binary OR. -/
theorem or2_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_Or w [enc a, enc b] = enc (a.setWidth w ||| b.setWidth w) := by
  have := or_spec w [⟨wa, a⟩, ⟨wb, b⟩]
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]; simp [resizeAll]

/-- Binary XOR. -/
theorem xor2_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_Xor w [enc a, enc b] = enc (a.setWidth w ^^^ b.setWidth w) := by
  have := xor_spec w [⟨wa, a⟩, ⟨wb, b⟩]
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]; simp [resizeAll]

/-- `Op_Not`: bitwise NOT of the operand resized to `w`. -/
theorem not_spec {wa : Nat} (w : Nat) (a : BitVec wa) :
    denote_op LGraphOp.Op_Not w [enc a] = enc (~~~(a.setWidth w)) := by
  show bv_not w (enc a) = _
  apply mk_bv_bits
  intro i hi
  rw [bv_bit_enc, BitVec.getLsbD_not, BitVec.getLsbD_setWidth]; simp [hi]

/-- `Op_Ror`: reduction OR over all operands (1 if any operand is nonzero), resized to `w`. -/
theorem ror_spec (w : Nat) (args : List (Σ k, BitVec k)) :
    denote_op LGraphOp.Op_Ror w (args.map encS)
      = enc ((BitVec.ofBool (args.any fun p => decide (p.2 ≠ 0#p.1))).setWidth w) := by
  simp only [denote_op]
  have h : (args.map encS).any bv_nonzero = args.any fun p => decide (p.2 ≠ 0#p.1) := by
    rw [List.any_map]; congr 1; funext p; exact bv_nonzero_enc p.2
  rw [h, mk_bv_boolB]

--------------------------------------------------------------------------------
-- Comparisons: Op_LT, Op_ULT, Op_GT, Op_UGT, Op_SLT, Op_SGT, Op_EQ
--------------------------------------------------------------------------------

/-- `Op_ULT`: unsigned less-than at any common width that holds both operands.  The 1-bit
result is resized to `w`. -/
theorem ult_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_ULT w [enc a, enc b]
      = enc ((BitVec.ofBool ((a.setWidth cw).ult (b.setWidth cw))).setWidth w) := by
  simp only [denote_op, bv_uint_enc]
  rw [mk_bv_bool, BitVec.ult_eq_decide, toNat_setWidth_le a ha, toNat_setWidth_le b hb]
  simp only [Int.ofNat_lt]

/-- `Op_LT` is unsigned, like `Op_ULT`. -/
theorem lt_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_LT w [enc a, enc b]
      = enc ((BitVec.ofBool ((a.setWidth cw).ult (b.setWidth cw))).setWidth w) := by
  rw [← ult_spec w a b ha hb]; rfl

/-- `Op_UGT`: unsigned greater-than, as `b < a`. -/
theorem ugt_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_UGT w [enc a, enc b]
      = enc ((BitVec.ofBool ((b.setWidth cw).ult (a.setWidth cw))).setWidth w) := by
  simp only [denote_op, bv_uint_enc]
  rw [mk_bv_bool, BitVec.ult_eq_decide, toNat_setWidth_le a ha, toNat_setWidth_le b hb]
  simp only [gt_iff_lt, Int.ofNat_lt]

/-- `Op_GT` is unsigned, like `Op_UGT`. -/
theorem gt_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_GT w [enc a, enc b]
      = enc ((BitVec.ofBool ((b.setWidth cw).ult (a.setWidth cw))).setWidth w) := by
  rw [← ugt_spec w a b ha hb]; rfl

/-- `Op_SLT`: each operand is signed at its own width and sign-extended to a common width. -/
theorem slt_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_SLT w [enc a, enc b]
      = enc ((BitVec.ofBool ((a.signExtend cw).slt (b.signExtend cw))).setWidth w) := by
  simp only [denote_op, bv_sint_enc]
  rw [mk_bv_bool, BitVec.slt_eq_decide, BitVec.toInt_signExtend_of_le ha,
    BitVec.toInt_signExtend_of_le hb]

/-- `Op_SGT`: signed greater-than, as `b < a` signed. -/
theorem sgt_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_SGT w [enc a, enc b]
      = enc ((BitVec.ofBool ((b.signExtend cw).slt (a.signExtend cw))).setWidth w) := by
  simp only [denote_op, bv_sint_enc]
  rw [mk_bv_bool, BitVec.slt_eq_decide, BitVec.toInt_signExtend_of_le ha,
    BitVec.toInt_signExtend_of_le hb]

theorem setWidth_eq_iff {wa wb cw : Nat} (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    a.setWidth cw = b.setWidth cw ↔ (a.toNat : Int) = (b.toNat : Int) := by
  rw [BitVec.toNat_eq, toNat_setWidth_le a ha, toNat_setWidth_le b hb, Int.natCast_inj]

/-- `Op_EQ`: 1 when every further operand equals the first, all zero-extended to a common
width. -/
theorem eq_spec {cw : Nat} (w : Nat) (a : Σ k, BitVec k) (rest : List (Σ k, BitVec k))
    (h : ∀ p, p ∈ a :: rest → p.1 ≤ cw) :
    denote_op LGraphOp.Op_EQ w ((a :: rest).map encS)
      = enc ((BitVec.ofBool (rest.all fun p => decide (p.2.setWidth cw = a.2.setWidth cw))).setWidth w) := by
  simp only [List.map_cons, denote_op]
  have hall : ((rest.map encS).all fun b => decide (bv_uint b = bv_uint (encS a)))
      = rest.all fun p => decide (p.2.setWidth cw = a.2.setWidth cw) := by
    rw [List.all_map, Bool.eq_iff_iff, List.all_eq_true, List.all_eq_true]
    apply forall_congr'; intro p
    apply imp_congr_right; intro hp
    simp only [Function.comp_apply, decide_eq_true_eq, encS, bv_uint_enc]
    exact (setWidth_eq_iff p.2 a.2 (h p (List.mem_cons_of_mem _ hp)) (h a List.mem_cons_self)).symm
  rw [hall, mk_bv_boolB]

/-- `Op_EQ` of no operands is 1. -/
theorem eq_nil_spec (w : Nat) :
    denote_op LGraphOp.Op_EQ w [] = enc ((BitVec.ofBool true).setWidth w) := by
  simp only [denote_op]
  rw [← mk_bv_boolB]; rfl

/-- Binary equality. -/
theorem eq2_spec {wa wb cw : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (ha : wa ≤ cw) (hb : wb ≤ cw) :
    denote_op LGraphOp.Op_EQ w [enc a, enc b]
      = enc ((BitVec.ofBool (decide (a.setWidth cw = b.setWidth cw))).setWidth w) := by
  have := eq_spec (cw := cw) w ⟨wa, a⟩ [⟨wb, b⟩] (by
    intro p hp
    simp only [List.mem_cons, List.not_mem_nil, or_false] at hp
    rcases hp with rfl | rfl <;> assumption)
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]
  simp only [List.all_cons, List.all_nil, Bool.and_true]
  rw [show decide (b.setWidth cw = a.setWidth cw) = decide (a.setWidth cw = b.setWidth cw) from
    decide_eq_decide.mpr eq_comm]

--------------------------------------------------------------------------------
-- Op_SHL
--------------------------------------------------------------------------------

/-- One `Op_SHL` fold term.  The certificate's guard at `k ≥ w` changes no value. -/
theorem shl_term {wa : Nat} (w : Nat) (a : BitVec wa) (k : Nat) :
    mk_bv w (if k ≥ w then 0 else (a.toNat : Int) * 2 ^ k) = enc (a.setWidth w <<< k) := by
  rw [mk_bv_eq_enc_iff]
  by_cases hk : k ≥ w
  · rw [ite_eq_left hk, BitVec.shiftLeft_eq_zero hk]; rfl
  · rw [ite_eq_right hk, ← pow_cast, ← Int.natCast_mul, BitVec.ofInt_natCast]
    apply BitVec.eq_of_toNat_eq
    rw [BitVec.toNat_ofNat, BitVec.toNat_shiftLeft, BitVec.toNat_setWidth, Nat.shiftLeft_eq,
      Nat.mod_mul_mod]

/-- `Op_SHL a :: bs`: XOR of the copies of `a` (resized to `w`) shifted left by each amount. -/
theorem shl_spec {wa : Nat} (w : Nat) (a : BitVec wa) (bs : List (Σ k, BitVec k)) :
    denote_op LGraphOp.Op_SHL w (enc a :: bs.map encS)
      = enc ((bs.map fun p => a.setWidth w <<< p.2.toNat).foldl (· ^^^ ·) 0#w) := by
  simp only [denote_op]
  rw [← enc_zero, List.foldl_map]
  refine foldl_bitwise _ _ (fun x y i _ => BitVec.getLsbD_xor) _ _ ?_ bs _
  intro p i _
  simp only [encS, bv_uint_enc, Int.toNat_natCast]
  rw [shl_term, bv_bit_enc]

/-- `Op_SHL` with no operands is 0. -/
theorem shl_nil_spec (w : Nat) : denote_op LGraphOp.Op_SHL w [] = enc (0#w) := by
  simp only [denote_op, enc_zero]

/-- `Op_SHL` with one amount is core shift left at the result width. -/
theorem shl1_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_SHL w [enc a, enc b] = enc (a.setWidth w <<< b.toNat) := by
  have := shl_spec w a [⟨wb, b⟩]
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]
  simp only [List.foldl_cons, List.foldl_nil, BitVec.zero_xor]

/-- An amount at or past the result width shifts every bit out. -/
theorem shl1_amount_ge {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (h : w ≤ b.toNat) :
    denote_op LGraphOp.Op_SHL w [enc a, enc b] = enc (0#w) := by
  rw [shl1_spec, BitVec.shiftLeft_eq_zero h]

/-- `Op_SHL` with two amounts is the XOR of the two shifted copies. -/
theorem shl2_spec {wa wb wc : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) (c : BitVec wc) :
    denote_op LGraphOp.Op_SHL w [enc a, enc b, enc c]
      = enc ((a.setWidth w <<< b.toNat) ^^^ (a.setWidth w <<< c.toNat)) := by
  have := shl_spec w a [⟨wb, b⟩, ⟨wc, c⟩]
  simp only [List.map_cons, List.map_nil, encS] at this
  rw [this]
  simp only [List.foldl_cons, List.foldl_nil, BitVec.zero_xor]

/-- Disagreement with the reference-only `sem_shl_many`.  With two amounts the certificate
XORs the shifted copies, while `sem_shl_many` ORs them.  Here `3 <<< 0 = 0011` and
`3 <<< 1 = 0110` overlap: the certificate gives `0101` and `sem_shl_many` gives `0111`.
The gate `simpleOpCertWfBool` admits `Op_SHL` only with two operands (one amount). -/
theorem shl_xor_ne_or :
    denote_op LGraphOp.Op_SHL 4 [enc (3#4), enc (0#2), enc (1#2)]
      ≠ enc (sem_shl_many (3#4) [0#2, 1#2]) := by
  rw [shl2_spec]
  intro h
  exact absurd (enc_inj h) (by decide)

--------------------------------------------------------------------------------
-- Op_SRA
--------------------------------------------------------------------------------

theorem neg_one_ediv {x d : Int} (hd : 0 < d) (hlo : -d ≤ x) (hhi : x < 0) : x / d = -1 := by
  have h := (Int.ediv_emod_unique (a := x) (b := d) (r := x + d) (q := -1) hd).mpr
    ⟨by rw [Int.mul_neg, Int.mul_one]; omega, by omega, by omega⟩
  exact h.1

theorem toInt_zero_width (a : BitVec 0) : a.toInt = 0 := by
  have : a = 0#0 := BitVec.eq_of_toNat_eq (by have := a.isLt; simp at this ⊢; omega)
  rw [this, BitVec.toInt_zero]

/-- The certificate's saturation guard for `Op_SRA` changes no value on encoded operands:
the result is the floor quotient by `2 ^ amount`. -/
theorem sra_guard {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    bv_sra w (enc a) (enc b) = mk_bv w (a.toInt / 2 ^ b.toNat) := by
  unfold bv_sra
  simp only [enc_width, bv_uint_enc, Int.toNat_natCast, bv_sint_enc]
  by_cases hk : b.toNat ≥ wa
  · rw [ite_eq_left hk]
    apply mk_bv_congr
    congr 1
    have hd : (0 : Int) < 2 ^ b.toNat := pow_pos' _
    have hpow : (2 : Int) ^ (wa - 1) ≤ 2 ^ b.toNat := by
      rw [← pow_cast, ← pow_cast, Int.ofNat_le]
      exact Nat.pow_le_pow_right (by decide) (by omega)
    have hlt : a.toInt < 2 ^ (wa - 1) := BitVec.toInt_lt
    have hge : -2 ^ (wa - 1) ≤ a.toInt := BitVec.le_toInt a
    by_cases hw : wa = 0
    · subst hw
      rw [ite_eq_left rfl, toInt_zero_width, Int.zero_ediv]
    · rw [ite_eq_right hw]
      by_cases hs : a.toInt ≥ 0
      · rw [ite_eq_left hs, Int.ediv_eq_zero_of_lt hs (by omega)]
      · rw [ite_eq_right hs, neg_one_ediv hd (by omega) (by omega)]
  · rw [ite_eq_right hk]

theorem signExtend_eq_ofInt {wa : Nat} (w : Nat) (x : BitVec wa) :
    x.signExtend w = BitVec.ofInt w x.toInt := rfl

/-- `Op_SRA`: arithmetic shift right at the operand width, then sign-extended or truncated
to `w`.  One statement covers `w ≤ wa` and `w > wa`. -/
theorem sra_spec {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) :
    denote_op LGraphOp.Op_SRA w [enc a, enc b] = enc ((a.sshiftRight b.toNat).signExtend w) := by
  show bv_sra w (enc a) (enc b) = _
  rw [sra_guard, mk_bv_ofInt, signExtend_eq_ofInt, BitVec.toInt_sshiftRight,
    Int.shiftRight_eq_div_pow, pow_cast]

theorem signExtend_eq_setWidth_of_le {wa : Nat} (w : Nat) (x : BitVec wa) (h : w ≤ wa) :
    x.signExtend w = x.setWidth w := by
  rw [signExtend_eq_ofInt, ← setWidth_ofInt_of_le _ h, BitVec.ofInt_toInt]

/-- A result no wider than the operand is the shifted value truncated to `w`. -/
theorem sra_spec_narrow {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) (h : w ≤ wa) :
    denote_op LGraphOp.Op_SRA w [enc a, enc b] = enc ((a.sshiftRight b.toNat).setWidth w) := by
  rw [sra_spec, signExtend_eq_setWidth_of_le w _ h]

/-- A result at least as wide as the operand: sign-extend first, then shift at width `w`.
This is IEEE 1800's context-determined `a >>> b` for a signed operand. -/
theorem sra_spec_wide {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb) (h : wa ≤ w) :
    denote_op LGraphOp.Op_SRA w [enc a, enc b] = enc ((a.signExtend w).sshiftRight b.toNat) := by
  show bv_sra w (enc a) (enc b) = _
  rw [sra_guard, mk_bv_ofInt, BitVec.sshiftRight_eq, BitVec.toInt_signExtend_of_le h,
    Int.shiftRight_eq_div_pow, pow_cast]

/-- Amount 0 only resizes. -/
theorem sra_amount_zero {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (h : b.toNat = 0) :
    denote_op LGraphOp.Op_SRA w [enc a, enc b] = enc (a.signExtend w) := by
  rw [sra_spec, h, BitVec.sshiftRight_zero]

/-- An amount at or past the operand width gives all sign bits. -/
theorem sra_amount_ge {wa wb : Nat} (w : Nat) (a : BitVec wa) (b : BitVec wb)
    (h : wa ≤ b.toNat) :
    denote_op LGraphOp.Op_SRA w [enc a, enc b]
      = enc (if a.msb then BitVec.allOnes w else 0#w) := by
  show bv_sra w (enc a) (enc b) = _
  unfold bv_sra
  simp only [enc_width, bv_uint_enc, Int.toNat_natCast, bv_sint_enc]
  rw [ite_eq_left h, BitVec.msb_eq_toInt]
  by_cases hw : wa = 0
  · subst hw
    rw [ite_eq_left rfl, toInt_zero_width]; rfl
  · rw [ite_eq_right hw]
    by_cases hs : a.toInt ≥ 0
    · rw [ite_eq_left hs, show decide (a.toInt < 0) = false by simp; omega]; rfl
    · rw [ite_eq_right hs, show decide (a.toInt < 0) = true by simp; omega, mk_bv_ofInt]
      simp only [ite_true]
      rw [BitVec.ofInt_neg, ← BitVec.neg_one_eq_allOnes]; rfl

--------------------------------------------------------------------------------
-- Op_MuxBool, Op_MuxN
--------------------------------------------------------------------------------

/-- `Op_MuxBool [sel, f, t]` is `sel ? t : f` with a nonzero select meaning true. -/
theorem muxbool_spec {ws wf wt : Nat} (w : Nat) (s : BitVec ws) (f : BitVec wf) (t : BitVec wt) :
    denote_op LGraphOp.Op_MuxBool w [enc s, enc f, enc t]
      = enc (if s ≠ 0#ws then t.setWidth w else f.setWidth w) := by
  simp only [denote_op, bv_nonzero_enc, bv_resize_enc]
  by_cases h : s = 0#ws <;> simp [h]

/-- `Op_MuxN (sel :: opts)` picks option `sel`, resized to `w`.  Past the last option it
gives 0. -/
theorem muxn_spec {ws : Nat} (w : Nat) (s : BitVec ws) (opts : List (Σ k, BitVec k)) :
    denote_op LGraphOp.Op_MuxN w (enc s :: opts.map encS)
      = enc ((opts[s.toNat]?.map fun p => p.2.setWidth w).getD 0#w) := by
  simp only [denote_op, bv_uint_enc, Int.toNat_natCast, List.length_map, List.getElem?_map]
  by_cases h : s.toNat < opts.length
  · rw [ite_eq_left h, List.getElem?_eq_getElem h]
    simp only [Option.map_some, Option.getD_some, encS, bv_resize_enc]
  · rw [ite_eq_right h, List.getElem?_eq_none (by omega)]
    simp only [Option.map_none, Option.getD_none, enc_zero]

/-- A select past the last option.  IEEE 1800 gives X.  The certificate picks 0. -/
theorem muxn_out_of_range {ws : Nat} (w : Nat) (s : BitVec ws) (opts : List (Σ k, BitVec k))
    (h : opts.length ≤ s.toNat) :
    denote_op LGraphOp.Op_MuxN w (enc s :: opts.map encS) = enc (0#w) := by
  rw [muxn_spec, List.getElem?_eq_none h]; rfl

/-- `Op_MuxN` with no operands is 0. -/
theorem muxn_nil_spec (w : Nat) : denote_op LGraphOp.Op_MuxN w [] = enc (0#w) := by
  simp only [denote_op, enc_zero]

--------------------------------------------------------------------------------
-- Op_Sext
--------------------------------------------------------------------------------

/-- The signed value of the low `n > 0` bits, as the certificate's `Op_Sext` writes it. -/
theorem toInt_setWidth {wa : Nat} (a : BitVec wa) (n : Nat) (hn : 0 < n) :
    (a.setWidth n).toInt
      = if ((a.toNat % 2 ^ n : Nat) : Int) < 2 ^ (n - 1) then ((a.toNat % 2 ^ n : Nat) : Int)
        else ((a.toNat % 2 ^ n : Nat) : Int) - 2 ^ n := by
  rw [BitVec.toInt_eq_toNat_cond, BitVec.toNat_setWidth, pow_cast]
  have hpow : (2 : Nat) ^ n = 2 * 2 ^ (n - 1) := by
    rw [← Nat.pow_succ']; congr 1; omega
  have hc : (((a.toNat % 2 ^ n : Nat) : Int) < 2 ^ (n - 1)) ↔ (2 * (a.toNat % 2 ^ n) < 2 ^ n) := by
    rw [← pow_cast, Int.ofNat_lt]; omega
  by_cases h : 2 * (a.toNat % 2 ^ n) < 2 ^ n
  · rw [ite_eq_left h, ite_eq_left (hc.mpr h)]
  · rw [ite_eq_right h, ite_eq_right (fun h' => h (hc.mp h'))]

/-- `Op_Sext [a, n]`: keep the low `n` bits of `a` and sign-extend them from bit `n - 1`
to `w`.  One statement covers `n = 0` (result 0), `n ≤ wa` and `n > wa`
(plain zero extension, since bit `n - 1` of `a` is then 0). -/
theorem sext_spec {wa wn : Nat} (w : Nat) (a : BitVec wa) (amt : BitVec wn) :
    denote_op LGraphOp.Op_Sext w [enc a, enc amt] = enc ((a.setWidth amt.toNat).signExtend w) := by
  simp only [denote_op, bv_uint_enc, Int.toNat_natCast, enc_width]
  rw [signExtend_eq_ofInt]
  by_cases h0 : amt.toNat = 0
  · rw [ite_eq_left h0, mk_bv_ofInt]
    congr 2
    rw [BitVec.toInt_eq_toNat_cond, BitVec.toNat_setWidth, h0]; simp [Nat.mod_one]
  · rw [ite_eq_right h0, toInt_setWidth a _ (by omega)]
    by_cases hg : amt.toNat > wa
    · rw [ite_eq_left hg, mk_bv_ofInt]
      have hlt : a.toNat < 2 ^ (amt.toNat - 1) :=
        Nat.lt_of_lt_of_le a.isLt (Nat.pow_le_pow_right (by decide) (by omega))
      have hm : a.toNat % 2 ^ amt.toNat = a.toNat :=
        Nat.mod_eq_of_lt (Nat.lt_of_lt_of_le a.isLt (Nat.pow_le_pow_right (by decide) (by omega)))
      rw [hm, ite_eq_left (by rw [← pow_cast]; exact Int.ofNat_lt.mpr hlt)]
    · rw [ite_eq_right hg]
      simp only [Int.natCast_emod, pow_cast]
      split <;> rw [mk_bv_ofInt]

--------------------------------------------------------------------------------
-- Op_GetMask
--------------------------------------------------------------------------------

theorem mask_indices_enc {wm : Nat} (m : BitVec wm) : mask_indices_bv (enc m) = mask_indices m := by
  simp only [mask_indices_bv, mask_indices, enc_width, bv_bit_enc]

theorem one_shiftLeft_getLsbD {w : Nat} (k j : Nat) :
    ((1#w) <<< k).getLsbD j = (decide (j < w) && decide (j = k)) := by
  rw [BitVec.getLsbD_shiftLeft, BitVec.getLsbD_one]
  by_cases hj : j < w
  · by_cases hk : j = k
    · subst hk; simp [hj]; omega
    · by_cases hlt : j < k
      · simp [hj, hk, hlt]
      · simp [hj, hk, hlt]; omega
  · simp [hj]

theorem ofInt_two_pow (w k : Nat) : BitVec.ofInt w ((2 : Int) ^ k) = (1#w) <<< k := by
  rw [← pow_cast, BitVec.ofInt_natCast]
  apply BitVec.eq_of_toNat_eq
  rw [BitVec.toNat_ofNat, BitVec.toNat_shiftLeft, BitVec.toNat_ofNat, Nat.shiftLeft_eq,
    Nat.mod_mul_mod, Nat.one_mul]

theorem pack_low_getLsbD_ge {wa w : Nat} (a : BitVec wa) :
    ∀ (is : List Nat) (j : Nat), is.length ≤ j → (pack_low a is : BitVec w).getLsbD j = false := by
  intro is
  induction is with
  | nil => intro j _; simp [pack_low]
  | cons i is ih =>
    intro j hj
    simp only [List.length_cons] at hj
    simp only [pack_low]
    split
    · rw [BitVec.getLsbD_or, one_shiftLeft_getLsbD, ih j (by omega)]
      simp; omega
    · exact ih j (by omega)

theorem pack_low_enc {wa w : Nat} (a : BitVec wa) :
    ∀ (is : List Nat), mk_bv w (pack_low_bv (enc a) is) = enc (pack_low a is : BitVec w) := by
  intro is
  induction is with
  | nil => simp only [pack_low_bv, pack_low, enc_zero]
  | cons i is ih =>
    simp only [pack_low_bv, pack_low, bv_bit_enc]
    split
    · rw [mk_bv_eq_enc_iff, BitVec.ofInt_add, ofInt_of_mk_bv ih, ofInt_two_pow]
      apply BitVec.add_eq_or_of_and_eq_zero
      apply BitVec.eq_of_getLsbD_eq
      intro j _
      rw [BitVec.getLsbD_and, one_shiftLeft_getLsbD, BitVec.getLsbD_zero]
      by_cases hj : j = is.length
      · rw [pack_low_getLsbD_ge a is j (by omega)]; simp
      · simp [hj]
    · exact ih

/-- `Op_GetMask` is the parallel bit extract `sem_get_mask`, at every width. -/
theorem getmask_spec {wa wm : Nat} (w : Nat) (a : BitVec wa) (m : BitVec wm) :
    denote_op LGraphOp.Op_GetMask w [enc a, enc m] = enc (sem_get_mask a m : BitVec w) := by
  show bv_get_mask w (enc a) (enc m) = _
  unfold bv_get_mask sem_get_mask
  rw [mask_indices_enc]
  exact pack_low_enc a _

theorem pack_low_getLsbD {wa w : Nat} (a : BitVec wa) :
    ∀ (is : List Nat) (j : Nat),
      (pack_low a is : BitVec w).getLsbD j = (decide (j < w) && (is.reverse[j]?).any a.getLsbD) := by
  intro is
  induction is with
  | nil => intro j; simp [pack_low]
  | cons i is ih =>
    intro j
    rw [List.reverse_cons, List.getElem?_append, List.length_reverse]
    simp only [pack_low]
    have hQ := ih j
    by_cases hlt : j < is.length
    · rw [ite_eq_left hlt]
      split
      · rw [BitVec.getLsbD_or, one_shiftLeft_getLsbD, hQ]
        have : ¬ j = is.length := by omega
        simp [this]
      · exact hQ
    · rw [ite_eq_right hlt, List.getElem?_singleton]
      have hQ0 : (pack_low a is : BitVec w).getLsbD j = false := pack_low_getLsbD_ge a is j (by omega)
      by_cases heq : j = is.length
      · subst heq
        simp only [Nat.sub_self, ite_true, Option.any_some]
        split
        · next hb => rw [BitVec.getLsbD_or, one_shiftLeft_getLsbD, hQ0, hb]; simp
        · next hb => rw [hQ0]; simp [hb]
      · have hne : j - is.length ≠ 0 := by omega
        rw [ite_eq_right hne, Option.any_none, Bool.and_false]
        split
        · rw [BitVec.getLsbD_or, one_shiftLeft_getLsbD, hQ0]; simp [heq]
        · exact hQ0

/-- Bit `j` of a GetMask result is the source bit at the `j`-th set mask bit, counted
from bit 0; bits past the number of set mask bits are 0. -/
theorem sem_get_mask_getLsbD {wa wm : Nat} (w : Nat) (a : BitVec wa) (m : BitVec wm) (j : Nat) :
    (sem_get_mask a m : BitVec w).getLsbD j
      = (decide (j < w) && ((mask_indices m)[j]?).any a.getLsbD) := by
  unfold sem_get_mask
  rw [pack_low_getLsbD, List.reverse_reverse]

theorem range_split (n lo len : Nat) (h : lo + len ≤ n) :
    List.range n = List.range' 0 lo ++ List.range' lo len ++ List.range' (lo + len) (n - lo - len) := by
  rw [List.range_eq_range']
  have e1 := @List.range'_append 0 lo len 1
  have e2 := @List.range'_append 0 (lo + len) (n - lo - len) 1
  simp only [Nat.one_mul, Nat.zero_add] at e1 e2
  rw [e1, e2]; congr 1; omega

theorem mask_indices_range {wm lo len : Nat} (m : BitVec wm)
    (hm : ∀ i, m.getLsbD i = (decide (lo ≤ i) && decide (i < lo + len))) :
    mask_indices m = List.range' lo len := by
  by_cases hl : len = 0
  · subst hl
    unfold mask_indices
    rw [List.filter_eq_nil_iff.mpr]; · rfl
    intro i _; rw [hm]; simp
  · have hfit : lo + len ≤ wm := by
      have h1 := hm (lo + len - 1)
      have h2 : m.getLsbD (lo + len - 1) = true := by rw [h1]; simp; omega
      have := BitVec.lt_of_getLsbD h2; omega
    unfold mask_indices
    rw [range_split wm lo len hfit, List.filter_append, List.filter_append,
      List.filter_eq_nil_iff.mpr, List.filter_eq_self.mpr, List.filter_eq_nil_iff.mpr]
    · simp
    · intro i hi; rw [List.mem_range'] at hi; rw [hm]; simp; omega
    · intro i hi; rw [List.mem_range'] at hi; rw [hm]; simp; omega
    · intro i hi; rw [List.mem_range'] at hi; rw [hm]; simp; omega

theorem range'_getElem? (lo len j : Nat) :
    (List.range' lo len)[j]? = if j < len then some (lo + j) else none := by
  by_cases hj : j < len
  · rw [ite_eq_left hj, List.getElem?_range' hj, Nat.one_mul]
  · rw [ite_eq_right hj, List.getElem?_eq_none (by simp; omega)]

/-- GetMask with a mask whose set bits are exactly `[lo, lo + len)` is a part-select. -/
theorem getmask_contiguous {wa wm lo len : Nat} (w : Nat) (a : BitVec wa) (m : BitVec wm)
    (hm : ∀ i, m.getLsbD i = (decide (lo ≤ i) && decide (i < lo + len))) :
    denote_op LGraphOp.Op_GetMask w [enc a, enc m] = enc ((a.extractLsb' lo len).setWidth w) := by
  rw [getmask_spec]
  congr 1
  apply BitVec.eq_of_getLsbD_eq
  intro j hj
  rw [sem_get_mask_getLsbD, mask_indices_range m hm, range'_getElem?, BitVec.getLsbD_setWidth,
    BitVec.getLsbD_extractLsb']
  by_cases hjl : j < len <;> simp [hj, hjl]

theorem extractLsb'_zero_eq_setWidth {wa : Nat} (n : Nat) (a : BitVec wa) :
    a.extractLsb' 0 n = a.setWidth n := by
  apply BitVec.eq_of_getLsbD_eq
  intro j hj
  rw [BitVec.getLsbD_extractLsb', BitVec.getLsbD_setWidth, Nat.zero_add]

/-- The all-ones mask (LiveHD's zero-extension idiom) selects the low `wm` bits. -/
theorem getmask_allOnes {wa wm : Nat} (w : Nat) (a : BitVec wa) :
    denote_op LGraphOp.Op_GetMask w [enc a, enc (BitVec.allOnes wm)]
      = enc ((a.setWidth wm).setWidth w) := by
  rw [getmask_contiguous (lo := 0) (len := wm) w a _ (by intro i; simp), extractLsb'_zero_eq_setWidth]

theorem ofInt_neg_one_eq_allOnes (w : Nat) : BitVec.ofInt w (-1) = BitVec.allOnes w := by
  rw [BitVec.ofInt_neg, ← BitVec.neg_one_eq_allOnes]; rfl

--------------------------------------------------------------------------------
-- Op_SetMask
--------------------------------------------------------------------------------

theorem bit_mask_at_getLsbD {wa : Nat} (i j : Nat) :
    (bit_mask_at i : BitVec wa).getLsbD j = (decide (j < wa) && decide (j = i)) :=
  one_shiftLeft_getLsbD i j

/-- One certificate bit update, on an encoded word, is the core set or clear of that bit. -/
theorem set_bit_enc {wa : Nat} (acc : BitVec wa) (i : Nat) (b : Bool) :
    bv_set_bit (enc acc) i b
      = enc (if b then acc ||| bit_mask_at i else acc &&& ~~~(bit_mask_at i : BitVec wa)) := by
  unfold bv_set_bit
  simp only [enc_width, bv_uint_enc, bv_bit_enc]
  rw [mk_bv_eq_enc_iff]
  have hM : ∀ j, (bit_mask_at i : BitVec wa).getLsbD j = (decide (j < wa) && decide (j = i)) :=
    bit_mask_at_getLsbD i
  have hacc : ∀ j, acc.getLsbD j = true → j < wa := fun j h => BitVec.lt_of_getLsbD h
  cases b <;> by_cases hb : acc.getLsbD i <;> simp only [hb, ite_true, ite_false, Bool.false_eq_true]
  · -- clear a set bit
    rw [Int.sub_eq_add_neg, BitVec.ofInt_add, BitVec.ofInt_neg, ofInt_toNat, BitVec.setWidth_eq,
      ofInt_two_pow, ← BitVec.sub_eq_add_neg,
      show ((1#wa) <<< i) = (bit_mask_at i : BitVec wa) from rfl]
    have hsplit : acc = (acc &&& ~~~(bit_mask_at i : BitVec wa)) + bit_mask_at i := by
      rw [BitVec.add_eq_or_of_and_eq_zero]
      · apply BitVec.eq_of_getLsbD_eq; intro j hj
        rw [BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_not, hM]
        by_cases hji : j = i
        · subst hji; simp [hb, hj]
        · simp [hji, hj]
      · apply BitVec.eq_of_getLsbD_eq; intro j hj
        rw [BitVec.getLsbD_and, BitVec.getLsbD_and, BitVec.getLsbD_not, hM, BitVec.getLsbD_zero]
        by_cases hji : j = i <;> simp [hji]
    conv => lhs; rw [hsplit]
    rw [BitVec.add_sub_cancel]
  · -- clear a clear bit
    rw [Int.sub_zero, ofInt_toNat, BitVec.setWidth_eq]
    apply BitVec.eq_of_getLsbD_eq; intro j hj
    rw [BitVec.getLsbD_and, BitVec.getLsbD_not, hM]
    by_cases hji : j = i
    · subst hji; simp [hb]
    · simp [hji, hj]
  · -- set a set bit
    rw [Int.add_zero, ofInt_toNat, BitVec.setWidth_eq]
    apply BitVec.eq_of_getLsbD_eq; intro j hj
    rw [BitVec.getLsbD_or, hM]
    by_cases hji : j = i
    · subst hji; simp [hb]
    · simp [hji]
  · -- set a clear bit
    rw [BitVec.ofInt_add, ofInt_toNat, BitVec.setWidth_eq, ofInt_two_pow]
    apply BitVec.add_eq_or_of_and_eq_zero
    apply BitVec.eq_of_getLsbD_eq; intro j hj
    rw [BitVec.getLsbD_and, one_shiftLeft_getLsbD, BitVec.getLsbD_zero]
    by_cases hji : j = i
    · subst hji; simp [hb]
    · simp [hji]

/-- The fold step of `sem_set_mask`. -/
def setMaskStep {wa wv : Nat} (v : BitVec wv) (acc : BitVec wa) (p : Nat × Nat) : BitVec wa :=
  if v.getLsbD p.1 then acc ||| bit_mask_at p.2 else acc &&& ~~~(bit_mask_at p.2 : BitVec wa)

theorem sem_set_mask_eq {wa wm wv : Nat} (a : BitVec wa) (m : BitVec wm) (v : BitVec wv) :
    sem_set_mask a m v
      = (List.zip (List.range (mask_indices m).length) (mask_indices m)).foldl (setMaskStep v) a := rfl

theorem set_mask_fold_enc {wa wv : Nat} (v : BitVec wv) :
    ∀ (pairs : List (Nat × Nat)) (acc : BitVec wa),
      pairs.foldl (fun acc p => bv_set_bit acc p.2 (bv_bit (enc v) p.1)) (enc acc)
        = enc (pairs.foldl (setMaskStep v) acc) := by
  intro pairs
  induction pairs with
  | nil => intro acc; rfl
  | cons p ps ih =>
    intro acc
    simp only [List.foldl_cons]
    rw [bv_bit_enc, set_bit_enc, ih]
    rfl

/-- `Op_SetMask [a, m, v]`: deposit the low bits of `v` into `a` at the set bits of `m`
(`sem_set_mask`), resized to `w`. -/
theorem setmask_spec {wa wm wv : Nat} (w : Nat) (a : BitVec wa) (m : BitVec wm) (v : BitVec wv) :
    denote_op LGraphOp.Op_SetMask w [enc a, enc m, enc v] = enc ((sem_set_mask a m v).setWidth w) := by
  show bv_set_mask w (enc a) (enc m) (enc v) = _
  unfold bv_set_mask
  simp only [mask_indices_enc]
  rw [set_mask_fold_enc, bv_resize_enc, sem_set_mask_eq]

theorem setMaskStep_getLsbD {wa wv : Nat} (v : BitVec wv) (acc : BitVec wa) (p : Nat × Nat)
    (k : Nat) :
    (setMaskStep v acc p).getLsbD k
      = if p.2 = k then (decide (k < wa) && v.getLsbD p.1) else acc.getLsbD k := by
  unfold setMaskStep
  have hacc : acc.getLsbD k = true → k < wa := fun h => BitVec.lt_of_getLsbD h
  by_cases hp : p.2 = k
  · subst hp
    split
    · next hv => rw [BitVec.getLsbD_or, bit_mask_at_getLsbD]; simp [hv]; intro h; exact hacc h
    · next hv => rw [BitVec.getLsbD_and, BitVec.getLsbD_not, bit_mask_at_getLsbD]; simp [hv]
  · have hne : ¬ k = p.2 := fun h => hp h.symm
    split
    · rw [BitVec.getLsbD_or, bit_mask_at_getLsbD]; simp [hne]
    · rw [BitVec.getLsbD_and, BitVec.getLsbD_not, bit_mask_at_getLsbD]; simp [hne]
      intro h; exact hacc h

theorem fold_setMaskStep_getLsbD {wa wv : Nat} (v : BitVec wv) (k : Nat) :
    ∀ (pairs : List (Nat × Nat)) (acc : BitVec wa),
      (pairs.foldl (setMaskStep v) acc).getLsbD k
        = pairs.foldl (fun b p => if p.2 = k then (decide (k < wa) && v.getLsbD p.1) else b)
            (acc.getLsbD k) := by
  intro pairs
  induction pairs with
  | nil => intro acc; rfl
  | cons p ps ih =>
    intro acc
    simp only [List.foldl_cons]
    rw [ih, setMaskStep_getLsbD]

theorem fold_zip_range' (c : Nat → Bool) (k : Nat) :
    ∀ (idxs : List Nat) (s : Nat) (b0 : Bool), idxs.Nodup →
      (List.zip (List.range' s idxs.length) idxs).foldl (fun b p => if p.2 = k then c p.1 else b) b0
        = if k ∈ idxs then c (s + idxs.idxOf k) else b0 := by
  intro idxs
  induction idxs with
  | nil => intro s b0 _; simp
  | cons x xs ih =>
    intro s b0 hnd
    rw [List.nodup_cons] at hnd
    simp only [List.length_cons, List.range'_succ, List.zip_cons_cons, List.foldl_cons]
    rw [ih (s + 1) _ hnd.2]
    by_cases hk : k ∈ xs
    · have hxk : x ≠ k := fun h => hnd.1 (h ▸ hk)
      rw [ite_eq_left hk, ite_eq_left (List.mem_cons_of_mem _ hk), List.idxOf_cons]
      simp only [beq_iff_eq, hxk, ite_false]
      congr 1; omega
    · rw [ite_eq_right hk]
      by_cases hxk : x = k
      · subst hxk
        rw [ite_eq_left rfl, ite_eq_left List.mem_cons_self, List.idxOf_cons_self, Nat.add_zero]
      · rw [ite_eq_right hxk, ite_eq_right]
        intro h; rcases List.mem_cons.mp h with h | h
        · exact hxk h.symm
        · exact hk h

theorem mask_indices_nodup {wm : Nat} (m : BitVec wm) : (mask_indices m).Nodup :=
  List.Nodup.sublist List.filter_sublist List.nodup_range

theorem mem_mask_indices {wm : Nat} (m : BitVec wm) (k : Nat) :
    k ∈ mask_indices m ↔ m.getLsbD k = true := by
  unfold mask_indices
  rw [List.mem_filter, List.mem_range]
  exact ⟨fun h => h.2, fun h => ⟨BitVec.lt_of_getLsbD h, h⟩⟩

/-- Bit `k` of a SetMask result: where the mask bit is set, the bit of `v` at the rank of
`k` among the set mask bits (its index in `mask_indices m`); elsewhere the bit of `a`.
Mask bits at or above the width of `a` change nothing. -/
theorem sem_set_mask_getLsbD {wa wm wv : Nat} (a : BitVec wa) (m : BitVec wm) (v : BitVec wv)
    (k : Nat) :
    (sem_set_mask a m v).getLsbD k
      = if m.getLsbD k then (decide (k < wa) && v.getLsbD ((mask_indices m).idxOf k))
        else a.getLsbD k := by
  rw [sem_set_mask_eq, fold_setMaskStep_getLsbD, List.range_eq_range',
    fold_zip_range' (fun j => decide (k < wa) && v.getLsbD j) k _ 0 _ (mask_indices_nodup m),
    Nat.zero_add]
  by_cases hk : m.getLsbD k = true
  · rw [ite_eq_left ((mem_mask_indices m k).mpr hk), ite_eq_left hk]
  · rw [ite_eq_right (fun h => hk ((mem_mask_indices m k).mp h)), ite_eq_right hk]

/-- The number of set mask bits strictly below `k`. -/
def maskRank {wm : Nat} (m : BitVec wm) (k : Nat) : Nat :=
  ((List.range k).filter fun i => m.getLsbD i).length

theorem idxOf_append_cons (L1 L2 : List Nat) (k : Nat) (h : k ∉ L1) :
    (L1 ++ k :: L2).idxOf k = L1.length := by
  rw [List.idxOf_append, ite_eq_right h, List.idxOf_cons_self, Nat.zero_add]

theorem idxOf_mask_indices {wm : Nat} (m : BitVec wm) (k : Nat) (hk : m.getLsbD k = true) :
    (mask_indices m).idxOf k = maskRank m k := by
  have hkw : k < wm := BitVec.lt_of_getLsbD hk
  unfold mask_indices maskRank
  have hsplit : List.range wm = List.range k ++ k :: List.range' (k + 1) (wm - k - 1) := by
    rw [List.range_eq_range', List.range_eq_range']
    have e := @List.range'_append 0 k (wm - k) 1
    simp only [Nat.one_mul, Nat.zero_add] at e
    rw [show wm - k = (wm - k - 1) + 1 by omega, List.range'_succ] at e
    rw [e]; congr 1; omega
  rw [hsplit, List.filter_append, List.filter_cons, ite_eq_left hk]
  apply idxOf_append_cons
  intro hmem
  rw [List.mem_filter, List.mem_range] at hmem
  omega

theorem sem_set_mask_getLsbD_rank {wa wm wv : Nat} (a : BitVec wa) (m : BitVec wm)
    (v : BitVec wv) (k : Nat) (hk : k < wa) :
    (sem_set_mask a m v).getLsbD k
      = if m.getLsbD k then v.getLsbD (maskRank m k) else a.getLsbD k := by
  rw [sem_set_mask_getLsbD]
  by_cases hm : m.getLsbD k = true
  · rw [ite_eq_left hm, ite_eq_left hm, idxOf_mask_indices m k hm]; simp [hk]
  · rw [ite_eq_right hm, ite_eq_right hm]

theorem idxOf_range' (lo len j : Nat) (hj : j < len) : (List.range' lo len).idxOf (lo + j) = j := by
  induction len generalizing lo j with
  | zero => omega
  | succ n ih =>
    rw [List.range'_succ, List.idxOf_cons]
    by_cases hj0 : j = 0
    · subst hj0; simp
    · have hne : (lo == lo + j) = false := by simp; omega
      rw [hne]
      simp only [Bool.false_eq_true, ite_false]
      have := ih (lo + 1) (j - 1) (by omega)
      rw [show lo + 1 + (j - 1) = lo + j by omega] at this
      rw [this]; omega

/-- SetMask with a mask whose set bits are exactly `[lo, lo + len)` writes `v` into that
field of `a` (a part-select assignment `a[lo +: len] = v`). -/
theorem setmask_contiguous {wa wm wv lo len : Nat} (w : Nat) (a : BitVec wa) (m : BitVec wm)
    (v : BitVec wv) (hm : ∀ i, m.getLsbD i = (decide (lo ≤ i) && decide (i < lo + len))) :
    denote_op LGraphOp.Op_SetMask w [enc a, enc m, enc v]
      = enc (((a &&& ~~~(m.setWidth wa)) ||| ((v.setWidth wa <<< lo) &&& m.setWidth wa)).setWidth w) := by
  rw [setmask_spec]
  congr 2
  apply BitVec.eq_of_getLsbD_eq
  intro k hk
  rw [sem_set_mask_getLsbD, mask_indices_range m hm]
  simp only [BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_not, BitVec.getLsbD_setWidth,
    BitVec.getLsbD_shiftLeft, hm, hk, decide_true, Bool.true_and]
  by_cases hin : lo ≤ k ∧ k < lo + len
  · have hidx : (List.range' lo len).idxOf k = k - lo := by
      have := idxOf_range' lo len (k - lo) (by omega)
      rwa [show lo + (k - lo) = k by omega] at this
    rw [hidx]
    have h1 : decide (lo ≤ k) = true := by simp; omega
    have h2 : decide (k < lo + len) = true := by simp; omega
    have h3 : decide (k < lo) = false := by simp; omega
    have h4 : decide (k - lo < wa) = true := by simp; omega
    simp [h1, h2, h3, h4]
  · have : (decide (lo ≤ k) && decide (k < lo + len)) = false := by
      simp; omega
    simp only [this, Bool.not_false, Bool.and_true, Bool.and_false, Bool.or_false, ite_false,
      Bool.false_eq_true]

--------------------------------------------------------------------------------
-- Memory
--------------------------------------------------------------------------------

/-- Encode a memory `BitVec aw → BitVec dw` as a certificate image.  Same as `OpBridge.memenc`.
The guard is needed: `cert_mem_write` compares addresses exactly on `Int`, while
`BitVec.ofInt` wraps.  Every address the certificate produces from an encoded operand is
inside the guard. -/
def memenc {aw dw : Nat} (m : BitVec aw → BitVec dw) : Int → BV :=
  fun x => if 0 ≤ x ∧ x < 2 ^ aw then enc (m (BitVec.ofInt aw x)) else mk_bv dw 0

theorem addr_in_range {aw : Nat} (x : BitVec aw) : 0 ≤ (x.toNat : Int) ∧ (x.toNat : Int) < 2 ^ aw :=
  ⟨Int.natCast_nonneg _, toNat_lt_cast x⟩

theorem ofInt_toNat_self {aw : Nat} (x : BitVec aw) : BitVec.ofInt aw (x.toNat : Int) = x := by
  rw [ofInt_toNat, BitVec.setWidth_eq]

theorem toNat_ofInt_in_range {aw : Nat} {x : Int} (h0 : 0 ≤ x) (h1 : x < 2 ^ aw) :
    ((BitVec.ofInt aw x).toNat : Int) = x := by
  rw [BitVec.toNat_ofInt, pow_cast, Int.emod_eq_of_lt h0 h1, Int.toNat_of_nonneg h0]

theorem memenc_at {aw dw : Nat} (m : BitVec aw → BitVec dw) (x : BitVec aw) :
    memenc m (x.toNat : Int) = enc (m x) := by
  unfold memenc
  rw [ite_eq_left (addr_in_range x), ofInt_toNat_self]

/-- `Op_MemRead [mem, addr, en]`: the entry at `addr` resized to `w` when `en` is nonzero,
else 0. -/
theorem memread_spec {aw dw we : Nat} (w : Nat) (m : BitVec aw → BitVec dw) (addr : BitVec aw)
    (en : BitVec we) :
    eval_op_cert LGraphOp.Op_MemRead w [.mem (memenc m), .bv (enc addr), .bv (enc en)]
      = .bv (enc (if en ≠ 0#we then (m addr).setWidth w else 0#w)) := by
  show CertVal.bv (cert_mem_read w (memenc m) (enc addr) (enc en)) = _
  unfold cert_mem_read
  rw [bv_nonzero_enc, bv_uint_enc, memenc_at]
  by_cases h : en = 0#we
  · simp [h, enc_zero]
  · simp [h, bv_resize_enc]

/-- `Op_MemWrite [mem, addr, d, en]`: write `d` at `addr` when `en` is nonzero.  The data
operand has the entry width. -/
theorem memwrite_spec {aw dw we : Nat} (w : Nat) (m : BitVec aw → BitVec dw) (addr : BitVec aw)
    (d : BitVec dw) (en : BitVec we) :
    eval_op_cert LGraphOp.Op_MemWrite w
        [.mem (memenc m), .bv (enc addr), .bv (enc d), .bv (enc en)]
      = .mem (memenc (if en ≠ 0#we then (fun x => if x = addr then d else m x) else m)) := by
  show CertVal.mem (cert_mem_write (memenc m) (enc addr) (enc d) (enc en)) = _
  unfold cert_mem_write
  rw [bv_nonzero_enc, bv_uint_enc]
  by_cases h : en = 0#we
  · simp [h]
  · simp only [h, ne_eq, not_false_eq_true, decide_true, ite_true]
    congr 1
    funext x
    unfold memenc
    dsimp only
    by_cases hr : 0 ≤ x ∧ x < 2 ^ aw
    · rw [ite_eq_left hr, ite_eq_left hr]
      by_cases hx : x = (addr.toNat : Int)
      · rw [ite_eq_left hx, hx, ofInt_toNat_self, ite_eq_left rfl]
      · rw [ite_eq_right hx]
        have hne : BitVec.ofInt aw x ≠ addr := by
          intro hc; apply hx; rw [← hc, toNat_ofInt_in_range hr.1 hr.2]
        rw [ite_eq_right hne]
    · have hx : x ≠ (addr.toNat : Int) := by
        intro hc; exact hr (hc ▸ addr_in_range addr)
      rw [ite_eq_right hx, ite_eq_right hr, ite_eq_right hr]

theorem ones_low_eq {d : Nat} (bw : Nat) : (1#d <<< bw) - 1#d = BitVec.ofNat d (2 ^ bw - 1) := by
  apply BitVec.eq_of_toNat_eq
  rw [BitVec.toNat_sub, BitVec.toNat_shiftLeft, BitVec.toNat_ofNat, BitVec.toNat_ofNat,
    Nat.shiftLeft_eq, Nat.mod_mul_mod, Nat.one_mul]
  rcases Nat.eq_zero_or_pos d with hd | hd
  · subst hd; simp [Nat.mod_one]
  · have h1 : 1 % 2 ^ d = 1 := Nat.mod_eq_of_lt (Nat.one_lt_two_pow (by omega))
    have hb : 1 ≤ 2 ^ bw := Nat.one_le_two_pow
    have hd1 : 1 ≤ 2 ^ d := Nat.one_le_two_pow
    rw [h1, Nat.add_mod_mod, show 2 ^ d - 1 + 2 ^ bw = (2 ^ bw - 1) + 2 ^ d by omega, Nat.add_mod_right]

theorem byte_mask_word_getLsbD {d : Nat} (bw bi i : Nat) :
    (byte_mask_word bw bi (d := d)).getLsbD i
      = (decide (i < d) && decide (bi * bw ≤ i) && decide (i - bi * bw < bw)) := by
  unfold byte_mask_word
  simp only []
  rw [BitVec.getLsbD_shiftLeft, ones_low_eq, BitVec.getLsbD_ofNat, Nat.testBit_two_pow_sub_one]
  by_cases h1 : i < d
  · by_cases h2 : bi * bw ≤ i
    · have h3 : ¬ i < bi * bw := by omega
      have h4 : i - bi * bw < d := by omega
      simp [h1, h2, h3, h4]
    · have h3 : i < bi * bw := by omega
      simp [h1, h2, h3]
  · simp [h1]

theorem fold_mask_getLsbD {be d : Nat} (bev : BitVec be) (bw i : Nat) :
    ∀ (l : List Nat) (acc : BitVec d),
      ((l.foldl (fun (acc : BitVec d) (bi : Nat) =>
          if bev.getLsbD bi then acc ||| byte_mask_word bw bi (d := d) else acc) acc).getLsbD i)
      = (acc.getLsbD i
          || l.any fun bi => bev.getLsbD bi && (byte_mask_word bw bi (d := d)).getLsbD i) := by
  intro l
  induction l with
  | nil => intro acc; simp
  | cons b t ih =>
    intro acc
    simp only [List.foldl_cons, List.any_cons]
    by_cases hb : bev.getLsbD b = true
    · rw [ite_eq_left hb, ih, BitVec.getLsbD_or, hb, Bool.true_and, Bool.or_assoc]
    · rw [ite_eq_right hb, ih]
      simp [hb]

theorem byte_enable_mask_getLsbD {be d : Nat} (bev : BitVec be) (bw i : Nat) (hbw : 0 < bw) :
    (byte_enable_mask bev bw (d := d)).getLsbD i = (decide (i < d) && bev.getLsbD (i / bw)) := by
  unfold byte_enable_mask
  rw [fold_mask_getLsbD, BitVec.getLsbD_zero, Bool.false_or]
  simp only [byte_mask_word_getLsbD]
  rw [Bool.eq_iff_iff]
  simp only [List.any_eq_true, List.mem_range, Bool.and_eq_true, decide_eq_true_eq]
  have hdm := Nat.div_add_mod' i bw
  have hml := Nat.mod_lt i hbw
  constructor
  · intro ⟨bi, _, hbev, ⟨hid, hle⟩, hlt⟩
    have hdiv : i / bw = bi := by
      apply Nat.div_eq_of_lt_le hle
      rw [Nat.succ_mul]; omega
    exact ⟨hid, by rw [hdiv]; exact hbev⟩
  · intro ⟨hid, hbev⟩
    exact ⟨i / bw, BitVec.lt_of_getLsbD hbev, hbev, ⟨hid, Nat.div_mul_le_self i bw⟩, by omega⟩

/-- Bit `i` of the byte-enable merge: the new bit where byte `i / byte_w` is enabled,
the old bit elsewhere. -/
theorem masked_word_update_getLsbD {d be : Nat} (old new : BitVec d) (bev : BitVec be)
    (bw i : Nat) (hbw : 0 < bw) :
    (masked_word_update old new bev bw).getLsbD i
      = (if bev.getLsbD (i / bw) then new.getLsbD i else old.getLsbD i) := by
  unfold masked_word_update
  simp only [BitVec.getLsbD_or, BitVec.getLsbD_and, BitVec.getLsbD_not,
    byte_enable_mask_getLsbD bev bw i hbw]
  by_cases hi : i < d
  · by_cases hb : bev.getLsbD (i / bw) = true <;> simp [hi, hb]
  · have ho : old.getLsbD i = false := BitVec.getLsbD_of_ge old i (by omega)
    have hn : new.getLsbD i = false := BitVec.getLsbD_of_ge new i (by omega)
    simp [hi, ho, hn]

theorem cert_masked_update_enc {d be : Nat} (old new : BitVec d) (bev : BitVec be) (bw : Nat)
    (hbw : 0 < bw) :
    cert_masked_update d (enc old) (enc new) (enc bev) bw = enc (masked_word_update old new bev bw) := by
  unfold cert_masked_update
  apply mk_bv_bits
  intro i _
  rw [masked_word_update_getLsbD old new bev bw i hbw]
  simp only [bv_bit_enc]

/-- `Op_MemWriteBE bw [mem, addr, d, be]`: when `be` is nonzero, merge `d` into the entry
at `addr` byte by byte (see `masked_word_update_getLsbD`).  The node width is the entry
width.  A byte has at least one bit (`0 < bw`).  At `bw = 0` the certificate takes
every bit from enable bit 0 while `masked_word_update` writes nothing. -/
theorem memwrite_be_spec {aw dw be : Nat} (bw : Nat) (hbw : 0 < bw) (m : BitVec aw → BitVec dw)
    (addr : BitVec aw) (d : BitVec dw) (bev : BitVec be) :
    eval_op_cert (LGraphOp.Op_MemWriteBE bw) dw
        [.mem (memenc m), .bv (enc addr), .bv (enc d), .bv (enc bev)]
      = .mem (memenc (if bev ≠ 0#be then
                (fun x => if x = addr then masked_word_update (m addr) d bev bw else m x)
              else m)) := by
  show CertVal.mem (cert_mem_write_be dw (memenc m) (enc addr) (enc d) (enc bev) bw) = _
  unfold cert_mem_write_be
  rw [bv_nonzero_enc, bv_uint_enc]
  by_cases h : bev = 0#be
  · simp [h]
  · simp only [h, ne_eq, not_false_eq_true, decide_true, ite_true]
    congr 1
    funext x
    rw [memenc_at, cert_masked_update_enc _ _ _ _ hbw]
    unfold memenc
    dsimp only
    by_cases hr : 0 ≤ x ∧ x < 2 ^ aw
    · rw [ite_eq_left hr]
      by_cases hx : x = (addr.toNat : Int)
      · rw [ite_eq_left hx, hx, ite_eq_left (addr_in_range addr), ofInt_toNat_self,
          ite_eq_left rfl]
      · rw [ite_eq_right hx, ite_eq_left hr]
        have hne : BitVec.ofInt aw x ≠ addr := by
          intro hc; apply hx; rw [← hc, toNat_ofInt_in_range hr.1 hr.2]
        rw [ite_eq_right hne]
    · have hx : x ≠ (addr.toNat : Int) := by
        intro hc; exact hr (hc ▸ addr_in_range addr)
      rw [ite_eq_right hx, ite_eq_right hr, ite_eq_right hr]

theorem extract_eq_ofNat {wb : Nat} (bus : BitVec wb) (s len : Nat) :
    bus.extractLsb' s len = BitVec.ofNat len (bus.toNat / 2 ^ s) := by
  apply BitVec.eq_of_toNat_eq
  rw [BitVec.extractLsb'_toNat, BitVec.toNat_ofNat, Nat.shiftRight_eq_div_pow]

/-- `Op_MemLoad n [mem, bus, en]`: when `en` is nonzero, entry `i < n` becomes bits
`[i * dw, (i + 1) * dw)` of `bus`, entry 0 in the low bits.  Bits past the top of the
bus read 0.  The hypothesis `n ≤ 2 ^ aw` keeps every loaded entry addressable. -/
theorem memload_spec {aw dw wb we : Nat} (n : Nat) (hn : n ≤ 2 ^ aw) (m : BitVec aw → BitVec dw)
    (bus : BitVec wb) (en : BitVec we) :
    eval_op_cert (LGraphOp.Op_MemLoad n) dw [.mem (memenc m), .bv (enc bus), .bv (enc en)]
      = .mem (memenc (if en ≠ 0#we then
                (fun x : BitVec aw => if x.toNat < n then bus.extractLsb' (x.toNat * dw) dw else m x)
              else m)) := by
  show CertVal.mem (cert_mem_load dw n (memenc m) (enc bus) (enc en)) = _
  unfold cert_mem_load
  rw [bv_nonzero_enc, bv_uint_enc]
  by_cases h : en = 0#we
  · simp [h]
  · simp only [h, ne_eq, not_false_eq_true, decide_true, ite_true]
    congr 1
    funext x
    have hn' : (n : Int) ≤ 2 ^ aw := by rw [← pow_cast]; exact Int.ofNat_le.mpr hn
    by_cases hx : 0 ≤ x ∧ x < n
    · rw [ite_eq_left hx]
      unfold memenc
      dsimp only
      have hr : 0 ≤ x ∧ x < 2 ^ aw := ⟨hx.1, by omega⟩
      rw [ite_eq_left hr]
      have hxn : (BitVec.ofInt aw x).toNat = x.toNat := by
        have := toNat_ofInt_in_range hr.1 hr.2; omega
      rw [hxn, ite_eq_left (by omega), extract_eq_ofNat, ← mk_bv_ofNat, ← pow_cast,
        Int.natCast_ediv]
    · rw [ite_eq_right hx]
      unfold memenc
      dsimp only
      by_cases hr : 0 ≤ x ∧ x < 2 ^ aw
      · rw [ite_eq_left hr, ite_eq_left hr]
        have hxn : ((BitVec.ofInt aw x).toNat : Int) = x := toNat_ofInt_in_range hr.1 hr.2
        rw [ite_eq_right (by omega)]
      · rw [ite_eq_right hr, ite_eq_right hr]

/-- Entries `0 .. k-1`, each `d` bits, packed into one `w`-bit bus with entry 0 in the
low bits: the concatenation `{e (k-1), ..., e 0}` zero-extended or truncated to `w`. -/
def toBus (w d : Nat) (e : Nat → BitVec d) : Nat → BitVec w
  | 0 => 0#w
  | k + 1 => toBus w d e k ||| ((e k).setWidth w <<< (k * d))

theorem ofInt_mul_two_pow {wa : Nat} (w : Nat) (a : BitVec wa) (s : Nat) :
    BitVec.ofInt w ((a.toNat : Int) * 2 ^ s) = a.setWidth w <<< s := by
  rw [← pow_cast, ← Int.natCast_mul, BitVec.ofInt_natCast]
  apply BitVec.eq_of_toNat_eq
  rw [BitVec.toNat_ofNat, BitVec.toNat_shiftLeft, BitVec.toNat_setWidth, Nat.shiftLeft_eq,
    Nat.mod_mul_mod]

theorem toBus_getLsbD_ge (w d : Nat) (e : Nat → BitVec d) :
    ∀ (k i : Nat), k * d ≤ i → (toBus w d e k).getLsbD i = false := by
  intro k
  induction k with
  | zero => intro i _; simp [toBus]
  | succ k ih =>
    intro i hi
    rw [Nat.succ_mul] at hi
    simp only [toBus]
    rw [BitVec.getLsbD_or, ih i (by omega), BitVec.getLsbD_shiftLeft, BitVec.getLsbD_setWidth,
      BitVec.getLsbD_of_ge (e k) (i - k * d) (by omega)]
    simp

/-- `Op_MemToBus n [mem]`: entries `0 .. n-1`, each truncated to `w / n` bits, side by side
with entry 0 in the low bits (see `toBus_getLsbD`).  The hypothesis `n ≤ 2 ^ aw` keeps
every entry addressable. -/
theorem memtobus_spec {aw dw : Nat} (w n : Nat) (hn : n ≤ 2 ^ aw) (m : BitVec aw → BitVec dw) :
    eval_op_cert (LGraphOp.Op_MemToBus n) w [.mem (memenc m)]
      = .bv (enc (toBus w (w / n) (fun k => (m (BitVec.ofNat aw k)).setWidth (w / n)) n)) := by
  show CertVal.bv (cert_mem_to_bus w n (memenc m)) = _
  unfold cert_mem_to_bus
  congr 1
  generalize w / n = d
  have key : ∀ k, k ≤ n → mk_bv w (cert_mem_pack d (memenc m) k)
      = enc (toBus w d (fun k => (m (BitVec.ofNat aw k)).setWidth d) k) := by
    intro k
    induction k with
    | zero => intro _; simp only [cert_mem_pack, toBus, enc_zero]
    | succ k ih =>
      intro hk
      simp only [cert_mem_pack, toBus]
      have hk2 : (k : Int) < 2 ^ aw := by
        rw [← pow_cast]; exact Int.ofNat_lt.mpr (by omega)
      have hentry : bv_uint (memenc m (k : Int)) % 2 ^ d
          = (((m (BitVec.ofNat aw k)).setWidth d).toNat : Int) := by
        unfold memenc
        rw [ite_eq_left ⟨Int.natCast_nonneg _, hk2⟩, bv_uint_enc, BitVec.ofInt_natCast,
          BitVec.toNat_setWidth, Int.natCast_emod, pow_cast]
      rw [hentry, mk_bv_eq_enc_iff, BitVec.ofInt_add, ofInt_of_mk_bv (ih (by omega)),
        ofInt_mul_two_pow, BitVec.add_eq_or_of_and_eq_zero]
      apply BitVec.eq_of_getLsbD_eq
      intro i _
      rw [BitVec.getLsbD_and, BitVec.getLsbD_zero, BitVec.getLsbD_shiftLeft]
      by_cases hi : i < k * d
      · simp [hi]
      · rw [toBus_getLsbD_ge w d _ k i (by omega)]; simp
  exact key n (Nat.le_refl n)

/-- The bus is the entries side by side: bit `i` below `k * d` is bit `i % d` of entry
`i / d`, and every bit at or above `k * d` is 0. -/
theorem toBus_getLsbD (w d : Nat) (hd : 0 < d) (e : Nat → BitVec d) :
    ∀ (k i : Nat), (toBus w d e k).getLsbD i
      = (decide (i < w) && decide (i < k * d) && (e (i / d)).getLsbD (i % d)) := by
  intro k
  induction k with
  | zero => intro i; simp [toBus]
  | succ k ih =>
    intro i
    simp only [toBus]
    rw [BitVec.getLsbD_or, ih, BitVec.getLsbD_shiftLeft, BitVec.getLsbD_setWidth]
    have hdm := Nat.div_add_mod' i d
    have hml := Nat.mod_lt i hd
    by_cases hlt : i < k * d
    · have : i < (k + 1) * d := by rw [Nat.succ_mul]; omega
      simp [hlt, this]
    · by_cases hlt2 : i < (k + 1) * d
      · rw [Nat.succ_mul] at hlt2
        have hq : i / d = k := Nat.div_eq_of_lt_le (by omega) (by rw [Nat.succ_mul]; omega)
        have hr : i % d = i - k * d := by rw [hq] at hdm; omega
        have hlt2' : i < (k + 1) * d := by rw [Nat.succ_mul]; omega
        by_cases hiw : i < w
        · have : i - k * d < w := by omega
          simp [hlt, hlt2', hq, hr, hiw, this]
        · simp [hiw]
      · have : ¬ i < (k + 1) * d := hlt2
        rw [Nat.succ_mul] at hlt2
        rw [BitVec.getLsbD_of_ge (e k) (i - k * d) (by omega)]
        simp [hlt, this]

--------------------------------------------------------------------------------
-- Wrong arity, and the eval_op forms
--------------------------------------------------------------------------------

/-- The operand count each fixed-arity operator needs. -/
def fixedArity : LGraphOp → Option Nat
  | .Op_Sub | .Op_Div | .Op_UDiv | .Op_SDiv => some 2
  | .Op_LT | .Op_GT | .Op_ULT | .Op_UGT | .Op_SLT | .Op_SGT => some 2
  | .Op_SRA | .Op_Sext | .Op_GetMask => some 2
  | .Op_Not => some 1
  | .Op_MuxBool | .Op_SetMask => some 3
  | _ => none

/-- The memory operators, which only `eval_op_cert` interprets. -/
def isMemOp : LGraphOp → Bool
  | .Op_MemRead | .Op_MemWrite | .Op_MemWriteBE _ | .Op_MemLoad _ | .Op_MemToBus _ => true
  | _ => false

/-- Every fixed-arity operator given the wrong number of operands, and every memory
operator under `denote_op`, gives 0. -/
theorem default_spec (op : LGraphOp) (w : Nat) (args : List BV)
    (h : isMemOp op = true ∨ ∃ k, fixedArity op = some k ∧ args.length ≠ k) :
    denote_op op w args = enc (0#w) := by
  rw [enc_zero]
  rcases h with h | ⟨k, hk, hlen⟩
  · cases op <;> simp [isMemOp] at h <;> rfl
  · cases op <;> simp [fixedArity] at hk <;> subst hk <;>
      rcases args with _ | ⟨a, _ | ⟨b, _ | ⟨c, _ | ⟨d, rest⟩⟩⟩⟩ <;> simp at hlen <;> rfl

/-- The executable evaluator is the reference semantics, so every theorem here applies to
`eval_op` by `rw [eval_op_spec]`. -/
theorem eval_op_spec (op : LGraphOp) (w : Nat) (args : List BV) :
    eval_op op w args = denote_op op w args :=
  eval_op_correct op w args

/-- For a non-memory operator `eval_op_cert` is `denote_op` on bit-vector operands. -/
theorem eval_op_cert_spec (op : LGraphOp) (w : Nat) (args : List BV) (h : isMemOp op = false) :
    eval_op_cert op w (args.map CertVal.bv) = .bv (denote_op op w args) := by
  rw [eval_op_cert_bv, eval_op_correct]
  intro b
  cases op <;> simp [isMemOp] at h <;> simp

--------------------------------------------------------------------------------
-- Corner values.  Each check compares the certificate with the standard value.
--------------------------------------------------------------------------------

-- Division by zero gives 0 (IEEE 1800 gives X).
#guard denote_op .Op_UDiv 8 [enc (200#8), enc (0#8)] == enc (0#8)
#guard denote_op .Op_Div 8 [enc (200#8), enc (0#8)] == enc (0#8)
#guard denote_op .Op_SDiv 8 [enc (200#8), enc (0#8)] == enc (0#8)
#guard (200#8) / (0#8) == 0#8
#guard (200#8).sdiv (0#8) == 0#8
-- MIN / -1 wraps to MIN at the operand width.
#guard denote_op .Op_SDiv 8 [enc (BitVec.intMin 8), enc (-1#8)] == enc ((BitVec.intMin 8).sdiv (-1#8))
#guard denote_op .Op_SDiv 8 [enc (BitVec.intMin 8), enc (-1#8)] == enc (BitVec.intMin 8)
-- A wider result does not wrap.
#guard denote_op .Op_SDiv 9 [enc (BitVec.intMin 8), enc (-1#8)] == enc (128#9)
#guard denote_op .Op_SDiv 9 [enc (BitVec.intMin 8), enc (-1#8)]
  == enc (((BitVec.intMin 8).signExtend 9).sdiv ((-1#8).signExtend 9))
#guard denote_op .Op_SDiv 8 [enc (-7#8), enc (2#8)] == enc ((-7#8).sdiv (2#8))
#guard denote_op .Op_SDiv 8 [enc (-7#8), enc (2#8)] == enc (-3#8)
#guard denote_op .Op_UDiv 64 [enc (BitVec.allOnes 64), enc (3#2)] == enc (BitVec.allOnes 64 / 3#64)
#guard denote_op .Op_UDiv 65 [enc (BitVec.allOnes 128), enc (1#1)] == enc (BitVec.allOnes 65)
-- Widths 0, 1, 64, 65, 128.
#guard denote_op (.Op_Sum 2) 0 [enc (5#8), enc (3#8)] == enc (0#0)
#guard denote_op (.Op_Sum 2) 1 [enc (1#1), enc (1#1)] == enc (0#1)
#guard denote_op (.Op_Sum 2) 64 [enc (BitVec.allOnes 64), enc (1#64)] == enc (0#64)
#guard denote_op (.Op_Sum 2) 65 [enc (BitVec.allOnes 64), enc (1#64)] == enc (BitVec.ofNat 65 (2 ^ 64))
#guard denote_op (.Op_Sum 1) 64 [enc (0#64), enc (1#64)] == enc (BitVec.allOnes 64)
#guard denote_op .Op_Sub 65 [enc (0#64), enc (1#64)] == enc (BitVec.allOnes 65)
#guard denote_op .Op_Mult 128 [enc (BitVec.allOnes 64), enc (BitVec.allOnes 64)]
  == enc ((BitVec.allOnes 64).setWidth 128 * (BitVec.allOnes 64).setWidth 128)
#guard denote_op .Op_Mult 64 [enc (BitVec.allOnes 64), enc (BitVec.allOnes 64)] == enc (1#64)
#guard denote_op .Op_Not 1 [enc (0#1)] == enc (1#1)
#guard denote_op .Op_Not 0 [enc (0#1)] == enc (0#0)
#guard denote_op .Op_Not 65 [enc (0#64)] == enc (~~~((0#64).setWidth 65))
-- Shift left by 0, w-1, w and far past w.  The core side is not evaluated at 2^33,
-- which would build a 2^33-bit number; `shl1_amount_ge` covers it.
#guard denote_op .Op_SHL 8 [enc (0xFF#8), enc (0#4)] == enc (0xFF#8)
#guard denote_op .Op_SHL 8 [enc (0xFF#8), enc (7#4)] == enc (0x80#8)
#guard denote_op .Op_SHL 8 [enc (0xFF#8), enc (8#4)] == enc (0#8)
#guard denote_op .Op_SHL 8 [enc (0xFF#8), enc (1000#16)] == enc ((0xFF#8) <<< 1000)
#guard denote_op .Op_SHL 8 [enc (0xFF#8), enc (BitVec.ofNat 40 (2 ^ 33))] == enc (0#8)
#guard denote_op .Op_SHL 65 [enc (BitVec.allOnes 64), enc (1#1)] == enc ((BitVec.allOnes 64).setWidth 65 <<< 1)
-- Arithmetic shift right by 0, wa-1, wa and far past wa, narrower and wider results.
#guard denote_op .Op_SRA 8 [enc (0x80#8), enc (0#4)] == enc (0x80#8)
#guard denote_op .Op_SRA 8 [enc (0x80#8), enc (7#4)] == enc (0xFF#8)
#guard denote_op .Op_SRA 8 [enc (0x80#8), enc (8#4)] == enc (0xFF#8)
#guard denote_op .Op_SRA 8 [enc (0x80#8), enc (BitVec.ofNat 40 (2 ^ 33))] == enc (0xFF#8)
#guard denote_op .Op_SRA 8 [enc (0x40#8), enc (BitVec.ofNat 40 (2 ^ 33))] == enc (0#8)
#guard denote_op .Op_SRA 16 [enc (0x80#8), enc (1#4)] == enc (0xFFC0#16)
#guard denote_op .Op_SRA 4 [enc (0x80#8), enc (1#4)] == enc (0#4)
#guard denote_op .Op_SRA 16 [enc (0x80#8), enc (1#4)]
  == enc (((0x80#8).sshiftRight 1).signExtend 16)
#guard denote_op .Op_SRA 128 [enc (BitVec.intMin 65), enc (64#7)] == enc (BitVec.allOnes 128)
#guard denote_op .Op_SRA 8 [enc (0#0), enc (3#4)] == enc (0#8)
-- Select out of range gives 0 (IEEE 1800 gives X).
#guard denote_op .Op_MuxN 8 [enc (2#2), enc (5#4), enc (6#8)] == enc (0#8)
#guard denote_op .Op_MuxN 8 [enc (1#2), enc (5#4), enc (6#8)] == enc (6#8)
#guard denote_op .Op_MuxBool 8 [enc (2#2), enc (5#4), enc (0x1FF#9)] == enc (0xFF#8)
-- Sext amounts 0, below wa, wa and above wa.
#guard denote_op .Op_Sext 8 [enc (0b1010#4), enc (0#4)] == enc (0#8)
#guard denote_op .Op_Sext 8 [enc (0b1010#4), enc (2#4)] == enc (0xFE#8)
#guard denote_op .Op_Sext 8 [enc (0b1010#4), enc (4#4)] == enc (0xFA#8)
#guard denote_op .Op_Sext 8 [enc (0b1010#4), enc (5#4)] == enc (0x0A#8)
#guard denote_op .Op_Sext 8 [enc (0b1010#4), enc (BitVec.ofNat 40 (2 ^ 33))] == enc (0x0A#8)
#guard denote_op .Op_Sext 2 [enc (0b1010#4), enc (4#4)] == enc (0b10#2)
-- Empty operand lists.
#guard denote_op .Op_And 8 [] == enc (0#8)
#guard denote_op .Op_Or 8 [] == enc (0#8)
#guard denote_op .Op_Xor 8 [] == enc (0#8)
#guard denote_op (.Op_Sum 0) 8 [] == enc (0#8)
#guard denote_op .Op_Mult 8 [] == enc (1#8)
#guard denote_op .Op_EQ 8 [] == enc (1#8)
#guard denote_op .Op_Ror 8 [] == enc (0#8)
#guard denote_op .Op_SHL 8 [] == enc (0#8)
#guard denote_op .Op_MuxN 8 [] == enc (0#8)
-- Bitwise folds over mixed widths.
#guard denote_op .Op_And 8 [enc (0xF0F#12), enc (0xFF#8), enc (0x3C#6)] == enc (0x0C#8)
#guard denote_op .Op_Or 8 [enc (0x100#9), enc (1#1)] == enc (1#8)
#guard denote_op .Op_Xor 4 [enc (0xF#4), enc (0x5#3), enc (0x1#1)] == enc (0xB#4)
#guard denote_op .Op_Ror 1 [enc (0#64), enc (0#3)] == enc (0#1)
#guard denote_op .Op_Ror 1 [enc (0#64), enc (BitVec.intMin 65)] == enc (1#1)
-- Comparisons: unsigned at the common width, signed at each operand's own width.
#guard denote_op .Op_ULT 1 [enc (0xF#4), enc (1#8)] == enc (0#1)
#guard denote_op .Op_LT 1 [enc (1#8), enc (0xF#4)] == enc (1#1)
#guard denote_op .Op_UGT 1 [enc (0xF#4), enc (1#8)] == enc (1#1)
#guard denote_op .Op_SLT 1 [enc (0xF#4), enc (1#8)] == enc (1#1)
#guard denote_op .Op_SGT 1 [enc (0xF#4), enc (0xFF#8)] == enc (0#1)
#guard denote_op .Op_SLT 1 [enc (BitVec.intMin 64), enc (0#65)] == enc (1#1)
#guard denote_op .Op_EQ 1 [enc (0xF#4), enc (15#8), enc (15#65)] == enc (1#1)
#guard denote_op .Op_EQ 1 [enc (0xF#4), enc (0xFF#8)] == enc (0#1)
-- GetMask: contiguous, scattered and all-ones masks.
#guard denote_op .Op_GetMask 8 [enc (0b11010110#8), enc (0b00111100#8)] == enc (0b0101#8)
#guard denote_op .Op_GetMask 8 [enc (0b11010110#8), enc (0b10000001#8)] == enc (0b10#8)
#guard denote_op .Op_GetMask 4 [enc (0xABC#12), enc (BitVec.allOnes 12)] == enc (0xC#4)
#guard denote_op .Op_GetMask 16 [enc (0xABC#12), enc (BitVec.allOnes 16)] == enc (0x0ABC#16)
#guard denote_op .Op_GetMask 8 [enc (0b11010110#8), enc (0b00111100#8)]
  == enc (((0b11010110#8).extractLsb' 2 4).setWidth 8)
-- SetMask: contiguous and scattered masks, and mask bits past the width of `a`.
#guard denote_op .Op_SetMask 8 [enc (0#8), enc (0b00111100#8), enc (0b1011#4)] == enc (0b00101100#8)
#guard denote_op .Op_SetMask 8 [enc (0xFF#8), enc (0b10000001#8), enc (0b00#2)] == enc (0x7E#8)
#guard denote_op .Op_SetMask 4 [enc (0#4), enc (0xF0#8), enc (0xF#4)] == enc (0#4)
#guard denote_op .Op_SetMask 8 [enc (0#4), enc (0x3#8), enc (0x3#4)] == enc (0x3#8)
-- Wrong arity gives 0.
#guard denote_op .Op_Sub 8 [enc (1#8)] == enc (0#8)
#guard denote_op .Op_MuxBool 8 [enc (1#1), enc (1#8)] == enc (0#8)
#guard denote_op .Op_MemRead 8 [enc (1#8), enc (1#8), enc (1#1)] == enc (0#8)

-- The right-hand sides of the theorems at corner values.
#guard ((200#8).setWidth 8 / (0#8).setWidth 8).setWidth 8 == 0#8
#guard (((BitVec.intMin 8).signExtend 8).sdiv ((-1#8).signExtend 8)).setWidth 8 == BitVec.intMin 8
#guard (((BitVec.intMin 8).signExtend 9).sdiv ((-1#8).signExtend 9)).setWidth 9 == 128#9
#guard BitVec.ofInt 8 ((BitVec.intMin 8).toInt.tdiv (-1#8).toInt) == BitVec.intMin 8
#guard BitVec.ofInt 8 ((-7#8).toInt.tdiv (0#8).toInt) == 0#8
#guard (0xFF#8).setWidth 8 <<< 0 == 0xFF#8
#guard (0xFF#8).setWidth 8 <<< 7 == 0x80#8
#guard (0xFF#8).setWidth 8 <<< 8 == 0#8
#guard ((0x80#8).sshiftRight 0).signExtend 8 == 0x80#8
#guard ((0x80#8).sshiftRight 7).signExtend 8 == 0xFF#8
#guard ((0x80#8).sshiftRight 8).signExtend 16 == 0xFFFF#16
#guard ((0x80#8).sshiftRight 1).setWidth 4 == 0#4
#guard ((0b1010#4).setWidth 0).signExtend 8 == 0#8
#guard ((0b1010#4).setWidth 2).signExtend 8 == 0xFE#8
#guard ((0b1010#4).setWidth 4).signExtend 8 == 0xFA#8
#guard ((0b1010#4).setWidth 5).signExtend 8 == 0x0A#8
#guard (([⟨4, 5#4⟩, ⟨8, 6#8⟩] : List (Σ k, BitVec k))[2]?.map (fun p => p.2.setWidth 8)).getD 0#8
  == 0#8
#guard ((BitVec.ofBool ((0xF#4).signExtend 8 |>.slt ((1#8).signExtend 8))).setWidth 1) == 1#1
#guard ((BitVec.ofBool ((0xF#4).setWidth 8 |>.ult ((1#8).setWidth 8))).setWidth 1) == 0#1

-- Multi-amount SHL: the certificate XORs the shifted copies, `sem_shl_many` ORs them.
-- They differ when the copies overlap or repeat.
#guard denote_op .Op_SHL 4 [enc (3#4), enc (0#2), enc (1#2)] == enc (5#4)
#guard denote_op .Op_SHL 4 [enc (1#4), enc (0#2), enc (0#2)] == enc (0#4)
#guard sem_shl_many (3#4) [0#2, 1#2] == 7#4
#guard sem_shl_many (1#4) [0#2, 0#2] == 1#4

-- Memory: read, whole-array read and load, and the byte-enable merge.
-- A memory entry past `n` is untouched by a load; an address outside the image reads 0.
#guard match eval_op_cert .Op_MemRead 8
    [.mem (memenc (fun x : BitVec 2 => x.setWidth 8 * 3#8)), .bv (enc (3#2)), .bv (enc (1#1))] with
  | .bv b => b == enc (9#8) | .mem _ => false
#guard match eval_op_cert .Op_MemRead 8
    [.mem (memenc (fun x : BitVec 2 => x.setWidth 8 * 3#8)), .bv (enc (3#2)), .bv (enc (0#1))] with
  | .bv b => b == enc (0#8) | .mem _ => false
#guard match eval_op_cert (.Op_MemToBus 4) 32
    [.mem (memenc (fun x : BitVec 2 => x.setWidth 8 + 0x10#8))] with
  | .bv b => b == enc (0x13121110#32) | .mem _ => false
#guard match eval_op_cert (.Op_MemLoad 4) 8
    [.mem (memenc (fun _ : BitVec 2 => 0#8)), .bv (enc (0x13121110#32)), .bv (enc (1#1))] with
  | .mem f => f 2 == enc (0x12#8) && f 4 == mk_bv 8 0 | .bv _ => false
#guard cert_masked_update 8 (enc (0#8)) (enc (0xFF#8)) (enc (0b10#2)) 4 == enc (0xF0#8)
#guard masked_word_update (0#8) (0xFF#8) (0b10#2) 4 == 0xF0#8
-- Byte width 0 is outside `memwrite_be_spec`: there the two merges differ.
#guard cert_masked_update 8 (enc (0#8)) (enc (0xFF#8)) (enc (1#1)) 0 == enc (0xFF#8)
#guard masked_word_update (0#8) (0xFF#8) (1#1) 0 == 0#8

end OpSpec

-- the axiom gate: every theorem above uses only propext, Classical.choice and Quot.sound
#audit_axioms OpSpec.pow_cast
#audit_axioms OpSpec.pow_pos'
#audit_axioms OpSpec.mk_bv_congr
#audit_axioms OpSpec.mk_bv_emod
#audit_axioms OpSpec.toNat_lt_cast
#audit_axioms OpSpec.toNat_emod_cast
#audit_axioms OpSpec.enc_eq
#audit_axioms OpSpec.enc_width
#audit_axioms OpSpec.bv_uint_enc
#audit_axioms OpSpec.enc_inj
#audit_axioms OpSpec.mk_bv_ofInt
#audit_axioms OpSpec.mk_bv_ofNat
#audit_axioms OpSpec.enc_zero
#audit_axioms OpSpec.ofInt_toNat
#audit_axioms OpSpec.bv_resize_enc
#audit_axioms OpSpec.bv_sint_enc
#audit_axioms OpSpec.bv_bit_eq_testBit
#audit_axioms OpSpec.bv_bit_enc
#audit_axioms OpSpec.bv_nonzero_enc
#audit_axioms OpSpec.bits_to_int_succ
#audit_axioms OpSpec.bits_to_int_eq_bitsNat
#audit_axioms OpSpec.bitsNat_lt
#audit_axioms OpSpec.bitsNat_testBit
#audit_axioms OpSpec.bitsNat_eq_toNat
#audit_axioms OpSpec.mk_bv_bits
#audit_axioms OpSpec.bv_bitwise_enc
#audit_axioms OpSpec.mk_bv_eq_enc_iff
#audit_axioms OpSpec.ofInt_of_mk_bv
#audit_axioms OpSpec.mk_bv_bool
#audit_axioms OpSpec.mk_bv_boolB
#audit_axioms OpSpec.const_spec
#audit_axioms OpSpec.map_uint_encS
#audit_axioms OpSpec.ofInt_sum
#audit_axioms OpSpec.ofInt_prod
#audit_axioms OpSpec.sum_spec
#audit_axioms OpSpec.sum2_spec
#audit_axioms OpSpec.sum1_spec
#audit_axioms OpSpec.sub_spec
#audit_axioms OpSpec.mult_spec
#audit_axioms OpSpec.mult2_spec
#audit_axioms OpSpec.intAbs_toNat
#audit_axioms OpSpec.trunc_div_int_eq_tdiv
#audit_axioms OpSpec.toNat_setWidth_le
#audit_axioms OpSpec.setWidth_ofInt_of_le
#audit_axioms OpSpec.uint_quot
#audit_axioms OpSpec.udiv_spec_nat
#audit_axioms OpSpec.div_spec_nat
#audit_axioms OpSpec.udiv_common
#audit_axioms OpSpec.udiv_spec
#audit_axioms OpSpec.div_spec
#audit_axioms OpSpec.udiv_by_zero
#audit_axioms OpSpec.div_by_zero
#audit_axioms OpSpec.sdiv_eq_ofInt_tdiv
#audit_axioms OpSpec.sdiv_spec_int
#audit_axioms OpSpec.sdiv_spec
#audit_axioms OpSpec.sdiv_spec_common
#audit_axioms OpSpec.sdiv_by_zero
#audit_axioms OpSpec.sdiv_intMin_neg_one
#audit_axioms OpSpec.bv_bitwise_step
#audit_axioms OpSpec.foldl_bitwise
#audit_axioms OpSpec.bv_bit_encS_setWidth
#audit_axioms OpSpec.and_spec
#audit_axioms OpSpec.and_nil_spec
#audit_axioms OpSpec.or_spec
#audit_axioms OpSpec.xor_spec
#audit_axioms OpSpec.and2_spec
#audit_axioms OpSpec.or2_spec
#audit_axioms OpSpec.xor2_spec
#audit_axioms OpSpec.not_spec
#audit_axioms OpSpec.ror_spec
#audit_axioms OpSpec.ult_spec
#audit_axioms OpSpec.lt_spec
#audit_axioms OpSpec.ugt_spec
#audit_axioms OpSpec.gt_spec
#audit_axioms OpSpec.slt_spec
#audit_axioms OpSpec.sgt_spec
#audit_axioms OpSpec.setWidth_eq_iff
#audit_axioms OpSpec.eq_spec
#audit_axioms OpSpec.eq_nil_spec
#audit_axioms OpSpec.eq2_spec
#audit_axioms OpSpec.shl_term
#audit_axioms OpSpec.shl_spec
#audit_axioms OpSpec.shl_nil_spec
#audit_axioms OpSpec.shl1_spec
#audit_axioms OpSpec.shl1_amount_ge
#audit_axioms OpSpec.shl2_spec
#audit_axioms OpSpec.shl_xor_ne_or
#audit_axioms OpSpec.neg_one_ediv
#audit_axioms OpSpec.toInt_zero_width
#audit_axioms OpSpec.sra_guard
#audit_axioms OpSpec.signExtend_eq_ofInt
#audit_axioms OpSpec.sra_spec
#audit_axioms OpSpec.signExtend_eq_setWidth_of_le
#audit_axioms OpSpec.sra_spec_narrow
#audit_axioms OpSpec.sra_spec_wide
#audit_axioms OpSpec.sra_amount_zero
#audit_axioms OpSpec.sra_amount_ge
#audit_axioms OpSpec.muxbool_spec
#audit_axioms OpSpec.muxn_spec
#audit_axioms OpSpec.muxn_out_of_range
#audit_axioms OpSpec.muxn_nil_spec
#audit_axioms OpSpec.toInt_setWidth
#audit_axioms OpSpec.sext_spec
#audit_axioms OpSpec.mask_indices_enc
#audit_axioms OpSpec.one_shiftLeft_getLsbD
#audit_axioms OpSpec.ofInt_two_pow
#audit_axioms OpSpec.pack_low_getLsbD_ge
#audit_axioms OpSpec.pack_low_enc
#audit_axioms OpSpec.getmask_spec
#audit_axioms OpSpec.pack_low_getLsbD
#audit_axioms OpSpec.sem_get_mask_getLsbD
#audit_axioms OpSpec.range_split
#audit_axioms OpSpec.mask_indices_range
#audit_axioms OpSpec.range'_getElem?
#audit_axioms OpSpec.getmask_contiguous
#audit_axioms OpSpec.extractLsb'_zero_eq_setWidth
#audit_axioms OpSpec.getmask_allOnes
#audit_axioms OpSpec.ofInt_neg_one_eq_allOnes
#audit_axioms OpSpec.bit_mask_at_getLsbD
#audit_axioms OpSpec.set_bit_enc
#audit_axioms OpSpec.sem_set_mask_eq
#audit_axioms OpSpec.set_mask_fold_enc
#audit_axioms OpSpec.setmask_spec
#audit_axioms OpSpec.setMaskStep_getLsbD
#audit_axioms OpSpec.fold_setMaskStep_getLsbD
#audit_axioms OpSpec.fold_zip_range'
#audit_axioms OpSpec.mask_indices_nodup
#audit_axioms OpSpec.mem_mask_indices
#audit_axioms OpSpec.sem_set_mask_getLsbD
#audit_axioms OpSpec.idxOf_append_cons
#audit_axioms OpSpec.idxOf_mask_indices
#audit_axioms OpSpec.sem_set_mask_getLsbD_rank
#audit_axioms OpSpec.idxOf_range'
#audit_axioms OpSpec.setmask_contiguous
#audit_axioms OpSpec.addr_in_range
#audit_axioms OpSpec.ofInt_toNat_self
#audit_axioms OpSpec.toNat_ofInt_in_range
#audit_axioms OpSpec.memenc_at
#audit_axioms OpSpec.memread_spec
#audit_axioms OpSpec.memwrite_spec
#audit_axioms OpSpec.ones_low_eq
#audit_axioms OpSpec.byte_mask_word_getLsbD
#audit_axioms OpSpec.fold_mask_getLsbD
#audit_axioms OpSpec.byte_enable_mask_getLsbD
#audit_axioms OpSpec.masked_word_update_getLsbD
#audit_axioms OpSpec.cert_masked_update_enc
#audit_axioms OpSpec.memwrite_be_spec
#audit_axioms OpSpec.extract_eq_ofNat
#audit_axioms OpSpec.memload_spec
#audit_axioms OpSpec.ofInt_mul_two_pow
#audit_axioms OpSpec.toBus_getLsbD_ge
#audit_axioms OpSpec.memtobus_spec
#audit_axioms OpSpec.toBus_getLsbD
#audit_axioms OpSpec.default_spec
#audit_axioms OpSpec.eval_op_spec
#audit_axioms OpSpec.eval_op_cert_spec
