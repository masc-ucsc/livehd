/-
# Proved fast paths for the residual operators that compute `2 ^ k` inline

`ResidualSemantics` imports `Translation/FastBV`, so every residual operator that is written
with the `BV` primitives already compiles to their fast forms. `rshlV` and `rnotV` are the
exceptions: `rshlV` spells the shifted copy `bv_uint a * 2 ^ k` inline, and `rnotV` its
`bits_to_int` loop. A `@[csimp]` lemma can only replace a named function, so each gets a
fast form here, with a theorem that it is equal. `rsextV` is spelled differently from
`eval_op`'s `Op_Sext` (`sextOp`); it is proved equal to it here and takes its fast form. TV's generated step calls them by name,
and `ReifyGen` imports this module, so every TV module compiles with them.

Mathlib-free, like `FastBV`.
-/
import LeanSemanticPrimitives.Compiler.ResidualSemantics
import LeanSemanticPrimitives.Translation.FastBV

namespace FastBV

open Compiler.Residual

/-- `rshlV` with its fold term named: `bv_shl_step` is exactly that term, so the two are
equal by unfolding. Compiled here, the named term and the XOR take their fast forms. -/
def rshlVFast (w : Nat) : List BV → BV
  | []      => mk_bv w 0
  | a :: bs => bs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc (bv_shl_step w a b)) (mk_bv w 0)

@[csimp] theorem rshlV_eq_fast : @rshlV = @rshlVFast := by
  funext w l
  cases l <;> rfl

def rnotVFast (w : Nat) (a : BV) : BV := bvNotFast w a

@[csimp] theorem rnotV_eq_fast : @rnotV = @rnotVFast := by
  funext w a
  simp only [rnotV, rnotVFast, bvNotFast, bitwiseNatTab_eq]
  rw [bits_to_int_eq w _ (bitwiseNat w (fun x _ => !x) (uintNat a) 0) (bitwiseNat_lt _ _ _ _)
      (fun i hi => by rw [testBit_bitwiseNat]; simp [hi, bv_bit_eq]),
    mk_bv_natCast w _ (bitwiseNat_lt _ _ _ _)]

/-- vc's sign extension (`rsextV`, "truncate to `n` bits, read as signed") is `eval_op`'s
(`sextOp`). Mathlib-free copy of `CompileOp.rsextV_correct`, stated with `sextOp`. -/
theorem rsextV_eq_sextOp (w : Nat) (a amt : BV) : rsextV w a amt = sextOp w a amt := by
  unfold rsextV sextOp
  by_cases hn : (bv_uint amt).toNat = 0
  · simp only [hn, ite_true]
  · simp only [hn, ite_false]
    by_cases hg : (bv_uint amt).toNat > a.width
    · simp only [hg, ite_true]
    · simp only [hg, ite_false]
      have hu : bv_uint (mk_bv (bv_uint amt).toNat (bv_uint a))
          = bv_uint a % (2 ^ (bv_uint amt).toNat : Int) := by
        simp only [bv_uint, mk_bv]
        exact Int.emod_emod _ _
      have hw : (mk_bv (bv_uint amt).toNat (bv_uint a)).width = (bv_uint amt).toNat := rfl
      simp only [bv_sint, hu, hw, hn, ite_false]
      split <;> rfl

/-- `rsextV` takes `sextOp`'s fast form (at every width; the wide steps are in
`sextOpFast`). TV's generated step and vc's compiled step call `rsextV` by name. -/
@[csimp] theorem rsextV_eq_fast : @rsextV = @sextOpFast := by
  funext w a amt
  rw [rsextV_eq_sextOp, sextOp_eq_fast]

end FastBV
