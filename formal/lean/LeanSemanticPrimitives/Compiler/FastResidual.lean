/-
# Proved fast paths for the residual operators that compute `2 ^ k` inline

`ResidualSemantics` imports `Translation/FastBV`, so every residual operator that is written
with the `BV` primitives already compiles to their fast forms. `rshlV` and `rnotV` are the
exceptions: `rshlV` spells the shifted copy `bv_uint a * 2 ^ k` inline, and `rnotV` its
`bits_to_int` loop. A `@[csimp]` lemma can only replace a named function, so each gets a
fast form here, with a theorem that it is equal. TV's generated step calls them by name,
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

end FastBV
