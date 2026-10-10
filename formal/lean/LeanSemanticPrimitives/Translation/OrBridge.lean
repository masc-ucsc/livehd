/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.Translation.OpBridge

namespace OpBridge

/-- Start a nonempty Or fold at its first operand. This removes the zero
accumulator before the emitter substitutes large dependency expressions.
Keeping `BitVec.zero_or` in the generated simplifier can unfold those expressions
while trying to match zero, making an otherwise local node proof expensive.
The certificate operation and the fast expression remain unchanged. -/
theorem orn_nonempty_bv_bridge {w : Nat} (a : BV) (rest : List BV) :
    eval_op LGraphOp.Op_Or w (a :: rest) =
      bvenc (rest.foldl (fun acc b => acc ||| bv_to_bitvec w b) (bv_to_bitvec w a)) := by
  rw [orn_bv_bridge]
  simp only [List.foldl_cons, BitVec.zero_or]

end OpBridge
