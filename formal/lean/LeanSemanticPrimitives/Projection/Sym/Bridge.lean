import LeanSemanticPrimitives.Translation.LGraphModel

/-!
# `Sym.Bridge` -- LGraphModel's runtime-width `BV` against core `BitVec`

Mathlib-free. Every statement is about a concrete `BV` whose NORMALIZED value
(`bv_uint`) equals a `BitVec`'s `toNat`: that is the relation the symbolic
layer keeps, so the raw (possibly unnormalized) field never matters.
-/

namespace Projection.Sym

theorem two_pow_pos_int (w : Nat) : (0 : Int) < 2 ^ w := Int.pow_pos (by decide)

theorem two_pow_cast (w : Nat) : ((2 : Int) ^ w) = ((2 ^ w : Nat) : Int) := by
  simp [Int.natCast_pow]

theorem bv_uint_nonneg' (b : BV) : 0 ≤ bv_uint b :=
  Int.emod_nonneg _ (Int.ne_of_gt (two_pow_pos_int _))

theorem bv_uint_lt' (b : BV) : bv_uint b < 2 ^ b.width :=
  Int.emod_lt_of_pos _ (two_pow_pos_int _)

theorem bv_uint_mk_bv (w : Nat) (z : Int) : bv_uint (mk_bv w z) = z % 2 ^ w := by
  simp [bv_uint, mk_bv]

/-- The normalized value of `mk_bv w (X.toNat)`... and any `BV` relates to its
own normalized value. -/
theorem testBit_eq_bv_bit {b : BV} {w : Nat} {X : BitVec w}
    (h : bv_uint b = (X.toNat : Int)) (i : Nat) : bv_bit b i = X.getLsbD i := by
  unfold bv_bit
  rw [h, Int.toNat_natCast, Nat.shiftRight_eq_div_pow, BitVec.getLsbD, Nat.testBit]
  simp only [Nat.shiftRight_eq_div_pow, Nat.one_and_eq_mod_two]
  cases h2 : X.toNat / 2 ^ i % 2 == 1 <;> simp_all

/-! ## `bits_to_int` -/

theorem bits_to_int_succ (w : Nat) (f : Nat → Bool) :
    bits_to_int (w + 1) f = bits_to_int w f + (if f w then (2:Int) ^ w else 0) := by
  unfold bits_to_int
  rw [List.range_succ, List.filterMap_append, List.sum_append]
  by_cases hfw : f w <;> simp [hfw]

theorem bits_to_int_nonneg (w : Nat) (f : Nat → Bool) : 0 ≤ bits_to_int w f := by
  induction w with
  | zero => simp [bits_to_int]
  | succ w ih =>
    rw [bits_to_int_succ]
    have : (0:Int) ≤ (if f w then (2:Int)^w else 0) := by
      split
      · exact Int.le_of_lt (two_pow_pos_int w)
      · exact Int.le_refl 0
    omega

theorem bits_to_int_lt (w : Nat) (f : Nat → Bool) : bits_to_int w f < 2 ^ w := by
  induction w with
  | zero => simp [bits_to_int]
  | succ w ih =>
    rw [bits_to_int_succ, Int.pow_succ]
    have : (if f w then (2:Int)^w else 0) ≤ 2 ^ w := by
      split
      · exact Int.le_refl _
      · exact Int.le_of_lt (two_pow_pos_int w)
    omega

theorem two_pow_or_add {k p : Nat} (hp : p < 2 ^ k) : 2 ^ k ||| p = 2 ^ k + p := by
  have := Nat.two_pow_add_eq_or_of_lt hp 1
  simp at this
  rw [this, Nat.or_comm]


