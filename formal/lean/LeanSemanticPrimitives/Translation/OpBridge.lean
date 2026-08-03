/-
  Per-operator bridge lemmas for the pass.lean fast-view bridge (step 5).

  Each generated design proves `<Top>_comb = <Top>_comb_cert` by instantiating
  `GraphRefine.evalGraph_of_localAgree` with φ = its encoded fast-model node
  values, and discharging the per-node recurrence `φ n = eval_op op w (deps.map φ)`
  with the lemmas here — one per LGraph operator — relating the width-erased
  certificate evaluator `eval_op` (LGraphModel.lean) to the native `BitVec`
  fast-model operator (SemanticPrimitives.lean), under the encoding `bvenc`.

  This module imports Mathlib (for the BitVec/Int/Nat lemma library).  It is NOT
  imported by the package root, so non-bridge generated files stay Mathlib-free;
  the emitter adds `import LeanSemanticPrimitives.Translation.OpBridge` only to
  bridge-enabled output.
-/

import Mathlib
import LeanSemanticPrimitives.SemanticPrimitives
import LeanSemanticPrimitives.Translation.LGraphModel
import LeanSemanticPrimitives.Translation.GraphRefine

namespace OpBridge

/-- Encode a fixed-width `BitVec` as a runtime-width certificate `BV`.  Matches
the `mk_bv w (Int.ofNat (BitVec.toNat _))` form the emitter uses in `sourceEnv`. -/
def bvenc {w : Nat} (x : BitVec w) : BV := mk_bv w (Int.ofNat x.toNat)

/-- The unsigned value of an encoded `BitVec` is just its `toNat`. -/
theorem bv_uint_bvenc {w : Nat} (x : BitVec w) : bv_uint (bvenc x) = Int.ofNat x.toNat := by
  unfold bvenc mk_bv bv_uint
  have hnn  : (0 : Int) ≤ Int.ofNat x.toNat := Int.natCast_nonneg _
  have hlt' : Int.ofNat x.toNat < ((2 ^ w : Nat) : Int) := Int.ofNat_lt.mpr x.isLt
  simp only []
  rw [(by simp : (2 : Int) ^ w = ((2 ^ w : Nat) : Int)),
      Int.emod_eq_of_lt hnn hlt', Int.emod_eq_of_lt hnn hlt']

