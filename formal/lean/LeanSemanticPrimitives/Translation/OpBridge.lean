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

end OpBridge
