/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.Translation.OpBridge

/- The `bv_sra` guard on EVERY `BV`.

`bv_sra` returns the all-sign-bits answer once the shift amount reaches the
source width, instead of computing `2 ^ k` for an amount that is a value rather
than a width. `OpBridge.bv_sra_guard_eq` proves that this equals the unguarded
formula `bv_sint x / 2 ^ k` on canonical (`bvenc`) operands, the form both legacy
bridges apply it to; `LGraphModel.lean` and `SHARED_SEMANTICS.md` recorded the
general case as open.

It needs no hypothesis: `bv_sint` reads its argument only through `bv_uint`,
which already reduces modulo `2 ^ width`, so every `BV` -- normalised or not --
has a signed value in `[-2^(w-1), 2^(w-1))`, and the guard's two answers are the
floor quotients there. A new file, so the pinned `OpBridge.lean` and
`LGraphModel.lean` hashes do not change. -/
namespace OpBridge

/-- The signed value of ANY `BV` of positive width is in `[-2^(w-1), 2^(w-1))`. -/
theorem bv_sint_bounds (x : BV) (h : 0 < x.width) :
    -(2 : Int) ^ (x.width - 1) ≤ bv_sint x ∧ bv_sint x < 2 ^ (x.width - 1) := by
  have hp : (2 : Int) ^ x.width = 2 * 2 ^ (x.width - 1) := by
    conv_lhs => rw [show x.width = (x.width - 1) + 1 from by omega]
    rw [pow_succ]; ring
  have hpos : (0 : Int) < 2 ^ x.width := by positivity
  have h0 : 0 ≤ bv_uint x := Int.emod_nonneg _ (by omega)
  have h1 : bv_uint x < 2 ^ x.width := Int.emod_lt_of_pos _ hpos
  unfold bv_sint
  simp only [show x.width ≠ 0 from by omega, ite_false]
  split <;> constructor <;> omega

/-- The guarded `bv_sra` equals the unguarded formula on every `BV`. -/
theorem bv_sra_guard_eq_any (w : Nat) (x s : BV) :
    bv_sra w x s = mk_bv w (bv_sint x / (2 : Int) ^ (bv_uint s).toNat) := by
  unfold bv_sra
  simp only []
  split
  · next hge =>
    rcases Nat.eq_zero_or_pos x.width with h0 | hpos
    · rw [ite_eq_left h0]; simp [bv_sint, h0]
    · rw [ite_eq_right (by omega)]
      obtain ⟨hlo, hhi⟩ := bv_sint_bounds x hpos
      have hpow : (2 : Int) ^ (x.width - 1) ≤ 2 ^ (bv_uint s).toNat :=
        pow_le_pow_right₀ (by norm_num) (by omega)
      rcases Int.lt_or_le (bv_sint x) 0 with hneg | hnn
      · rw [ite_eq_right (by omega)]
        congr 1
        have hpos2 : (0 : Int) < 2 ^ (bv_uint s).toNat := by positivity
        have hstep : (bv_sint x + 2 ^ (bv_uint s).toNat * 1) / 2 ^ (bv_uint s).toNat
                   = bv_sint x / 2 ^ (bv_uint s).toNat + 1 :=
          Int.add_mul_ediv_left _ _ (by omega)
        have hzero : (bv_sint x + 2 ^ (bv_uint s).toNat * 1) / 2 ^ (bv_uint s).toNat = 0 :=
          Int.ediv_eq_zero_of_lt (by omega) (by omega)
        omega
      · rw [ite_eq_left hnn, Int.ediv_eq_zero_of_lt hnn (by omega)]
  · rfl

/-- The canonical case, `bv_sra_guard_eq`, is the special case `x = bvenc a`. -/
theorem bv_sra_guard_eq_of_any {wa wb w : Nat} (a : BitVec wa) (b : BitVec wb) :
    bv_sra w (bvenc a) (bvenc b) = mk_bv w (a.toInt / (2 : Int) ^ b.toNat) := by
  rw [bv_sra_guard_eq_any, bv_sint_bvenc]
  have hk : (bv_uint (bvenc b)).toNat = b.toNat := by rw [bv_uint_bvenc]; simp
  rw [hk]

end OpBridge