/-- Decoding a freshly-encoded `BitVec` returns it unchanged (round trip). -/
theorem bv_to_bitvec_bvenc {w : Nat} (x : BitVec w) : bv_to_bitvec w (bvenc x) = x := by
  have hnn  : (0 : Int) ≤ Int.ofNat x.toNat := Int.natCast_nonneg _
  have hlt' : Int.ofNat x.toNat < ((2 ^ w : Nat) : Int) := Int.ofNat_lt.mpr x.isLt
  apply BitVec.eq_of_toNat_eq
  unfold bv_to_bitvec bvenc mk_bv bv_uint
  simp only [BitVec.toNat_ofInt]
  rw [(by simp : (2 : Int) ^ w = ((2 ^ w : Nat) : Int)),
      Int.emod_eq_of_lt hnn hlt', Int.emod_eq_of_lt hnn hlt', Int.emod_eq_of_lt hnn hlt']
  rfl

/-- Zero-extension to the same width is the identity. -/
theorem bv_zext_id {w : Nat} (x : BitVec w) : (bv_zext x : BitVec w) = x := by
  apply BitVec.eq_of_toNat_eq
  unfold bv_zext
  simp

/-- The i-th certificate bit of an encoded `BitVec` is its i-th `BitVec` bit. -/
theorem bv_bit_bvenc {w : Nat} (x : BitVec w) (i : Nat) : bv_bit (bvenc x) i = x.getLsbD i := by
  unfold bv_bit
  rw [bv_uint_bvenc]
  show decide (x.toNat >>> i % 2 = 1) = x.getLsbD i
  rw [BitVec.getLsbD, Nat.testBit, Nat.shiftRight_eq_div_pow, Nat.and_comm, Nat.and_one_is_mod]
  rcases Nat.mod_two_eq_zero_or_one (x.toNat / 2 ^ i) with h | h <;> simp [h]

--------------------------------------------------------------------------------
-- GetMask bridge building blocks (assembly of the full bridge is pending the
-- disjoint-OR-sum step for pack_low correspondence).
--------------------------------------------------------------------------------

/-- The certificate and fast mask-index lists agree under encoding. -/
theorem mask_indices_bvenc {mw : Nat} (M : BitVec mw) :
    mask_indices_bv (bvenc M) = mask_indices M := by
  unfold mask_indices_bv mask_indices
  have hw : (bvenc M).width = mw := rfl
  rw [hw]
  apply List.filter_congr
  intro i _
  rw [bv_bit_bvenc]

/-- `1#b` shifted left by `k < b` has `toNat = 2^k`. -/
theorem toNat_one_shiftLeft {b k : Nat} (hk : k < b) : (1#b <<< k).toNat = 2 ^ k := by
  have hb0 : 0 < b := Nat.lt_of_le_of_lt (Nat.zero_le _) hk
  rw [BitVec.toNat_shiftLeft, BitVec.toNat_ofNat]
  have h1lt : (1 : Nat) < 2 ^ b := by
    have h2 : 2 ^ 1 ≤ 2 ^ b := Nat.pow_le_pow_right (by norm_num) hb0
    simp only [pow_one] at h2; omega
  have hlt2 : (2 : Nat) ^ k < 2 ^ b := Nat.pow_lt_pow_right (by norm_num) hk
  rw [Nat.mod_eq_of_lt h1lt, Nat.shiftLeft_eq, one_mul, Nat.mod_eq_of_lt hlt2]

/-- `pack_low` packs into the low `is.length` bits (when they fit in `b`). -/
theorem pack_low_toNat_lt {a b : Nat} (X : BitVec a) :
    ∀ (is : List Nat), is.length ≤ b → (pack_low X is : BitVec b).toNat < 2 ^ is.length := by
  intro is
  induction is with
  | nil => intro _; simp [pack_low]
  | cons i is' ih =>
    intro hlen
    have hlt' : is'.length < b := Nat.lt_of_lt_of_le (Nat.lt_succ_self _) hlen
    have hih : (pack_low X is' : BitVec b).toNat < 2 ^ is'.length := ih (Nat.le_of_lt hlt')
    unfold pack_low
    by_cases hb : X.getLsbD i
    · simp only [hb, if_true]
      rw [BitVec.toNat_or, toNat_one_shiftLeft hlt']
      have hstep : (2 : Nat) ^ is'.length < 2 ^ (i :: is').length :=
        Nat.pow_lt_pow_right (by norm_num) (by simp)
      exact Nat.or_lt_two_pow hstep (Nat.lt_trans hih hstep)
    · simp only [hb]
      exact Nat.lt_trans hih (Nat.pow_lt_pow_right (by norm_num) (by simp))

/-- `Nat.testBit` as a div/mod predicate. -/
theorem testBit_div_mod (n j : Nat) : n.testBit j = decide (n / 2 ^ j % 2 = 1) := by
  rw [Nat.testBit, Nat.shiftRight_eq_div_pow, Nat.and_comm, Nat.and_one_is_mod]
  rcases Nat.mod_two_eq_zero_or_one (n / 2 ^ j) with h | h <;> simp [h]

/-- Disjoint OR is addition: `2^k` and `p < 2^k` share no bits. -/
theorem two_pow_or_add {k p : Nat} (hp : p < 2 ^ k) : 2 ^ k ||| p = 2 ^ k + p := by
  apply Nat.eq_of_testBit_eq
  intro j
  rw [Nat.testBit_lor, testBit_div_mod (2 ^ k) j, testBit_div_mod p j, testBit_div_mod (2 ^ k + p) j]
  have h2j : 0 < (2 : Nat) ^ j := pow_pos (by norm_num) j
  rcases lt_trichotomy j k with hjk | hjk | hjk
  · have hkj : 2 ^ k = 2 ^ j * 2 ^ (k - j) := by rw [← pow_add]; congr 1; omega
    have hdiv : (2 ^ k + p) / 2 ^ j = 2 ^ (k - j) + p / 2 ^ j := by rw [hkj, Nat.mul_add_div h2j]
    have hdiv2 : 2 ^ k / 2 ^ j = 2 ^ (k - j) := by rw [hkj, Nat.mul_div_cancel_left _ h2j]
    have heven : 2 ^ (k - j) % 2 = 0 := by
      rw [← Nat.dvd_iff_mod_eq_zero]; exact dvd_pow_self 2 (by omega)
    rw [hdiv, hdiv2]
    rcases Nat.mod_two_eq_zero_or_one (p / 2 ^ j) with h | h <;> simp [Nat.add_mod, heven, h]
  · subst hjk
    have hpd : p / 2 ^ j = 0 := Nat.div_eq_of_lt hp
    have he : (2 ^ j + p) / 2 ^ j = 1 := by rw [Nat.add_comm, Nat.add_div_right _ h2j, hpd]
    rw [he, Nat.div_self h2j, hpd]; simp
  · have h1 : 2 ^ k / 2 ^ j = 0 := Nat.div_eq_of_lt (Nat.pow_lt_pow_right (by norm_num) hjk)
    have h2 : p / 2 ^ j = 0 := Nat.div_eq_of_lt (Nat.lt_trans hp (Nat.pow_lt_pow_right (by norm_num) hjk))
    have h3 : (2 ^ k + p) / 2 ^ j = 0 := by
      apply Nat.div_eq_of_lt
      have hlt : 2 ^ k + p < 2 ^ (k + 1) := by rw [pow_succ]; omega
      exact Nat.lt_of_lt_of_le hlt (Nat.pow_le_pow_right (by norm_num) (by omega))
    rw [h1, h2, h3]; simp

/-- `mk_bv` depends only on the value modulo `2^b`. -/
theorem mk_bv_eq_of_emod {b : Nat} {x y : Int} (h : x % (2 : Int) ^ b = y % 2 ^ b) :
    mk_bv b x = mk_bv b y := by unfold mk_bv; rw [h]

theorem mk_bv_emod_eq {b : Nat} {x y : Int} (h : mk_bv b x = mk_bv b y) :
    x % (2 : Int) ^ b = y % 2 ^ b := by
  have := congrArg BV.value h; simpa [mk_bv] using this

/-- `pack_low` correspondence: the certificate `pack_low_bv` over an encoded
`BitVec` matches the fast `pack_low` (when the packed bits fit in `b`). -/
theorem pack_low_bvenc {a b : Nat} (X : BitVec a) :
    ∀ (is : List Nat), is.length ≤ b →
      mk_bv b (pack_low_bv (bvenc X) is) = bvenc (pack_low X is : BitVec b) := by
  intro is
  induction is with
  | nil => intro _; simp [pack_low_bv, pack_low, bvenc]
  | cons i is' ih =>
    intro hlen
    have hlt' : is'.length < b := Nat.lt_of_lt_of_le (Nat.lt_succ_self _) hlen
    have hih := ih (Nat.le_of_lt hlt')
    have hP : (pack_low X is' : BitVec b).toNat < 2 ^ is'.length :=
      pack_low_toNat_lt X is' (Nat.le_of_lt hlt')
    simp only [pack_low_bv, pack_low, bv_bit_bvenc]
    by_cases hb : X.getLsbD i
    · simp only [hb, if_true]
      unfold bvenc
      apply mk_bv_eq_of_emod
      have hor : ((1#b <<< is'.length) ||| (pack_low X is')).toNat
               = 2 ^ is'.length + (pack_low X is' : BitVec b).toNat := by
        rw [BitVec.toNat_or, toNat_one_shiftLeft hlt', two_pow_or_add hP]
      rw [hor]
      have hQ : (pack_low_bv (bvenc X) is') % (2 : Int) ^ b
              = (Int.ofNat (pack_low X is' : BitVec b).toNat) % 2 ^ b := by
        have := mk_bv_emod_eq hih; unfold bvenc at this; exact this
      exact Int.ModEq.add_left _ hQ
    · simp only [hb]; exact hih

/-- **GetMask bridge**: the certificate bit-select equals the fast `sem_get_mask`
under the encoding (requires the selected-bit count to fit in the output width). -/
theorem getmask_bridge {aw mw b : Nat} (X : BitVec aw) (M : BitVec mw)
    (hb : (mask_indices M).length ≤ b) :
    bv_get_mask b (bvenc X) (bvenc M) = bvenc (sem_get_mask X M : BitVec b) := by
  unfold bv_get_mask sem_get_mask
  rw [mask_indices_bvenc]
  exact pack_low_bvenc X (mask_indices M).reverse (by rw [List.length_reverse]; exact hb)

--------------------------------------------------------------------------------
-- Bitwise family (And / Or / Xor / Not): relate the certificate bits_to_int /
-- bv_bitwise / bv_not to the native BitVec ops under the encoding.
--------------------------------------------------------------------------------

theorem bits_to_int_succ (w : Nat) (f : Nat → Bool) :
    bits_to_int (w + 1) f = bits_to_int w f + (if f w then (2:Int) ^ w else 0) := by
  unfold bits_to_int
  rw [List.range_succ, List.filterMap_append, List.sum_append]
  by_cases hfw : f w <;> simp [hfw]

theorem bits_to_int_nonneg (w : Nat) (f : Nat → Bool) : 0 ≤ bits_to_int w f := by
  induction w with
  | zero => simp [bits_to_int]
  | succ w ih => rw [bits_to_int_succ]; have : (0:Int) ≤ (if f w then (2:Int)^w else 0) := by split <;> positivity
                 omega

theorem bits_to_int_lt (w : Nat) (f : Nat → Bool) : bits_to_int w f < 2 ^ w := by
  induction w with
  | zero => simp [bits_to_int]
  | succ w ih =>
    rw [bits_to_int_succ, pow_succ]
    have : (if f w then (2:Int)^w else 0) ≤ 2 ^ w := by split <;> first | exact le_refl _ | positivity
    omega

theorem bits_to_int_testBit (w : Nat) (f : Nat → Bool) (j : Nat) :
    (bits_to_int w f).toNat.testBit j = (decide (j < w) && f j) := by
  induction w generalizing j with
  | zero => simp [bits_to_int]
  | succ w ih =>
    have hB0 : 0 ≤ bits_to_int w f := bits_to_int_nonneg w f
    have hBlt : (bits_to_int w f).toNat < 2 ^ w := by
      have h := bits_to_int_lt w f
      have hc : (2:Int) ^ w = ((2 ^ w : Nat) : Int) := by simp
      rw [hc] at h; omega
    rw [bits_to_int_succ]
    by_cases hfw : f w
    · simp only [hfw, if_true]
      have hcast : (bits_to_int w f + (2:Int) ^ w).toNat = (bits_to_int w f).toNat + 2 ^ w := by
        have h2 : (2:Int) ^ w = ((2 ^ w : Nat) : Int) := by simp
        rw [h2, Int.toNat_add hB0 (by positivity), Int.toNat_natCast]
      rw [hcast, Nat.add_comm, ← two_pow_or_add hBlt, Nat.testBit_lor, Nat.testBit_two_pow, ih]
      rcases Nat.lt_trichotomy j w with h | h | h
      · have e1 : ¬ (w = j) := by omega
        simp [e1, h, Nat.lt_succ_of_lt h]
      · subst h; simp [hfw]
      · have e1 : ¬ (w = j) := by omega
        have e2 : ¬ (j < w) := by omega
        have e3 : ¬ (j < w + 1) := by omega
        simp [e1, e2, e3]
    · have hfw' : f w = false := by simpa using hfw
      rw [hfw']; simp only [Bool.false_eq_true, if_false, add_zero]
      rw [ih]
      rcases Nat.lt_trichotomy j w with h | h | h
      · simp [h, Nat.lt_succ_of_lt h]
      · subst h; simp [hfw']
      · have e2 : ¬ (j < w) := by omega
        have e3 : ¬ (j < w + 1) := by omega
        simp [e2, e3]

-- ============ bitwise family ============


theorem bits_to_int_toNat {w : Nat} (X : BitVec w) :
    bits_to_int w (fun i => X.getLsbD i) = Int.ofNat X.toNat := by
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

theorem bits_to_int_congr {w : Nat} {f g : Nat → Bool} (h : ∀ i, i < w → f i = g i) :
    bits_to_int w f = bits_to_int w g := by
  unfold bits_to_int; congr 1
  apply List.filterMap_congr
  intro i hi; rw [List.mem_range] at hi; rw [h i hi]

theorem getLsbD_zext_lt {wa w : Nat} (a : BitVec wa) {i : Nat} (hi : i < w) :
    (bv_zext a : BitVec w).getLsbD i = a.getLsbD i := by
  unfold bv_zext; rw [BitVec.getLsbD_ofNat]; simp [hi, BitVec.getLsbD]

theorem bv_resize_bvenc {wa w : Nat} (a : BitVec wa) :
    bv_resize w (bvenc a) = bvenc (bv_zext a : BitVec w) := by
  have hz : (bv_zext a : BitVec w).toNat = a.toNat % 2 ^ w := by unfold bv_zext; rw [BitVec.toNat_ofNat]
  unfold bv_resize; rw [bv_uint_bvenc]; unfold bvenc
  apply mk_bv_eq_of_emod
  rw [hz]
  have hc : (2:Int) ^ w = ((2 ^ w : Nat) : Int) := by simp
  simp only [Int.ofNat_eq_natCast]
  rw [hc, Int.natCast_emod, Int.emod_emod_of_dvd _ (dvd_refl _)]

theorem bv_bit_mk_bv_zero {w i : Nat} : bv_bit (mk_bv w 0) i = false := by
  unfold bv_bit mk_bv bv_uint; simp

theorem bv_bit_bitsToInt {w : Nat} (g : Nat → Bool) {i : Nat} (hi : i < w) :
    bv_bit (mk_bv w (bits_to_int w g)) i = g i := by
  have hnn := bits_to_int_nonneg w g
  have hlt := bits_to_int_lt w g
  have hval : bv_uint (mk_bv w (bits_to_int w g)) = bits_to_int w g := by
    unfold bv_uint mk_bv
    rw [Int.emod_emod_of_dvd _ (dvd_refl _)]
    exact Int.emod_eq_of_lt hnn hlt
  unfold bv_bit; rw [hval, Nat.shiftRight_eq_div_pow, ← testBit_div_mod, bits_to_int_testBit]
  simp [hi]

theorem bv_bitwise_eq {w : Nat} (f : Bool → Bool → Bool) (A B : BV) (Z : BitVec w)
    (h : ∀ i, i < w → f (bv_bit A i) (bv_bit B i) = Z.getLsbD i) :
    bv_bitwise w f A B = bvenc Z := by
  unfold bv_bitwise bvenc; congr 1
  rw [← bits_to_int_toNat Z]; exact bits_to_int_congr h

theorem bv_not_eq {w : Nat} (A : BV) (Z : BitVec w)
    (h : ∀ i, i < w → (! bv_bit A i) = Z.getLsbD i) :
    bv_not w A = bvenc Z := by
  unfold bv_not bvenc; congr 1
  rw [← bits_to_int_toNat Z]
  apply bits_to_int_congr
  intro i hi; rw [← h i hi]; simp

theorem and_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_And w [bvenc a, bvenc b]
      = bvenc ((bv_zext a : BitVec w) &&& (bv_zext b : BitVec w)) := by
  show bv_bitwise w (fun x y => x && y) (bv_resize w (bvenc a)) (bvenc b) = _
  rw [bv_resize_bvenc]
  apply bv_bitwise_eq
  intro i hi
  simp only [bv_bit_bvenc, BitVec.getLsbD_and, getLsbD_zext_lt a hi, getLsbD_zext_lt b hi]

theorem or_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_Or w [bvenc a, bvenc b]
      = bvenc ((bv_zext a : BitVec w) ||| (bv_zext b : BitVec w)) := by
  show bv_bitwise w (fun x y => x || y) (bv_bitwise w (fun x y => x || y) (mk_bv w 0) (bvenc a)) (bvenc b) = _
  apply bv_bitwise_eq
  intro i hi
  have hz : bv_bit (bv_bitwise w (fun x y => x || y) (mk_bv w 0) (bvenc a)) i = a.getLsbD i := by
    unfold bv_bitwise; rw [bv_bit_bitsToInt _ hi]; simp [bv_bit_mk_bv_zero, bv_bit_bvenc]
  simp only [hz, bv_bit_bvenc, BitVec.getLsbD_or, getLsbD_zext_lt a hi, getLsbD_zext_lt b hi]

theorem xor_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_Xor w [bvenc a, bvenc b]
      = bvenc ((bv_zext a : BitVec w) ^^^ (bv_zext b : BitVec w)) := by
  show bv_bitwise w (fun x y => xor x y) (bv_bitwise w (fun x y => xor x y) (mk_bv w 0) (bvenc a)) (bvenc b) = _
  apply bv_bitwise_eq
  intro i hi
  have hz : bv_bit (bv_bitwise w (fun x y => xor x y) (mk_bv w 0) (bvenc a)) i = a.getLsbD i := by
    unfold bv_bitwise; rw [bv_bit_bitsToInt _ hi]; simp [bv_bit_mk_bv_zero, bv_bit_bvenc]
  simp only [hz, bv_bit_bvenc, BitVec.getLsbD_xor, getLsbD_zext_lt a hi, getLsbD_zext_lt b hi]

theorem not_bridge {wa w : Nat} (a : BitVec wa) :
    eval_op LGraphOp.Op_Not w [bvenc a] = bvenc (~~~ (bv_zext a : BitVec w)) := by
  show bv_not w (bvenc a) = _
  apply bv_not_eq
  intro i hi
  rw [bv_bit_bvenc, BitVec.getLsbD_not, getLsbD_zext_lt a hi]; simp [hi]

--------------------------------------------------------------------------------
-- Arithmetic / comparison family (Sum, EQ, ULT, UGT).
--------------------------------------------------------------------------------

theorem toNat_bv_zext_le {wa cw : Nat} (a : BitVec wa) (h : wa ≤ cw) :
    (bv_zext a : BitVec cw).toNat = a.toNat := by
  unfold bv_zext; rw [BitVec.toNat_ofNat]
  exact Nat.mod_eq_of_lt (lt_of_lt_of_le a.isLt (Nat.pow_le_pow_right (by norm_num) h))

theorem bvenc_bool (P : Bool) : bvenc (bool_to_bv1 P) = mk_bv 1 (if P then 1 else 0) := by
  cases P <;> simp [bvenc, bool_to_bv1, mk_bv]

theorem sum2_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op (LGraphOp.Op_Sum 2) w [bvenc a, bvenc b]
      = bvenc ((bv_zext a : BitVec w) + (bv_zext b : BitVec w)) := by
  rw [show eval_op (LGraphOp.Op_Sum 2) w [bvenc a, bvenc b]
        = mk_bv w (Int.ofNat a.toNat + Int.ofNat b.toNat) from by simp [eval_op, bv_uint_bvenc]]
  unfold bvenc
  apply mk_bv_eq_of_emod
  rw [BitVec.toNat_add]
  simp only [bv_zext, BitVec.toNat_ofNat, Int.ofNat_eq_natCast]
  push_cast
  rw [Int.emod_emod_of_dvd _ (dvd_refl _), ← Int.add_emod]

theorem eq_bridge {wa wb ew : Nat} (a : BitVec wa) (b : BitVec wb) (ha : wa ≤ ew) (hb : wb ≤ ew) :
    eval_op LGraphOp.Op_EQ 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext b : BitVec ew) = (bv_zext a : BitVec ew))) := by
  rw [show eval_op LGraphOp.Op_EQ 1 [bvenc a, bvenc b]
        = mk_bv 1 (if b.toNat = a.toNat then (1:Int) else 0) from by simp [eval_op, bv_uint_bvenc]]
  rw [bvenc_bool]
  congr 1
  have hiff : ((bv_zext b : BitVec ew) = (bv_zext a : BitVec ew)) ↔ (b.toNat = a.toNat) := by
    rw [BitVec.toNat_eq, toNat_bv_zext_le b hb, toNat_bv_zext_le a ha]
  simp only [decide_eq_true_eq]
  by_cases h : b.toNat = a.toNat
  · rw [if_pos h, if_pos (hiff.mpr h)]
  · rw [if_neg h, if_neg (fun hc => h (hiff.mp hc))]

theorem ult_bridge {wa wb cw : Nat} (a : BitVec wa) (b : BitVec wb) (ha : wa ≤ cw) (hb : wb ≤ cw) :
    eval_op LGraphOp.Op_ULT 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext a : BitVec cw) < (bv_zext b : BitVec cw))) := by
  rw [show eval_op LGraphOp.Op_ULT 1 [bvenc a, bvenc b]
        = mk_bv 1 (if a.toNat < b.toNat then (1:Int) else 0) from by simp [eval_op, bv_uint_bvenc]]
  rw [bvenc_bool]; congr 1
  have hiff : ((bv_zext a : BitVec cw) < (bv_zext b : BitVec cw)) ↔ (a.toNat < b.toNat) := by
    rw [BitVec.lt_def, toNat_bv_zext_le a ha, toNat_bv_zext_le b hb]
  simp only [decide_eq_true_eq]
  by_cases h : a.toNat < b.toNat
  · rw [if_pos h, if_pos (hiff.mpr h)]
  · rw [if_neg h, if_neg (fun hc => h (hiff.mp hc))]

theorem ugt_bridge {wa wb cw : Nat} (a : BitVec wa) (b : BitVec wb) (ha : wa ≤ cw) (hb : wb ≤ cw) :
    eval_op LGraphOp.Op_UGT 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext a : BitVec cw) > (bv_zext b : BitVec cw))) := by
  rw [show eval_op LGraphOp.Op_UGT 1 [bvenc a, bvenc b]
        = mk_bv 1 (if b.toNat < a.toNat then (1:Int) else 0) from by simp [eval_op, bv_uint_bvenc]]
  rw [bvenc_bool]; congr 1
  have hiff : ((bv_zext a : BitVec cw) > (bv_zext b : BitVec cw)) ↔ (b.toNat < a.toNat) := by
    rw [gt_iff_lt, BitVec.lt_def, toNat_bv_zext_le a ha, toNat_bv_zext_le b hb]
  simp only [decide_eq_true_eq]
  by_cases h : b.toNat < a.toNat
  · rw [if_pos h, if_pos (hiff.mpr h)]
  · rw [if_neg h, if_neg (fun hc => h (hiff.mp hc))]

--------------------------------------------------------------------------------
-- Arithmetic shift right (SRA): signed value + arithmetic shift = floor div.
--------------------------------------------------------------------------------

theorem toNat_toInt_emod {w : Nat} (x : BitVec w) : (x.toNat : Int) % 2 ^ w = x.toInt % 2 ^ w := by
  rw [BitVec.toInt_eq_toNat_bmod, show (2:Int) ^ w = ((2 ^ w : Nat) : Int) from by simp, Int.bmod_emod]

theorem bv_sint_bvenc {w : Nat} (a : BitVec w) : bv_sint (bvenc a) = a.toInt := by
  unfold bv_sint
  rw [show (bvenc a).width = w from rfl, bv_uint_bvenc, BitVec.toInt_eq_toNat_cond]
  rcases Nat.eq_zero_or_pos w with hw | hw
  · subst hw; simp [Nat.lt_one_iff.mp a.isLt]
  · have hne : w ≠ 0 := by omega
    have hpow : (2:Nat) ^ w = 2 * 2 ^ (w - 1) := by
      conv_lhs => rw [show w = (w - 1) + 1 from by omega]
      rw [pow_succ]; ring
    rw [if_neg hne]
    have hcond : (Int.ofNat a.toNat < 2 ^ (w - 1)) ↔ (2 * a.toNat < 2 ^ w) := by
      rw [Int.ofNat_eq_natCast, show (2:Int) ^ (w - 1) = ((2 ^ (w - 1) : Nat) : Int) from by simp, Nat.cast_lt]
      omega
    by_cases hc : 2 * a.toNat < 2 ^ w
    · rw [if_pos (hcond.mpr hc), if_pos hc, Int.ofNat_eq_natCast]
    · rw [if_neg (fun h => hc (hcond.mp h)), if_neg hc, Int.ofNat_eq_natCast,
          show (2:Int) ^ w = ((2 ^ w : Nat) : Int) from by simp]

theorem mk_bv_toInt_zext {wy w : Nat} (y : BitVec wy) (hw : w ≤ wy) :
    mk_bv w y.toInt = bvenc (bv_zext y : BitVec w) := by
  unfold bvenc
  apply mk_bv_eq_of_emod
  have hz : (bv_zext y : BitVec w).toNat = y.toNat % 2 ^ w := by unfold bv_zext; rw [BitVec.toNat_ofNat]
  rw [hz]
  have key : (↑y.toNat : Int) % (2:Int) ^ w = y.toInt % (2:Int) ^ w :=
    Int.ModEq.of_dvd (pow_dvd_pow 2 hw) (toNat_toInt_emod y)
  rw [Int.ofNat_eq_natCast, ← key,
      show (2:Int) ^ w = ((2 ^ w : Nat) : Int) from by simp,
      ← Int.natCast_emod, ← Int.natCast_emod, Nat.mod_mod]

theorem sra_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) (hw : w ≤ wa) :
    eval_op LGraphOp.Op_SRA w [bvenc a, bvenc b]
      = bvenc (bv_zext (sem_sra a b) : BitVec w) := by
  have hsra : (sem_sra a b).toInt = a.toInt / (2:Int) ^ b.toNat := by
    unfold sem_sra
    rw [BitVec.toInt_sshiftRight, Int.shiftRight_eq_div_pow]; norm_num
  show bv_sra w (bvenc a) (bvenc b) = _
  unfold bv_sra
  rw [bv_sint_bvenc, bv_uint_bvenc]
  show mk_bv w (a.toInt / (2:Int) ^ b.toNat) = bvenc (bv_zext (sem_sra a b) : BitVec w)
  rw [← hsra]
  exact mk_bv_toInt_zext (sem_sra a b) hw

--------------------------------------------------------------------------------
-- Shift left (SHL): the cert xor-fold-from-zero collapses to a masked shift.
--------------------------------------------------------------------------------

theorem bv_bit_mk_bv_general {w i : Nat} (V : Int) (hi : i < w) :
    bv_bit (mk_bv w V) i = (V % 2 ^ w).toNat.testBit i := by
  unfold bv_bit bv_uint mk_bv
  rw [Int.emod_emod_of_dvd _ (dvd_refl _), Nat.shiftRight_eq_div_pow, ← testBit_div_mod]

theorem shl_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_SHL w [bvenc a, bvenc b]
      = bvenc ((bv_zext a : BitVec w) <<< b.toNat) := by
  show bv_bitwise w (fun x y => xor x y) (mk_bv w 0)
        (mk_bv w (bv_uint (bvenc a) * 2 ^ (bv_uint (bvenc b)).toNat)) = _
  rw [bv_uint_bvenc, bv_uint_bvenc]
  apply bv_bitwise_eq
  intro i hi
  rw [bv_bit_mk_bv_zero, Bool.false_xor, bv_bit_mk_bv_general _ hi]
  have hV : ((Int.ofNat a.toNat * 2 ^ (Int.ofNat b.toNat).toNat) % (2:Int) ^ w).toNat
          = (a.toNat * 2 ^ b.toNat) % 2 ^ w := by
    rw [show (Int.ofNat b.toNat).toNat = b.toNat from rfl,
        show Int.ofNat a.toNat * (2:Int) ^ b.toNat = ((a.toNat * 2 ^ b.toNat : Nat) : Int) from by simp only [Int.ofNat_eq_natCast]; push_cast; ring,
        show (2:Int) ^ w = ((2 ^ w : Nat) : Int) from by simp, ← Int.natCast_emod, Int.toNat_natCast]
  rw [hV, Nat.testBit_mod_two_pow, ← Nat.shiftLeft_eq, Nat.testBit_shiftLeft,
      BitVec.getLsbD_shiftLeft]
  simp only [hi, decide_true, Bool.true_and]
  by_cases hib : i < b.toNat
  · simp [hib, Nat.not_le.mpr hib]
  · have hlt : i - b.toNat < w := by omega
    rw [getLsbD_zext_lt a hlt, BitVec.getLsbD]
    simp [hib, Nat.not_lt.mp hib]

--------------------------------------------------------------------------------
-- Mux (2-way bool), reduction-or (1 input), signed compares (SLT/SGT).
--------------------------------------------------------------------------------

theorem bv_nonzero_bvenc {w : Nat} (x : BitVec w) : bv_nonzero (bvenc x) = bitvec_nonzero x := by
  have hiff : (bv_uint (bvenc x) ≠ 0) ↔ (x ≠ 0#w) := by
    rw [bv_uint_bvenc]; simp [BitVec.toNat_eq]
  unfold bv_nonzero bitvec_nonzero
  exact decide_eq_decide.mpr hiff

theorem muxbool_bridge {ws w1 w2 w : Nat} (sel : BitVec ws) (o1 : BitVec w1) (o2 : BitVec w2) :
    eval_op LGraphOp.Op_MuxBool w [bvenc sel, bvenc o1, bvenc o2]
      = bvenc (if bitvec_nonzero sel then (bv_zext o2 : BitVec w) else (bv_zext o1 : BitVec w)) := by
  show (if bv_nonzero (bvenc sel) then bv_resize w (bvenc o2) else bv_resize w (bvenc o1)) = _
  rw [bv_nonzero_bvenc, bv_resize_bvenc, bv_resize_bvenc]
  by_cases h : bitvec_nonzero sel <;> simp [h]

theorem ror1_bridge {wa : Nat} (a : BitVec wa) :
    eval_op LGraphOp.Op_Ror 1 [bvenc a] = bvenc (bool_to_bv1 (bitvec_nonzero a)) := by
  rw [show eval_op LGraphOp.Op_Ror 1 [bvenc a] = mk_bv 1 (if bitvec_nonzero a then (1:Int) else 0) from by
        simp [eval_op, bv_nonzero_bvenc]]
  rw [bvenc_bool]

theorem slt_bridge {cw : Nat} (a b : BitVec cw) :
    eval_op LGraphOp.Op_SLT 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext a : BitVec cw).toInt < (bv_zext b : BitVec cw).toInt)) := by
  rw [show eval_op LGraphOp.Op_SLT 1 [bvenc a, bvenc b]
        = mk_bv 1 (if a.toInt < b.toInt then (1:Int) else 0) from by simp [eval_op, bv_sint_bvenc]]
  rw [bvenc_bool]; simp only [bv_zext_id, decide_eq_true_eq]

