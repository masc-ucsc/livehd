/-
# Shift and sign-extension boundary regressions

Permanent verification, not a scratch probe. These pin the guards added after a
canonical CORE-ET sweep failed three designs with

    INTERNAL PANIC: Nat.pow exponent is too big

The panic was NOT about wide data paths -- 1024/1025-bit widths evaluate fine,
and `mk_bv 2048 1` is a 617-digit modulus GMP handles without complaint. It came
from raising 2 to a shift AMOUNT, which is a VALUE: the three failing designs
carry 33-bit amount operands, so the exponent reached ~8.6e9 while the
mathematical answer is immediate saturation.

Three bounds, and they are NOT the same bound:

  SHL   saturates at the RESULT width w.   `shl_bridge` shifts `(zext a : BitVec w)`.
  SRA   saturates at the SOURCE width a.width.  `sem_sra` is `x.sshiftRight`, which
        shifts at its own width and only then widens -- `sra_bridge` truncates with
        `bv_zext` for `w <= wa`, `sra_bridge_sext` sign-extends otherwise. Bounding
        SRA at `w` would disagree with both whenever `w > wa`.
  Sext  diverts only for n STRICTLY greater than a.width, because at `n = a.width`
        the sign bit can still be set.

Run: `lake env lean probes/d3_shift_boundary_test.lean` (silent = all pass).
-/
import LeanSemanticPrimitives.Compiler.ResidualSemantics
open Compiler Compiler.Residual

private def B (w : Nat) (v : Int) : BV := mk_bv w v

--------------------------------------------------------------------------------
-- SHL: k = w-1, w, w+1, huge.  Saturates at the RESULT width.
--------------------------------------------------------------------------------
#guard bv_uint (eval_op LGraphOp.Op_SHL 8 [B 8 1, B 32 7]) == 128   -- k = w-1
#guard bv_uint (eval_op LGraphOp.Op_SHL 8 [B 8 1, B 32 8]) == 0     -- k = w
#guard bv_uint (eval_op LGraphOp.Op_SHL 8 [B 8 1, B 32 9]) == 0     -- k = w+1
#guard bv_uint (eval_op LGraphOp.Op_SHL 8 [B 8 1, B 64 4294967296]) == 0  -- huge
#guard bv_uint (rshlV 8 [B 8 1, B 32 7]) == 128
#guard bv_uint (rshlV 8 [B 8 1, B 32 8]) == 0
#guard bv_uint (rshlV 8 [B 8 1, B 64 4294967296]) == 0
/- residual and evaluator agree at every SHL boundary -/
#guard (List.range 12).all fun k =>
  bv_uint (rshlV 8 [B 8 5, B 32 (Int.ofNat k)])
    == bv_uint (eval_op LGraphOp.Op_SHL 8 [B 8 5, B 32 (Int.ofNat k)])

--------------------------------------------------------------------------------
-- SRA: k = wa-1, wa, wa+1, huge, positive and negative, w < wa / w = wa / w > wa.
-- Saturates at the SOURCE width, so the sign fill happens at wa and only then
-- widens. Bounding at w instead would be visible in the w > wa rows.
--------------------------------------------------------------------------------
/- positive source, a.width = 8, value 64 -/
#guard bv_uint (bv_sra 8 (B 8 64) (B 32 7)) == 0      -- k = wa-1
#guard bv_uint (bv_sra 8 (B 8 64) (B 32 8)) == 0      -- k = wa
#guard bv_uint (bv_sra 8 (B 8 64) (B 32 9)) == 0      -- k = wa+1
#guard bv_uint (bv_sra 8 (B 8 64) (B 64 4294967296)) == 0  -- huge

/- negative source (-1 at width 8 = 255): sign fill is all ones at EVERY width -/
#guard bv_uint (bv_sra 8  (B 8 (-1)) (B 32 7)) == 255
#guard bv_uint (bv_sra 8  (B 8 (-1)) (B 32 8)) == 255   -- k = wa
#guard bv_uint (bv_sra 8  (B 8 (-1)) (B 32 9)) == 255   -- k = wa+1
#guard bv_uint (bv_sra 8  (B 8 (-1)) (B 64 4294967296)) == 255  -- huge
#guard bv_uint (bv_sra 4  (B 8 (-1)) (B 64 4294967296)) == 15    -- w < wa
#guard bv_uint (bv_sra 16 (B 8 (-1)) (B 64 4294967296)) == 65535 -- w > wa
/- negative, modest value -2 -/
#guard bv_uint (bv_sra 16 (B 8 (-2)) (B 64 4294967296)) == 65535
/- positive source with w > wa: zero fill, not sign fill -/
#guard bv_uint (bv_sra 16 (B 8 64) (B 64 4294967296)) == 0
/- zero-width source: degenerate, bv_sint = 0, so zero fill -/
#guard bv_uint (bv_sra 8 (B 0 0) (B 64 4294967296)) == 0
/- residual rsraV agrees with the evaluator across the boundary -/
#guard (List.range 12).all fun k =>
  bv_uint (rsraV 8 (B 8 (-3)) (B 32 (Int.ofNat k)))
    == bv_uint (eval_op LGraphOp.Op_SRA 8 [B 8 (-3), B 32 (Int.ofNat k)])

--------------------------------------------------------------------------------
-- Sext: n = 0, n < a.width, n = a.width, n > a.width, huge.
--
-- THE REGRESSION THAT MOTIVATED `>` RATHER THAN `>=`:
--   a = mk_bv 4 (-1) (value 15), w = 8
--     n = 4 (= a.width)  -> SIGN extension -> 255
--     n = 5 (> a.width)  -> ZERO extension -> 15
-- Under `n >= a.width` the first would wrongly give 15.
--------------------------------------------------------------------------------
#guard bv_uint (eval_op LGraphOp.Op_Sext 8 [B 4 (-1), B 32 4]) == 255  -- n = a.width
#guard bv_uint (eval_op LGraphOp.Op_Sext 8 [B 4 (-1), B 32 5]) == 15   -- n = a.width+1
#guard bv_uint (rsextV 8 (B 4 (-1)) (B 32 4)) == 255
#guard bv_uint (rsextV 8 (B 4 (-1)) (B 32 5)) == 15

#guard bv_uint (eval_op LGraphOp.Op_Sext 8 [B 4 (-1), B 32 0]) == 0    -- n = 0
/- n < a.width: u = 15 % 2^3 = 7, and 7 >= 2^(n-1) = 4, so this SIGN-extends -/
#guard bv_uint (eval_op LGraphOp.Op_Sext 8 [B 4 (-1), B 32 3]) == 255  -- n < a.width, sign bit set
#guard bv_uint (eval_op LGraphOp.Op_Sext 8 [B 4 3, B 32 3]) == 3       -- n < a.width, sign bit clear (011)
#guard bv_uint (eval_op LGraphOp.Op_Sext 8 [B 4 (-1), B 64 4294967296]) == 15 -- huge
#guard bv_uint (rsextV 8 (B 4 (-1)) (B 64 4294967296)) == 15
/- residual and evaluator agree across the whole Sext boundary -/
#guard (List.range 10).all fun n =>
  bv_uint (rsextV 8 (B 4 (-1)) (B 32 (Int.ofNat n)))
    == bv_uint (eval_op LGraphOp.Op_Sext 8 [B 4 (-1), B 32 (Int.ofNat n)])

#eval IO.println "BOUNDARY-MATRIX OK"
