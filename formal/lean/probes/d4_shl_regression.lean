/-
  D4 regression: `Op_SHL` with a RUNTIME shift operand.

  Three CORE-ET certificates (`minion_dcache_tensor_load` binding 1360,
  `vpu_ml` binding 1786, `vpu_tensorquant` binding 730) carry a 33-bit shift
  amount.  The old spelling of `Op_SHL` evaluated `(2 : Int) ^ k` BEFORE taking
  `% 2 ^ w`, so Lean's `Nat.pow` guard panicked with "exponent is too big" and
  the whole run died -- at widths 66, 258 and 258, with exponents 8589934576,
  8589934368 and 8589934496.

  The values below are the ONLY thing standing between a passing build and that
  panic coming back, so they are `#guard`s, not commented `#eval`s: a wrong
  answer -- or a panic -- fails this file, and the file is built by
  `pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/shl_regression.sh`.

  Every case goes through the VERIFIED runtime path, `compileDesign` followed by
  `denoteResidual`, except where a case names the semantics it is pinning
  (`denote_op` / `eval_op` / `rshlV`) on purpose.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign

open Compiler
open Compiler.Residual

def noState : RuntimeState := ⟨#[], #[]⟩

/-- `y = a << k`: `a` is `w` bits wide, the SHIFT OPERAND is `kw` bits wide and
is a primary input, i.e. not a literal the elaborator can see through. -/
def shlDesign (w kw : Nat) : DesignCert :=
  { sources  := #[.input 0 w, .input 1 kw]
    nodes    := #[{ op := .Op_SHL, width := w, deps := #[0, 1] }]
    outputs  := #[{ slot := 2, width := w }]
    flops := #[], memories := #[] }

/-- Compile, then run.  `none` means the certificate did not compile, which is a
different failure from a wrong value and must not be confused with one.

ADAPTED from the pin: `denoteResidual` now takes the CLOCK EDGE VECTOR first.
The pinned call predates edges and means every declared clock fires, so it is
given `allEdges` of the same certificate -- named at the call site rather than
defaulted underneath, the same adaptation the restored probes and drivers
make. This design has no flops, so the edges cannot change the answer; stating
them keeps the contract visible anyway. -/
def shlRun (w kw : Nat) (a k : Int) : Option Int :=
  match compileDesign (shlDesign w kw) with
  | .error _ => none
  | .ok R    =>
    ((denoteResidual R (allEdges (shlDesign w kw)) #[mk_bv w a, mk_bv kw k]
        noState).outputs[0]?).map bv_uint

--------------------------------------------------------------------------------
-- 1. The required cases: in range, exactly at the width, and a 33-bit exponent.
--------------------------------------------------------------------------------

-- w = 8, a = 1, k = 7: the last shift that survives.  128, not 0.
#guard shlRun 8 8 1 7 == some 128
-- w = 8, a = 1, k = 8: the first shift the modulus kills.
#guard shlRun 8 8 1 8 == some 0
-- w = 8, a = 1, k = 8589934576 (33 bits): 0, and -- the point -- no panic.
#guard shlRun 8 40 1 8589934576 == some 0

--------------------------------------------------------------------------------
-- 2. The three CORE-ET shapes that actually panicked, at their real widths and
--    their real exponents.
--------------------------------------------------------------------------------

#guard shlRun  66 40 1 8589934576 == some 0   -- minion_dcache_tensor_load:1360
#guard shlRun 258 40 1 8589934368 == some 0   -- vpu_ml:1786
#guard shlRun 258 40 1 8589934496 == some 0   -- vpu_tensorquant:730

--------------------------------------------------------------------------------
-- 3. Teeth: the in-range branch must still be EXACT at those widths, so the
--    repair cannot be "return 0 more often".
--------------------------------------------------------------------------------

-- 3 << 64 at width 66 is 55340232221128654848, which does NOT wrap.
#guard shlRun 66 40 3 64 == some 55340232221128654848
-- 3 << 65 at width 66 keeps only the low bit of the 3: 2^65.
#guard shlRun 66 40 3 65 == some 36893488147419103232
-- one past the width, at width 66.
#guard shlRun 66 40 3 66 == some 0
-- a shift that wraps a non-trivial operand at width 8: 0xB5 << 3 = 0xA8.
#guard shlRun 8 8 0xB5 3 == some 0xA8

--------------------------------------------------------------------------------
-- 4. Source and residual semantics agree on the same inputs.  `eval_op_correct`
--    and `rshlV_correct` prove this generically; these pin the executable
--    behaviour of all three spellings so a future edit to one is caught here.
--------------------------------------------------------------------------------

#guard bv_uint (denote_op .Op_SHL  8 [mk_bv  8 1, mk_bv  8 7])          == 128
#guard bv_uint (eval_op   .Op_SHL  8 [mk_bv  8 1, mk_bv  8 7])          == 128
#guard bv_uint (rshlV              8 [mk_bv  8 1, mk_bv  8 7])          == 128
#guard bv_uint (denote_op .Op_SHL 66 [mk_bv 66 1, mk_bv 40 8589934576]) == 0
#guard bv_uint (eval_op   .Op_SHL 66 [mk_bv 66 1, mk_bv 40 8589934576]) == 0
#guard bv_uint (rshlV             66 [mk_bv 66 1, mk_bv 40 8589934576]) == 0

--------------------------------------------------------------------------------
-- 5. The branch boundary itself.
--
-- RESTORED from pinned D4 f6ad1fe29f8f15aeda045115864fd81a34c33aef, with this
-- section ADAPTED.  The pin called a named helper `bv_shl_bounded` directly.
-- Master has no such definition: the bound lives INSIDE `eval_op`/`denote_op`
-- as `if (bv_uint b).toNat >= w then 0 else ...`, which is where the guard
-- belongs and where `sem_shl` characterises it.  Introducing a helper on
-- master so a probe could call it would be adding a definition for the test's
-- convenience.
--
-- The coverage is not lost: the boundary cases below go through the same
-- guard by the ordinary route -- at the width, one past it, and far past it --
-- and sections 1-4 already drive the three real CORE-ET exponents through it.
--------------------------------------------------------------------------------

-- At the width minus one: the shift still lands.
#guard bv_uint (eval_op   .Op_SHL 8 [mk_bv 8 1, mk_bv 8 7]) == 128
#guard bv_uint (denote_op .Op_SHL 8 [mk_bv 8 1, mk_bv 8 7]) == 128
-- AT the width: everything has shifted out.  This is the branch boundary, and
-- it is the case an unguarded `2 ^ k` gets wrong rather than merely slowly.
#guard bv_uint (eval_op   .Op_SHL 8 [mk_bv 8 1, mk_bv 8 8]) == 0
#guard bv_uint (denote_op .Op_SHL 8 [mk_bv 8 1, mk_bv 8 8]) == 0
-- Far past it, with the exponent that used to panic.
#guard bv_uint (eval_op   .Op_SHL 8 [mk_bv 8 1, mk_bv 40 8589934576]) == 0
#guard bv_uint (denote_op .Op_SHL 8 [mk_bv 8 1, mk_bv 40 8589934576]) == 0
-- Zero width: no bits to hold anything.
#guard bv_uint (eval_op   .Op_SHL 0 [mk_bv 0 1, mk_bv 8 0]) == 0