theorem sgt_bridge {cw : Nat} (a b : BitVec cw) :
    eval_op LGraphOp.Op_SGT 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext a : BitVec cw).toInt > (bv_zext b : BitVec cw).toInt)) := by
  rw [show eval_op LGraphOp.Op_SGT 1 [bvenc a, bvenc b]
        = mk_bv 1 (if a.toInt > b.toInt then (1:Int) else 0) from by simp [eval_op, bv_sint_bvenc]]
  rw [bvenc_bool]; simp only [bv_zext_id, decide_eq_true_eq]

--------------------------------------------------------------------------------
-- Sign-extend (Op_Sext): certificate sign-extend if-form to fast bv_sext.
--------------------------------------------------------------------------------

theorem mk_bv_ofInt {w : Nat} (y : Int) : mk_bv w y = bvenc (BitVec.ofInt w y) := by
  unfold bvenc
  apply mk_bv_eq_of_emod
  rw [show (2:Int) ^ w = ((2 ^ w : Nat) : Int) from by simp]
  have hnn : 0 ≤ y % ((2 ^ w : Nat) : Int) := Int.emod_nonneg y (by positivity)
  rw [BitVec.toNat_ofInt, Int.ofNat_eq_natCast, Int.toNat_of_nonneg hnn,
      Int.emod_emod_of_dvd _ (dvd_refl _)]

