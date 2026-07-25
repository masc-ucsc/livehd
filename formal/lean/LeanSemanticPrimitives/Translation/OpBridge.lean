/-
  Per-operator bridge lemmas for the pass.lean fast-view bridge (step 5).

  Each generated design proves `<Top>_comb = <Top>_comb_cert` by instantiating
  `GraphRefine.evalGraph_of_localAgree` with φ = its encoded fast-model node
  values, and discharging the per-node recurrence `φ n = eval_op op w (deps.map φ)`
  with the lemmas here — one per LGraph operator — relating the width-erased
  certificate evaluator `eval_op` (LGraphModel.lean) to the native `BitVec`
  fast-model operator (SemanticPrimitives.lean), under the encoding `bvenc`.

  This package is Mathlib-free, so proofs use core tactics only.  NOTE: `omega`
  cannot reduce `Int` `emod` by a *symbolic* modulus (`2^w`), so bounded mods are
  reduced explicitly with `Int.emod_eq_of_lt` after unifying the `2^w` cast atom.
-/

import LeanSemanticPrimitives.SemanticPrimitives
import LeanSemanticPrimitives.Translation.LGraphModel

namespace OpBridge

/-- Encode a fixed-width `BitVec` as a runtime-width certificate `BV`.  Matches
the `mk_bv w (Int.ofNat (BitVec.toNat _))` form the emitter uses in `sourceEnv`. -/
def bvenc {w : Nat} (x : BitVec w) : BV := mk_bv w (Int.ofNat x.toNat)

/-- Decoding a freshly-encoded `BitVec` returns it unchanged (round trip). -/
theorem bv_to_bitvec_bvenc {w : Nat} (x : BitVec w) : bv_to_bitvec w (bvenc x) = x := by
  have hlt  : x.toNat < 2 ^ w := x.isLt
  have hA   : (2 : Int) ^ w = ((2 ^ w : Nat) : Int) := by simp [Int.natCast_pow]
  have hlt' : Int.ofNat x.toNat < ((2 ^ w : Nat) : Int) := Int.ofNat_lt.mpr hlt
  have hnn  : (0 : Int) ≤ Int.ofNat x.toNat := Int.natCast_nonneg _
  apply BitVec.eq_of_toNat_eq
  unfold bv_to_bitvec bvenc mk_bv bv_uint
  simp only [BitVec.toNat_ofInt]
  rw [hA]
  rw [Int.emod_eq_of_lt hnn hlt', Int.emod_eq_of_lt hnn hlt', Int.emod_eq_of_lt hnn hlt']
  rfl

/-- Zero-extension to the same width is the identity. -/
theorem bv_zext_id {w : Nat} (x : BitVec w) : (bv_zext x : BitVec w) = x := by
  apply BitVec.eq_of_toNat_eq
  unfold bv_zext
  simp

end OpBridge
