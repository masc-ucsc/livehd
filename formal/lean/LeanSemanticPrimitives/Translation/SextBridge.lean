/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.Translation.OpBridge

/- The sign-extend bridge at ANY sign position (decision #21). The legacy fast
   model writes `Op_Sext` as `bv_sext a` when the sign position is the operand's
   width (or the node's, if smaller); `sext_bridge` and `sext_bridge_low` in
   OpBridge prove those two. At any other position -- an interior sign bit
   (sign-extend the low k bits of a wider operand), a position above the operand,
   0, or an amount that is not a constant -- it writes
   `bv_sext (BitVec.ofNat amt a)`, and this lemma proves that form with no side
   condition. It is a separate file, as MulBridge is, so the pinned OpBridge.lean
   stays byte-identical; the emitter imports it only when a design needs it.
   eval_op, the certificate vocabulary and the emitted fast-model expressions are
   unchanged. -/
namespace OpBridge

/-- Sext at any sign position `amt`: the low `amt` bits of `a`, sign-extended to
`w`. At `amt = 0` both sides are 0; at `amt > wa` the sign bit lies above `a`, so
both sides zero-extend; otherwise both read bit `amt - 1` of `a` as the sign. -/
theorem sext_bridge_any {wa wam w : Nat} (a : BitVec wa) (amt : BitVec wam) :
    eval_op LGraphOp.Op_Sext w [bvenc a, bvenc amt]
      = bvenc (bv_sext (BitVec.ofNat amt.toNat a.toNat) : BitVec w) := by
  unfold bv_sext
  rw [← mk_bv_ofInt]
  simp only [eval_op, bv_uint_bvenc]
  rw [show (Int.ofNat amt.toNat).toNat = amt.toNat from by simp,
      show (bvenc a).width = wa from rfl]
  generalize amt.toNat = n
  have hlt := a.isLt
  rw [BitVec.toInt_eq_toNat_cond, BitVec.toNat_ofNat]
  by_cases h0 : n = 0
  · subst h0; simp [Nat.mod_one]
  by_cases hgt : n > wa
  · have hmod : a.toNat % 2 ^ n = a.toNat :=
      Nat.mod_eq_of_lt (Nat.lt_of_lt_of_le hlt (Nat.pow_le_pow_right (by norm_num) (by omega)))
    have h2 : 2 * a.toNat < 2 ^ n := by
      have : 2 ^ wa * 2 ≤ 2 ^ n := by
        rw [← Nat.pow_succ]; exact Nat.pow_le_pow_right (by norm_num) (by omega)
      omega
    simp only [h0, hgt, hmod, h2, ↓reduceIte]; rfl
  · have hn : 2 ^ n = 2 * 2 ^ (n - 1) := by
      rw [← Nat.pow_succ']; congr 1; omega
    have hpow : (2:Int) ^ n = ((2 ^ n : Nat) : Int) := by simp
    have hpow1 : (2:Int) ^ (n - 1) = ((2 ^ (n - 1) : Nat) : Int) := by simp
    simp only [h0, hgt, ↓reduceIte, hpow, hpow1, Int.ofNat_eq_natCast, ← Int.natCast_mod]
    by_cases hc : 2 * (a.toNat % 2 ^ n) < 2 ^ n
    · have hc' : ((a.toNat % 2 ^ n : Nat) : Int) < ((2 ^ (n - 1) : Nat) : Int) := by omega
      simp only [hc, hc', ↓reduceIte]
    · have hc' : ¬ ((a.toNat % 2 ^ n : Nat) : Int) < ((2 ^ (n - 1) : Nat) : Int) := by omega
      simp only [hc, hc', ↓reduceIte]

/-- An interior sign position is a real third case: the low 3 bits of `0x0C` are
`100`, so sign-extending them gives `0xFC`, while extending from the operand's
own width keeps `0x0C`. -/
example : (bv_sext (BitVec.ofNat (3#4).toNat (0x0C#8).toNat) : BitVec 8) = 0xFC#8
        ∧ (bv_sext (0x0C#8) : BitVec 8) = 0x0C#8 := by native_decide

end OpBridge