theorem sext_bridge {wa wam w : Nat} (a : BitVec wa) (amt : BitVec wam) (hamt : amt.toNat = wa) :
    eval_op LGraphOp.Op_Sext w [bvenc a, bvenc amt] = bvenc (bv_sext a : BitVec w) := by
  have hcert : eval_op LGraphOp.Op_Sext w [bvenc a, bvenc amt] = mk_bv w (bv_sint (bvenc a)) := by
    simp only [eval_op, bv_uint_bvenc]
    rw [show (Int.ofNat amt.toNat).toNat = wa from by simp [hamt]]
    have hu : Int.ofNat a.toNat % (2:Int) ^ wa = Int.ofNat a.toNat := by
      apply Int.emod_eq_of_lt (Int.natCast_nonneg _)
      rw [show (2:Int) ^ wa = ((2 ^ wa : Nat) : Int) from by simp, Nat.cast_lt]
      exact a.isLt
    rw [hu]
    unfold bv_sint
    rw [show (bvenc a).width = wa from rfl, bv_uint_bvenc]
    simp only [apply_ite (mk_bv w)]
  rw [hcert, bv_sint_bvenc]
  unfold bv_sext
  exact mk_bv_ofInt a.toInt

--------------------------------------------------------------------------------
-- MuxN (n-way select): reusable `muxn_fast` combinator + general bridge.
-- The emitter targets `muxn_fast (BitVec.toNat sel) [option..]` for n-way Mux
-- (options pre-resized to output width), making this bridge design-independent.
--------------------------------------------------------------------------------

