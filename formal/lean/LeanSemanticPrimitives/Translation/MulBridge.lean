/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.Translation.OpBridge

/- The two-operand multiply bridge. The legacy fast model writes `Op_Mult` with
   two operands as `((bv_zext a : BitVec w) * (bv_zext b : BitVec w))`, the same
   shape as a two-operand Sum; this is `sum2_bridge` with `*` for `+`. eval_op,
   the certificate vocabulary and the emitted fast-model expressions are
   unchanged. -/
namespace OpBridge

theorem mul2_bridge {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    eval_op LGraphOp.Op_Mult w [bvenc a, bvenc b]
      = bvenc ((bv_zext a : BitVec w) * (bv_zext b : BitVec w)) := by
  rw [show eval_op LGraphOp.Op_Mult w [bvenc a, bvenc b]
        = mk_bv w (Int.ofNat a.toNat * Int.ofNat b.toNat) from by simp [eval_op, bv_uint_bvenc]]
  unfold bvenc
  apply mk_bv_eq_of_emod
  rw [BitVec.toNat_mul]
  simp only [bv_zext, BitVec.toNat_ofNat, Int.ofNat_eq_natCast]
  push_cast
  rw [Int.emod_emod_of_dvd _ (dvd_refl _), ← Int.mul_emod]

end OpBridge
