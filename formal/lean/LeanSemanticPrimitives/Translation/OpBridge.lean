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

end OpBridge