/-- Fast-model n-way select combinator: the emitter targets this for n-way Mux
(options pre-resized to the output width `w`), instead of a raw nested `if`. -/
def muxn_fast {w : Nat} (sel : Nat) : List (BitVec w) → BitVec w
  | [] => 0#w
  | a :: l => if sel = 0 then a else muxn_fast (sel - 1) l

/-- The certificate's `Op_MuxN` list-index (over encoded, width-`w` options)
equals the encoded `muxn_fast`, for every selector value. -/
theorem muxn_index {w : Nat} (opts : List (BitVec w)) : ∀ s : Nat,
    (if s < (opts.map bvenc).length
       then bv_resize w (((opts.map bvenc)[s]?).getD (mk_bv w 0)) else mk_bv w 0)
      = bvenc (muxn_fast s opts) := by
  induction opts with
  | nil => intro s; simp [muxn_fast, bvenc]
  | cons a l ih =>
    intro s
    cases s with
    | zero =>
      simp only [muxn_fast, List.map_cons, List.length_cons, Nat.zero_lt_succ,
        List.getElem?_cons_zero, Option.getD_some, if_true]
      rw [bv_resize_bvenc, bv_zext_id]
    | succ k =>
      simp only [muxn_fast, Nat.succ_ne_zero, if_false, Nat.succ_sub_one, List.map_cons,
        List.length_cons, Nat.succ_lt_succ_iff, List.getElem?_cons_succ]
      exact ih k