theorem bits_to_int_testBit (w : Nat) (f : Nat → Bool) (j : Nat) :
    (bits_to_int w f).toNat.testBit j = (decide (j < w) && f j) := by
  induction w generalizing j with
  | zero => simp [bits_to_int]
  | succ w ih =>
    have hB0 : 0 ≤ bits_to_int w f := bits_to_int_nonneg w f
    have hBlt : (bits_to_int w f).toNat < 2 ^ w := by
      have h := bits_to_int_lt w f
      rw [two_pow_cast] at h; omega
    rw [bits_to_int_succ]
    by_cases hfw : f w
    · simp only [hfw, if_true]
      have hcast : (bits_to_int w f + (2:Int) ^ w).toNat = (bits_to_int w f).toNat + 2 ^ w := by
        rw [two_pow_cast]; omega
      rw [hcast, Nat.add_comm, ← two_pow_or_add hBlt, Nat.testBit_or, Nat.testBit_two_pow, ih]
      rcases Nat.lt_trichotomy j w with h | h | h
      · have e1 : ¬ (w = j) := by omega
        simp [e1, h, Nat.lt_succ_of_lt h]
      · subst h; simp [hfw]
      · have e1 : ¬ (w = j) := by omega
        have e2 : ¬ (j < w) := by omega
        have e3 : ¬ (j < w + 1) := by omega
        simp [e1, e2, e3]
    · have hfw' : f w = false := by simpa using hfw
      rw [hfw']; simp only [Bool.false_eq_true, if_false, Int.add_zero]
      rw [ih]
      rcases Nat.lt_trichotomy j w with h | h | h
      · simp [h, Nat.lt_succ_of_lt h]
      · subst h; simp [hfw']
      · have e2 : ¬ (j < w) := by omega
        have e3 : ¬ (j < w + 1) := by omega
        simp [e2, e3]

theorem bits_to_int_toNat {w : Nat} (X : BitVec w) :
    bits_to_int w (fun i => X.getLsbD i) = (X.toNat : Int) := by
  have hnn := bits_to_int_nonneg w (fun i => X.getLsbD i)
  have key : (bits_to_int w (fun i => X.getLsbD i)).toNat = X.toNat := by
    apply Nat.eq_of_testBit_eq
    intro j
    rw [bits_to_int_testBit]
    by_cases hj : j < w
    · simp only [hj, decide_true, Bool.true_and, BitVec.getLsbD]
    · have hge : X.getLsbD j = false := BitVec.getLsbD_of_ge X j (by omega)
      have hge2 : X.toNat.testBit j = false := by rw [BitVec.getLsbD] at hge; exact hge
      simp [hj, hge2]
  rw [← key]; exact (Int.toNat_of_nonneg hnn).symm

theorem filterMap_congr_mem {α β : Type} {f g : α → Option β} :
    ∀ (l : List α), (∀ x ∈ l, f x = g x) → l.filterMap f = l.filterMap g
  | [],      _ => rfl
  | x :: xs, h => by
      simp only [List.filterMap_cons, h x List.mem_cons_self]
      rw [filterMap_congr_mem xs (fun y hy => h y (List.mem_cons_of_mem _ hy))]

theorem bits_to_int_congr {w : Nat} {f g : Nat → Bool} (h : ∀ i, i < w → f i = g i) :
    bits_to_int w f = bits_to_int w g := by
  unfold bits_to_int
  congr 1
  apply filterMap_congr_mem
  intro i hi; rw [List.mem_range] at hi; rw [h i hi]

/-- A `bits_to_int` whose bits are `Z`'s, normalized at its own width. -/
theorem uint_mk_bits {w : Nat} {f : Nat → Bool} (Z : BitVec w) (h : ∀ i, i < w → f i = Z.getLsbD i) :
    bv_uint (mk_bv w (bits_to_int w f)) = (Z.toNat : Int) := by
  rw [bv_uint_mk_bv, bits_to_int_congr h, bits_to_int_toNat]
  exact Int.emod_eq_of_lt (by omega) (by rw [two_pow_cast]; have := Z.isLt; omega)

theorem bitwise_rel {a b : BV} {wa wb : Nat} {A : BitVec wa} {B : BitVec wb}
    (ha : bv_uint a = (A.toNat : Int)) (hb : bv_uint b = (B.toNat : Int))
    (w : Nat) (f : Bool → Bool → Bool) (Z : BitVec w)
    (hz : ∀ i, i < w → Z.getLsbD i = f (A.getLsbD i) (B.getLsbD i)) :
    bv_uint (bv_bitwise w f a b) = (Z.toNat : Int) := by
  unfold bv_bitwise
  apply uint_mk_bits
  intro i hi
  rw [hz i hi, testBit_eq_bv_bit ha, testBit_eq_bv_bit hb]

theorem not_rel {a : BV} {wa : Nat} {A : BitVec wa} (ha : bv_uint a = (A.toNat : Int))
    (w : Nat) (Z : BitVec w) (hz : ∀ i, i < w → Z.getLsbD i = !(A.getLsbD i)) :
    bv_uint (bv_not w a) = (Z.toNat : Int) := by
  unfold bv_not
  apply uint_mk_bits
  intro i hi
  rw [hz i hi, testBit_eq_bv_bit ha]; simp

theorem resize_rel {a : BV} {wa : Nat} {A : BitVec wa} (ha : bv_uint a = (A.toNat : Int)) (w : Nat) :
    bv_uint (bv_resize w a) = ((A.setWidth w).toNat : Int) := by
  unfold bv_resize
  rw [bv_uint_mk_bv, ha, BitVec.toNat_setWidth, two_pow_cast]
  simp [Int.natCast_emod]

theorem mk_rel (w : Nat) (z : Int) :
    bv_uint (mk_bv w z) = ((BitVec.ofInt w z).toNat : Int) := by
  rw [bv_uint_mk_bv, BitVec.toNat_ofInt]
  have h := Int.emod_nonneg z (Int.ne_of_gt (two_pow_pos_int w))
  rw [two_pow_cast] at h ⊢
  omega

theorem sint_rel {a : BV} {w : Nat} {A : BitVec w} (hw : a.width = w)
    (ha : bv_uint a = (A.toNat : Int)) : bv_sint a = A.toInt := by
  unfold bv_sint
  simp only [hw, ha]
  rw [BitVec.toInt_eq_toNat_bmod, Int.bmod_def]
  have hlt := A.isLt
  by_cases hw0 : w = 0
  · subst hw0; simp at hlt; simp [hlt]
  · simp only [hw0, if_false]
    have hmod : ((A.toNat : Int) % ((2 ^ w : Nat) : Int)) = A.toNat := Int.emod_eq_of_lt (by omega) (by omega)
    rw [hmod]
    have h2 : (2 : Int) ^ w = 2 * 2 ^ (w - 1) := by
      rw [← Int.pow_succ']; congr 1; omega
    have h3 : (((2 ^ w : Nat) : Int) + 1) / 2 = 2 ^ (w - 1) := by
      rw [← two_pow_cast, h2]; omega
    rw [h3, ← two_pow_cast]

/-! ## Shifts -/

theorem shl_rel {a s : BV} {wa ws : Nat} {A : BitVec wa} {S : BitVec ws}
    (ha : bv_uint a = (A.toNat : Int)) (hs : bv_uint s = (S.toNat : Int)) (w : Nat) :
    bv_uint (bv_shl_step w a s) = (((A.setWidth w) <<< S.toNat).toNat : Int) := by
  unfold bv_shl_step
  rw [bv_uint_mk_bv, hs, ha, Int.toNat_natCast, BitVec.toNat_shiftLeft, BitVec.toNat_setWidth,
    Nat.shiftLeft_eq, Nat.mod_mul_mod]
  split
  · rename_i hk
    have hd : 2 ^ w ∣ A.toNat * 2 ^ S.toNat := Nat.dvd_mul_left_of_dvd (Nat.pow_dvd_pow 2 hk) _
    rw [Nat.mod_eq_zero_of_dvd hd]; simp
  · rw [two_pow_cast]
    simp [Int.natCast_emod, Int.natCast_mul, Int.natCast_pow]

theorem sra_rel {x s : BV} {wx ws : Nat} {X : BitVec wx} {S : BitVec ws}
    (hw : x.width = wx) (hx : bv_uint x = (X.toNat : Int)) (hs : bv_uint s = (S.toNat : Int)) (w : Nat) :
    bv_uint (bv_sra w x s) = (((X.sshiftRight S.toNat).signExtend w).toNat : Int) := by
  have hsint := sint_rel hw hx
  unfold bv_sra
  simp only [hs, hw, Int.toNat_natCast]
  rw [BitVec.signExtend, ← mk_rel, BitVec.toInt_sshiftRight, Int.shiftRight_eq_div_pow,
    show ((2 ^ S.toNat : Nat) : Int) = (2 : Int) ^ S.toNat by simp]
  split
  · rename_i hk
    congr 2
    have hlo := BitVec.le_toInt X
    have hhi := BitVec.toInt_lt (x := X)
    have hpow : (2 : Int) ^ (wx - 1) ≤ 2 ^ S.toNat := by
      rw [two_pow_cast, two_pow_cast]
      exact Int.ofNat_le.mpr (Nat.pow_le_pow_right (by decide) (by omega))
    have hpos := two_pow_pos_int S.toNat
    by_cases hw0 : wx = 0
    · subst hw0
      have : X.toInt = 0 := by simp [BitVec.toInt, BitVec.toNat_of_zero_length]
      simp [this]
    · simp only [hw0, if_false, hsint]
      split
      · rename_i hnn
        exact (Int.ediv_eq_zero_of_lt hnn (by omega)).symm
      · rename_i hneg
        have h1 : X.toInt / 2 ^ S.toNat < 0 := Int.ediv_neg_of_neg_of_pos (by omega) hpos
        have h2 : -1 ≤ X.toInt / 2 ^ S.toNat := (Int.le_ediv_iff_mul_le hpos).mpr (by omega)
        omega
  · rw [hsint]

/-! ## Get_mask -/

theorem pack_low_bits (x : BV) :
    ∀ (is : List Nat),
      pack_low_bv x is = bits_to_int is.length (fun j => bv_bit x (is.getD (is.length - 1 - j) 0))
  | [] => by simp [pack_low_bv, bits_to_int]
  | i :: is => by
      rw [pack_low_bv, pack_low_bits x is, List.length_cons, bits_to_int_succ]
      have hc : bits_to_int is.length (fun j => bv_bit x ((i :: is).getD (is.length + 1 - 1 - j) 0))
          = bits_to_int is.length (fun j => bv_bit x (is.getD (is.length - 1 - j) 0)) := by
        apply bits_to_int_congr
        intro j hj
        have : is.length + 1 - 1 - j = (is.length - 1 - j) + 1 := by omega
        rw [this]; rfl
      rw [hc]
      simp only [show is.length + 1 - 1 - is.length = 0 by omega, List.getD_cons_zero]
      split <;> simp [Int.add_comm]

/-- `bv_get_mask` with a CONCRETE mask: the result's bit `j` is `x`'s bit at the
mask's `j`-th set position. -/
theorem getmask_rel {x : BV} {wx : Nat} {X : BitVec wx} (hx : bv_uint x = (X.toNat : Int))
    (w : Nat) (m : BV) {L : Nat} (Z : BitVec L) (hL : (mask_indices_bv m).length = L)
    (hz : ∀ j, j < L → Z.getLsbD j = X.getLsbD ((mask_indices_bv m).getD j 0)) :
    bv_uint (bv_get_mask w x m) = ((Z.setWidth w).toNat : Int) := by
  unfold bv_get_mask
  rw [pack_low_bits, List.length_reverse, hL]
  have hb : bits_to_int L (fun j => bv_bit x ((mask_indices_bv m).reverse.getD (L - 1 - j) 0))
      = (Z.toNat : Int) := by
    rw [← bits_to_int_toNat Z]
    apply bits_to_int_congr
    intro j hj
    rw [hz j hj, testBit_eq_bv_bit hx]
    congr 1
    rw [List.getD_eq_getElem?_getD, List.getElem?_reverse (by omega), List.getD_eq_getElem?_getD]
    congr 2; omega
  rw [hb, bv_uint_mk_bv, BitVec.toNat_setWidth, two_pow_cast]
  simp [Int.natCast_emod]

end Projection.Sym
