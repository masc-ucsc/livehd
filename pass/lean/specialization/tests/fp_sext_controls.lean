/-
# Finite boundary controls for the `Op_Sext` bridge repair

These are ADDITIONAL evidence, not a substitute for the theorem.  The universal
obligation is `Projection.evalOp_Sext`, proved for all `w`, `a` and `amount`;
what these pin is the handful of points where a plausible-looking change to the
guard would still typecheck and would still satisfy most examples.

`#guard` throughout, so the build is the test.

THE BOUNDARY THAT MATTERS.  Master's `Op_Sext` branches on `n > a.width`,
STRICTLY.  At `n = a.width` the sign bit can be set and sign extension still
applies; at `n > a.width` the value is already below `2^(n-1)` and the answer is
plain zero extension.  `a = mk_bv 4 (-1)` with `w = 8` separates them: 255 at
`n = 4`, 15 at `n = 5`.  An implementation that used `≥` would answer 15 at
BOTH, so `sext_boundary_detects_ge` below fails against it.
-/

import LeanSemanticPrimitives.Projection.OperatorBridge

open Projection

/-! ## The `≥`-detector. This is the control the repair exists to keep honest. -/

-- n = a.width, sign bit set: sign extension applies, so 0b1111 -> 255 at w = 8.
#guard (eval_op .Op_Sext 8 [mk_bv 4 (-1), mk_bv 8 4]).value = 255

-- n > a.width, same input: plain zero extension, 15.  Under `≥` the branch
-- above would also take this path and answer 15, so the pair separates them.
#guard (eval_op .Op_Sext 8 [mk_bv 4 (-1), mk_bv 8 5]).value = 15

/-! ## n = 0 -/
#guard (eval_op .Op_Sext 8 [mk_bv 4 (-1), mk_bv 8 0]).value = 0
#guard (eval_op .Op_Sext 0 [mk_bv 4 (-1), mk_bv 8 0]).value = 0

/-! ## n < a.width: truncate, then sign-extend from the new top bit -/
-- 0b1111 truncated to 2 bits is 0b11 = -1 signed, so 255 at w = 8.
#guard (eval_op .Op_Sext 8 [mk_bv 4 (-1), mk_bv 8 2]).value = 255
-- 0b0101 = 5 truncated to 2 bits is 0b01 = 1, positive.
#guard (eval_op .Op_Sext 8 [mk_bv 4 5, mk_bv 8 2]).value = 1
-- 0b0110 = 6 truncated to 2 bits is 0b10 = -2, so 254 at w = 8.
#guard (eval_op .Op_Sext 8 [mk_bv 4 6, mk_bv 8 2]).value = 254

/-! ## zero-width input and zero-width result -/
#guard (eval_op .Op_Sext 8 [mk_bv 0 0, mk_bv 8 4]).value = 0
#guard (eval_op .Op_Sext 0 [mk_bv 4 (-1), mk_bv 8 4]).value = 0
#guard (eval_op .Op_Sext 8 [mk_bv 0 7, mk_bv 8 3]).value = 0

/-! ## unreduced and negative `BV.value` fields -/
-- `bv_uint` reduces first: ⟨4, 19⟩ is 3, and 3 at n = 4 has its sign bit clear.
#guard (eval_op .Op_Sext 8 [{ width := 4, value := 19 }, mk_bv 8 4]).value = 3
-- ⟨4, -1⟩ is 15, sign bit set at n = 4.
#guard (eval_op .Op_Sext 8 [{ width := 4, value := -1 }, mk_bv 8 4]).value = 255
-- a negative amount: `bv_uint` of ⟨8, -1⟩ is 255, far above a.width, so zero
-- extension -- and the proof's wide branch is what covers it.
#guard (eval_op .Op_Sext 8 [mk_bv 4 (-1), { width := 8, value := -1 }]).value = 15

/-! ## signed and unsigned examples at the same width -/
#guard (eval_op .Op_Sext 16 [mk_bv 8 127, mk_bv 8 8]).value = 127
#guard (eval_op .Op_Sext 16 [mk_bv 8 128, mk_bv 8 8]).value = 65408
#guard (eval_op .Op_Sext 16 [mk_bv 8 255, mk_bv 8 8]).value = 65535

/-! ## the theorem itself, with exact-name axiom reports -/
#print axioms Projection.evalOp_Sext
#print axioms Projection.sext_wide
#print axioms Projection.evalOp_SHL_unchanged_by_overlay
#print axioms Projection.denoteOp_SHL_unchanged_by_overlay
#print axioms Projection.bv_shl_step_unfolds
#print axioms Projection.shl_step_eq