/-- MuxN operator bridge: certificate `eval_op Op_MuxN` matches encoded `muxn_fast`. -/
theorem muxn_bridge {w sw : Nat} (sel : BitVec sw) (opts : List (BitVec w)) :
    eval_op LGraphOp.Op_MuxN w (bvenc sel :: opts.map bvenc)
      = bvenc (muxn_fast sel.toNat opts) := by
  have hidx : (bv_uint (bvenc sel)).toNat = sel.toNat := by rw [bv_uint_bvenc]; simp
  show (let idx := (bv_uint (bvenc sel)).toNat;
        if idx < (opts.map bvenc).length
          then bv_resize w (((opts.map bvenc)[idx]?).getD (mk_bv w 0)) else mk_bv w 0)
       = bvenc (muxn_fast sel.toNat opts)
  simp only []
  rw [hidx]
  exact muxn_index opts sel.toNat

--------------------------------------------------------------------------------
-- n-ary fold variants (And/Or/Xor/Ror/Sum).  The emitter targets the `*_fast`
-- combinators for n-way gates (operands pre-resized to output width), so one
-- general induction lemma per family covers every arity.
--------------------------------------------------------------------------------

-- Single-op bitwise bridge over equal width w (from bv_bitwise_eq + bv_bit_bvenc).
theorem hfg_and {w : Nat} (x y : BitVec w) :
    bv_bitwise w (fun a b => a && b) (bvenc x) (bvenc y) = bvenc (x &&& y) := by
  apply bv_bitwise_eq; intro i hi; rw [bv_bit_bvenc, bv_bit_bvenc, BitVec.getLsbD_and]
theorem hfg_or {w : Nat} (x y : BitVec w) :
    bv_bitwise w (fun a b => a || b) (bvenc x) (bvenc y) = bvenc (x ||| y) := by
  apply bv_bitwise_eq; intro i hi; rw [bv_bit_bvenc, bv_bit_bvenc, BitVec.getLsbD_or]
theorem hfg_xor {w : Nat} (x y : BitVec w) :
    bv_bitwise w (fun a b => xor a b) (bvenc x) (bvenc y) = bvenc (x ^^^ y) := by
  apply bv_bitwise_eq; intro i hi; rw [bv_bit_bvenc, bv_bit_bvenc, BitVec.getLsbD_xor]

-- General foldl bridge: a certificate bitwise fold matches the encoded BitVec fold.
theorem foldl_bitwise_bvenc {w : Nat} (f : Bool → Bool → Bool) (g : BitVec w → BitVec w → BitVec w)
    (hfg : ∀ x y : BitVec w, bv_bitwise w f (bvenc x) (bvenc y) = bvenc (g x y)) :
    ∀ (l : List (BitVec w)) (seed : BitVec w),
      (l.map bvenc).foldl (fun acc b => bv_bitwise w f acc b) (bvenc seed)
        = bvenc (l.foldl g seed) := by
  intro l
  induction l with
  | nil => intro seed; simp
  | cons b l ih =>
    intro seed
    simp only [List.map_cons, List.foldl_cons]
    rw [hfg seed b]
    exact ih (g seed b)

-- Fast combinators the emitter targets for n-way And/Or/Xor (operands pre-resized to w).
def andn_fast {w : Nat} (a : BitVec w) (rest : List (BitVec w)) : BitVec w := rest.foldl (· &&& ·) a
def orn_fast  {w : Nat} (l : List (BitVec w)) : BitVec w := l.foldl (· ||| ·) 0#w
def xorn_fast {w : Nat} (l : List (BitVec w)) : BitVec w := l.foldl (· ^^^ ·) 0#w

theorem andn_bridge {w : Nat} (a : BitVec w) (rest : List (BitVec w)) :
    eval_op LGraphOp.Op_And w (bvenc a :: rest.map bvenc) = bvenc (andn_fast a rest) := by
  show (rest.map bvenc).foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b)
        (bv_resize w (bvenc a)) = _
  rw [bv_resize_bvenc, bv_zext_id]
  exact foldl_bitwise_bvenc _ _ hfg_and rest a

theorem orn_bridge {w : Nat} (l : List (BitVec w)) :
    eval_op LGraphOp.Op_Or w (l.map bvenc) = bvenc (orn_fast l) := by
  show (l.map bvenc).foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0) = _
  rw [show (mk_bv w 0) = bvenc (0#w) from by simp [bvenc]]
  exact foldl_bitwise_bvenc _ _ hfg_or l 0#w

theorem xorn_bridge {w : Nat} (l : List (BitVec w)) :
    eval_op LGraphOp.Op_Xor w (l.map bvenc) = bvenc (xorn_fast l) := by
  show (l.map bvenc).foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0) = _
  rw [show (mk_bv w 0) = bvenc (0#w) from by simp [bvenc]]
  exact foldl_bitwise_bvenc _ _ hfg_xor l 0#w

-- Reduction-OR (Op_Ror): fast combinator over the (width-w) input list.
def rorn_fast {w : Nat} (l : List (BitVec w)) : BitVec 1 := bool_to_bv1 (l.any bitvec_nonzero)

theorem rorn_bridge {w : Nat} (l : List (BitVec w)) :
    eval_op LGraphOp.Op_Ror 1 (l.map bvenc) = bvenc (rorn_fast l) := by
  show mk_bv 1 (if (l.map bvenc).any bv_nonzero then 1 else 0) = bvenc (rorn_fast l)
  unfold rorn_fast
  rw [bvenc_bool]
  have hany : (l.map bvenc).any bv_nonzero = l.any bitvec_nonzero := by
    rw [List.any_map]; congr 1; funext x
    show bv_nonzero (bvenc x) = bitvec_nonzero x
    exact bv_nonzero_bvenc x
  rw [hany]

--------------------------------------------------------------------------------
-- n-ary Sum (adds/subs groups): fold-add + subtraction, all modulo 2^w.
--------------------------------------------------------------------------------

theorem mk_bv_add {w : Nat} (x y : BitVec w) :
    mk_bv w (Int.ofNat (x + y).toNat) = mk_bv w (Int.ofNat x.toNat + Int.ofNat y.toNat) := by
  apply mk_bv_eq_of_emod
  rw [BitVec.toNat_add, Int.ofNat_eq_natCast, Int.natCast_emod, Nat.cast_add,
      show ((2 ^ w : Nat) : Int) = (2 : Int) ^ w from by simp, Int.emod_emod_of_dvd _ (dvd_refl _)]
  simp [Int.ofNat_eq_natCast]

theorem mk_bv_sub_emod {w : Nat} (x y : BitVec w) :
    Int.ofNat (x - y).toNat % (2 : Int) ^ w
      = (Int.ofNat x.toNat - Int.ofNat y.toNat) % (2 : Int) ^ w := by
  have hyle : y.toNat ≤ 2 ^ w := Nat.le_of_lt y.isLt
  rw [BitVec.toNat_sub, Int.ofNat_eq_natCast, Int.natCast_emod, Nat.cast_add, Nat.cast_sub hyle,
      show ((2 ^ w : Nat) : Int) = (2 : Int) ^ w from by simp, Int.emod_emod_of_dvd _ (dvd_refl _),
      show ((2 : Int) ^ w - (y.toNat : Int) + (x.toNat : Int))
             = (2 : Int) ^ w + ((x.toNat : Int) - (y.toNat : Int)) from by ring,
      Int.add_emod_left]
  simp [Int.ofNat_eq_natCast]

def bvSum {w : Nat} (l : List (BitVec w)) : BitVec w := l.foldl (· + ·) 0#w
def isum {w : Nat} (l : List (BitVec w)) : Int := (l.map (fun x => Int.ofNat x.toNat)).sum
def sumn_fast {w : Nat} (adds subs : List (BitVec w)) : BitVec w := bvSum adds - bvSum subs

theorem foldl_add_bvenc {w : Nat} :
    ∀ (l : List (BitVec w)) (acc : BitVec w),
      bvenc (l.foldl (· + ·) acc) = mk_bv w (Int.ofNat acc.toNat + isum l) := by
  intro l
  induction l with
  | nil => intro acc; simp [isum, bvenc]
  | cons b l ih =>
    intro acc
    simp only [List.foldl_cons]
    rw [ih (acc + b)]
    apply mk_bv_eq_of_emod
    have hadd : Int.ofNat (acc + b).toNat % (2:Int)^w
                  = (Int.ofNat acc.toNat + Int.ofNat b.toNat) % (2:Int)^w :=
      mk_bv_emod_eq (mk_bv_add acc b)
    simp only [isum, List.map_cons, List.sum_cons]
    rw [Int.add_emod, hadd, ← Int.add_emod, add_assoc]

theorem bvenc_bvSum {w : Nat} (l : List (BitVec w)) : bvenc (bvSum l) = mk_bv w (isum l) := by
  unfold bvSum
  rw [foldl_add_bvenc l 0#w]
  simp

theorem sumn_bridge {w : Nat} (adds subs : List (BitVec w)) :
    eval_op (LGraphOp.Op_Sum adds.length) w (adds.map bvenc ++ subs.map bvenc)
      = bvenc (sumn_fast adds subs) := by
  have hmap : ∀ (l : List (BitVec w)),
      (l.map bvenc).map bv_uint = l.map (fun x => Int.ofNat x.toNat) := by
    intro l; rw [List.map_map]; apply List.map_congr_left; intro x _; exact bv_uint_bvenc x
  have hcert : eval_op (LGraphOp.Op_Sum adds.length) w (adds.map bvenc ++ subs.map bvenc)
                 = mk_bv w (isum adds - isum subs) := by
    show mk_bv w ((((adds.map bvenc ++ subs.map bvenc).map bv_uint).take adds.length).sum
                    - (((adds.map bvenc ++ subs.map bvenc).map bv_uint).drop adds.length).sum) = _
    rw [List.map_append]
    have hlen : ((adds.map bvenc).map bv_uint).length = adds.length := by simp
    rw [List.take_left' hlen, List.drop_left' hlen, hmap adds, hmap subs]
    rfl
  rw [hcert]
  unfold sumn_fast
  apply mk_bv_eq_of_emod
  have ha := mk_bv_emod_eq (bvenc_bvSum adds)
  have hs := mk_bv_emod_eq (bvenc_bvSum subs)
  rw [mk_bv_sub_emod, Int.sub_emod, ← ha, ← hs, ← Int.sub_emod]


/-- `eval_op`-form GetMask bridge (uniform with the other op bridges), so the
generated per-node recurrence closes by a single `rw` regardless of operator. -/
theorem getmask_bridge' {aw mw b : Nat} (X : BitVec aw) (M : BitVec mw)
    (hb : (mask_indices M).length ≤ b) :
    eval_op LGraphOp.Op_GetMask b [bvenc X, bvenc M] = bvenc (sem_get_mask X M : BitVec b) :=
  getmask_bridge X M hb


/-- `max`-width EQ bridge: fixes the compare width to `max wa wb` so it is
determined by the operands (the emitter uses `max(pin widths)`), avoiding a free
implicit in the generated `rw`. -/
theorem eq_bridge_max {wa wb : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_EQ 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext b : BitVec (max wa wb)) = (bv_zext a : BitVec (max wa wb)))) :=
  eq_bridge a b (Nat.le_max_left wa wb) (Nat.le_max_right wa wb)

/-- `max`-width unsigned-less-than bridge. -/
theorem ult_bridge_max {wa wb : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_ULT 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext a : BitVec (max wa wb)) < (bv_zext b : BitVec (max wa wb)))) :=
  ult_bridge a b (Nat.le_max_left wa wb) (Nat.le_max_right wa wb)

/-- `max`-width unsigned-greater-than bridge. -/
theorem ugt_bridge_max {wa wb : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_UGT 1 [bvenc a, bvenc b]
      = bvenc (bool_to_bv1 ((bv_zext a : BitVec (max wa wb)) > (bv_zext b : BitVec (max wa wb)))) :=
  ugt_bridge a b (Nat.le_max_left wa wb) (Nat.le_max_right wa wb)

/-- Fixed-arity (2-option) MuxN bridge matching the fast if-chain directly, with
mixed-width options (each zero-extended to the output width).  DINO's MuxN nodes
are all arity 3 (selector + 2 data). -/
theorem muxn3_bridge {sw w0 w1 w : Nat} (sel : BitVec sw) (o0 : BitVec w0) (o1 : BitVec w1) :
    eval_op LGraphOp.Op_MuxN w [bvenc sel, bvenc o0, bvenc o1]
      = bvenc (if sel.toNat = 0 then (bv_zext o0 : BitVec w)
               else if sel.toNat = 1 then (bv_zext o1 : BitVec w) else 0#w) := by
  have hs : (bv_uint (bvenc sel)).toNat = sel.toNat := by rw [bv_uint_bvenc]; simp
  show (if (bv_uint (bvenc sel)).toNat < 2
          then bv_resize w (([bvenc o0, bvenc o1][(bv_uint (bvenc sel)).toNat]?).getD (mk_bv w 0))
          else mk_bv w 0) = _
  rw [hs]
  by_cases h0 : sel.toNat = 0
  · simp only [h0, if_pos, Nat.zero_lt_succ, List.getElem?_cons_zero, Option.getD_some]
    rw [bv_resize_bvenc]
  · by_cases h1 : sel.toNat = 1
    · simp only [h1, List.getElem?_cons_succ, List.getElem?_cons_zero, Option.getD_some, if_neg h0]
      rw [bv_resize_bvenc]
      norm_num
    · have h2 : ¬ sel.toNat < 2 := by omega
      simp only [if_neg h2, if_neg h0, if_neg h1]
      simp [bvenc]

--------------------------------------------------------------------------------
-- n-ary Or over the certificate's width-erased BV list (any arity, mixed widths).
-- Emitter: rw [orn_bv_bridge] then unfold the fold + bv_to_bitvec_bvenc_zext + zero_or.
--------------------------------------------------------------------------------

theorem bv_to_bitvec_getLsbD {w : Nat} (B : BV) (i : Nat) (hi : i < w) :
    (bv_to_bitvec w B).getLsbD i = bv_bit B i := by
  have hnn : 0 ≤ bv_uint B := by unfold bv_uint; exact Int.emod_nonneg _ (by positivity)
  unfold bv_to_bitvec bv_bit
  rw [BitVec.getLsbD, BitVec.toNat_ofInt]
  have hcast : (bv_uint B % ((2 ^ w : Nat) : Int)).toNat = (bv_uint B).toNat % 2 ^ w := by
    conv_lhs => rw [← Int.toNat_of_nonneg hnn]
    rw [← Int.natCast_emod, Int.toNat_natCast]
  rw [hcast, Nat.testBit_mod_two_pow]
  simp only [hi, decide_true, Bool.true_and]
  rw [testBit_div_mod, Nat.shiftRight_eq_div_pow]

theorem bv_bitwise_or_step {w : Nat} (A : BitVec w) (B : BV) :
    bv_bitwise w (fun x y => x || y) (bvenc A) B = bvenc (A ||| bv_to_bitvec w B) := by
  apply bv_bitwise_eq
  intro i hi
  rw [bv_bit_bvenc, BitVec.getLsbD_or, bv_to_bitvec_getLsbD B i hi]

theorem orn_bv_foldl {w : Nat} : ∀ (bvs : List BV) (acc : BitVec w),
    bvs.foldl (fun a b => bv_bitwise w (fun x y => x || y) a b) (bvenc acc)
      = bvenc (bvs.foldl (fun a b => a ||| bv_to_bitvec w b) acc) := by
  intro bvs
  induction bvs with
  | nil => intro acc; simp
  | cons b bs ih =>
    intro acc
    simp only [List.foldl_cons]
    rw [bv_bitwise_or_step]
    exact ih (acc ||| bv_to_bitvec w b)

theorem orn_bv_bridge {w : Nat} (bvs : List BV) :
    eval_op LGraphOp.Op_Or w bvs
      = bvenc (bvs.foldl (fun a b => a ||| bv_to_bitvec w b) 0#w) := by
  show bvs.foldl (fun a b => bv_bitwise w (fun x y => x || y) a b) (mk_bv w 0) = _
  rw [show (mk_bv w 0) = bvenc (0#w) from by simp [bvenc]]
  exact orn_bv_foldl bvs 0#w

theorem bv_to_bitvec_bvenc_zext {wv w : Nat} (v : BitVec wv) :
    bv_to_bitvec w (bvenc v) = (bv_zext v : BitVec w) := by
  unfold bv_to_bitvec bv_zext
  rw [bv_uint_bvenc]
  apply BitVec.eq_of_toNat_eq
  rw [BitVec.toNat_ofInt, BitVec.toNat_ofNat, Int.ofNat_eq_natCast, ← Int.natCast_emod, Int.toNat_natCast]

/-- Binary add/subtract (Op_Sum 1 = one add, one subtract), mixed operand widths. -/
theorem sum1_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op (LGraphOp.Op_Sum 1) w [bvenc a, bvenc b]
      = bvenc ((bv_zext a : BitVec w) - (bv_zext b : BitVec w)) := by
  rw [show eval_op (LGraphOp.Op_Sum 1) w [bvenc a, bvenc b]
        = mk_bv w (Int.ofNat a.toNat - Int.ofNat b.toNat) from by simp [eval_op, bv_uint_bvenc]]
  unfold bvenc
  apply mk_bv_eq_of_emod
  rw [mk_bv_sub_emod]
  simp only [bv_zext, BitVec.toNat_ofNat, Int.ofNat_eq_natCast]
  push_cast
  rw [← Int.sub_emod]


/-- Zero-const spelling: the fast model emits `0#w` (lit_zero) while a cert source
leaf is `BitVec.ofInt w 0`.  Normalizes the two for compares / MuxN branches. -/
theorem ofInt_zero_eq (w : Nat) : (BitVec.ofInt w 0 : BitVec w) = 0#w := by
  apply BitVec.eq_of_toNat_eq; simp [BitVec.toNat_ofInt]

/-- For `w ≤ wa`, a BitVec's unsigned and signed values agree mod `2^w`. -/
theorem toNat_toInt_emod_le {wa w : Nat} (a : BitVec wa) (hle : w ≤ wa) :
    (a.toNat : Int) % (2:Int)^w = a.toInt % (2:Int)^w := by
  have hbmod : a.toInt ≡ (a.toNat : Int) [ZMOD ((2^wa : Nat) : Int)] := by
    unfold Int.ModEq; rw [BitVec.toInt_eq_toNat_bmod, Int.bmod_emod]
  have hdvd : (2:Int)^w ∣ ((2^wa : Nat) : Int) := by push_cast; exact pow_dvd_pow 2 hle
  exact (hbmod.of_dvd hdvd).symm

/-- Sext sign-truncate variant: sign position `amt = out width w ≤ operand width `wa`.
The result is the low `w` bits (the sign bit is at/above the output, so irrelevant),
matching `bv_sext a : BitVec w`.  Companion to `sext_bridge` (which needs `amt = wa`). -/
theorem sext_bridge_low {wa wam w : Nat} (a : BitVec wa) (amt : BitVec wam)
    (hamt : amt.toNat = w) (hle : w ≤ wa) :
    eval_op LGraphOp.Op_Sext w [bvenc a, bvenc amt] = bvenc (bv_sext a : BitVec w) := by
  have hcert : eval_op LGraphOp.Op_Sext w [bvenc a, bvenc amt] = mk_bv w (Int.ofNat a.toNat) := by
    simp only [eval_op, bv_uint_bvenc]
    rw [show (Int.ofNat amt.toNat).toNat = w from by simp [hamt]]
    by_cases hw : w = 0
    · subst hw; apply mk_bv_eq_of_emod; simp
    · rw [if_neg hw]
      have hb : mk_bv w (Int.ofNat a.toNat % 2^w - 2^w) = mk_bv w (Int.ofNat a.toNat % 2^w) := by
        apply mk_bv_eq_of_emod; rw [Int.sub_emod, Int.emod_emod_of_dvd _ (dvd_refl _)]; simp
      rw [show (if Int.ofNat a.toNat % 2^w < (2:Int)^(w-1) then mk_bv w (Int.ofNat a.toNat % 2^w)
              else mk_bv w (Int.ofNat a.toNat % 2^w - 2^w))
            = mk_bv w (Int.ofNat a.toNat % 2^w) from by rw [hb]; exact ite_self _]
      apply mk_bv_eq_of_emod; rw [Int.emod_emod_of_dvd _ (dvd_refl _)]
  rw [hcert]; unfold bvenc bv_sext; apply mk_bv_eq_of_emod
  rw [BitVec.toNat_ofInt]; simp only [Int.ofNat_eq_natCast]; push_cast
  rw [Int.toNat_of_nonneg (Int.emod_nonneg _ (by positivity)), Int.emod_emod_of_dvd _ (dvd_refl _)]
  exact toNat_toInt_emod_le a hle

end OpBridge
