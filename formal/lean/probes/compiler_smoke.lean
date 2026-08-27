/-
Smoke test for the verified compiler (step 2 of the verification plan): tiny
certificates for add, mux, SRA, masks, reset/enable, memory, and malformed
graphs.  Every `#eval` below is a real execution of `denoteResidual` on the
output of `compileDesign`.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
open Compiler

def noState : RuntimeState := ⟨#[], #[]⟩
def run (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Option (Array BV) :=
  match compileDesign D with
  | .error _ => none
  | .ok R    => some (denoteResidual R i s).outputs

--------------------------------------------------------------------------------
-- 1. add:  y = a + b
--------------------------------------------------------------------------------
def dAdd : DesignCert :=
  { sources  := #[.input 0 8, .input 1 8]
    nodes    := #[{ op := .Op_Sum 2, width := 8, deps := #[0, 1] }]
    outputs  := #[{ slot := 2, width := 8 }]
    flops := #[], memories := #[] }
#eval run dAdd #[mk_bv 8 3, mk_bv 8 4] noState          -- expect [7]
#eval run dAdd #[mk_bv 8 200, mk_bv 8 100] noState      -- expect [44]  (wraps mod 256)

--------------------------------------------------------------------------------
-- 2. mux:  deps are [sel, falseVal, trueVal] -- ORDER is the point
--------------------------------------------------------------------------------
def dMux : DesignCert :=
  { sources  := #[.input 0 1, .input 1 8, .input 2 8]
    nodes    := #[{ op := .Op_MuxBool, width := 8, deps := #[0, 1, 2] }]
    outputs  := #[{ slot := 3, width := 8 }]
    flops := #[], memories := #[] }
#eval run dMux #[mk_bv 1 0, mk_bv 8 11, mk_bv 8 22] noState   -- sel=0 -> 11 (false arm)
#eval run dMux #[mk_bv 1 1, mk_bv 8 11, mk_bv 8 22] noState   -- sel=1 -> 22 (true arm)

--------------------------------------------------------------------------------
-- 3. SRA: sign preservation, and widening sign-extends
--------------------------------------------------------------------------------
def dSra : DesignCert :=
  { sources  := #[.input 0 8, .input 1 8]
    nodes    := #[{ op := .Op_SRA, width := 8, deps := #[0, 1] }]
    outputs  := #[{ slot := 2, width := 8 }]
    flops := #[], memories := #[] }
#eval (run dSra #[mk_bv 8 (-8), mk_bv 8 2] noState).map (·.map bv_sint)   -- expect [-2]

--------------------------------------------------------------------------------
-- 4. Get_mask: selected bits PACKED to the low end (mask 0b1010 over 0b1010 -> 3)
--------------------------------------------------------------------------------
def dMask : DesignCert :=
  { sources  := #[.input 0 8, .input 1 8]
    nodes    := #[{ op := .Op_GetMask, width := 8, deps := #[0, 1] }]
    outputs  := #[{ slot := 2, width := 8 }]
    flops := #[], memories := #[] }
#eval run dMask #[mk_bv 8 10, mk_bv 8 10] noState        -- expect [3], not [10]

--------------------------------------------------------------------------------
-- 5. general Sext: amount is a WIDTH, sign bit at amt-1.  The manual fast model
--    cannot express this shape at all (it ignores the amount operand).
--------------------------------------------------------------------------------
def dSext : DesignCert :=
  { sources  := #[.input 0 8, .input 1 8]
    nodes    := #[{ op := .Op_Sext, width := 8, deps := #[0, 1] }]
    outputs  := #[{ slot := 2, width := 8 }]
    flops := #[], memories := #[] }
#eval (run dSext #[mk_bv 8 8, mk_bv 8 4] noState).map (·.map bv_sint)   -- amt=4 -> -8
#eval (run dSext #[mk_bv 8 8, mk_bv 8 5] noState).map (·.map bv_sint)   -- amt=5 -> +8

--------------------------------------------------------------------------------
-- 6. flop with reset (active LOW, nonzero reset value) and enable
--------------------------------------------------------------------------------
def dFlop : DesignCert :=
  { sources  := #[.input 0 8, .input 1 1, .input 2 1, .flopQ 0 8]
    nodes    := #[]
    outputs  := #[{ slot := 3, width := 8 }]
    flops    := #[{ width := 8, din := 0, enable := some 1, resetPin := some 2,
                    resetValue := 0x5A, resetActiveLow := true }]
    memories := #[] }
def flopNextOf (i : RuntimeInput) (q : Int) : Option (Array BV) :=
  match compileDesign dFlop with
  | .error _ => none
  | .ok R    => some (denoteResidual R i ⟨#[mk_bv 8 q], #[]⟩).nextState.flops
--        din  en  nrst        q
#eval flopNextOf #[mk_bv 8 7, mk_bv 1 1, mk_bv 1 0] 99   -- nrst=0 -> ACTIVE -> 0x5A = 90
#eval flopNextOf #[mk_bv 8 7, mk_bv 1 1, mk_bv 1 1] 99   -- nrst=1, en=1 -> din = 7
#eval flopNextOf #[mk_bv 8 7, mk_bv 1 0, mk_bv 1 1] 99   -- nrst=1, en=0 -> hold 99
#eval flopNextOf #[mk_bv 8 7, mk_bv 1 0, mk_bv 1 0] 99   -- reset PRIORITY over enable -> 90

--------------------------------------------------------------------------------
-- 7. memory: byte-enabled write, then a read that FORWARDS from it
--------------------------------------------------------------------------------
def dMem : DesignCert :=
  { sources  := #[.memImg 0 4 8, .input 0 4, .input 1 8, .input 2 1]
    nodes    := #[{ op := .Op_MemWriteBE 4, width := 8, deps := #[0, 1, 2, 3] }
                , { op := .Op_MemRead,     width := 8, deps := #[4, 1, 3] }]
    outputs  := #[{ slot := 5, width := 8 }]
    flops    := #[]
    memories := #[{ aw := 4, dw := 8, nextImg := 4 }] }
def memRun (be : Int) : Option (Array BV) :=
  run dMem #[mk_bv 4 2, mk_bv 8 0xA5, mk_bv 1 be] ⟨#[], #[fun _ => mk_bv 8 0x0F]⟩
#eval memRun 1   -- be=1 -> low byte-lane enabled; forwarded read
#eval memRun 0   -- be=0 -> no write; read returns 0 (read enable is the same pin)

--------------------------------------------------------------------------------
-- 8. REFUSALS.  Each must be `.error`, never a zero-filled `.ok`.
--------------------------------------------------------------------------------
def dFwdDep : DesignCert :=    -- node 0 depends on a LATER slot
  { sources := #[.input 0 8], nodes := #[{ op := .Op_Not, width := 8, deps := #[2] }]
    outputs := #[], flops := #[], memories := #[] }
def dUnsupported : DesignCert :=
  { sources := #[.input 0 8, .input 1 8, .input 2 8]
    nodes := #[{ op := .Op_SetMask, width := 8, deps := #[0, 1, 2] }]
    outputs := #[], flops := #[], memories := #[] }
def dZeroWidth : DesignCert :=
  { sources := #[.input 0 8], nodes := #[{ op := .Op_Not, width := 0, deps := #[0] }]
    outputs := #[], flops := #[], memories := #[] }
def dBadArity : DesignCert :=
  { sources := #[.input 0 8], nodes := #[{ op := .Op_MuxBool, width := 8, deps := #[0] }]
    outputs := #[], flops := #[], memories := #[] }

#eval compileDesign dFwdDep      |>.toOption.isNone   -- expect true (refused)
#eval compileDesign dUnsupported |>.toOption.isNone   -- expect true
#eval compileDesign dZeroWidth   |>.toOption.isNone   -- expect true
#eval compileDesign dBadArity    |>.toOption.isNone   -- expect true

--------------------------------------------------------------------------------
-- 9. The theorem, instantiated -- in the shape the exporter emits.
--
-- NOTE the shape: no `ResidualProgram` appears in a theorem STATEMENT.  Naming
-- one there forces the KERNEL to reduce `compileDesign`, and since `Array.push`
-- is `⟨toList ++ [a]⟩` that is O(N^2) kernel terms.  Measured on SingleCycleCPU:
-- OOM at 27 min / 27 GB under a 40 GB cap (120 GB when uncapped), against 38 s /
-- 7.4 GB for this shape.  It is invisible at the size below, which is exactly why
-- it is worth writing down here.
--------------------------------------------------------------------------------

def dAdd_step : RuntimeInput → RuntimeState → RuntimeResult := compileAndRun dAdd

theorem dAdd_compiles : compilesOk dAdd = true := by native_decide

theorem dAdd_step_correct : ∀ inp st,
    dAdd_step inp st = interpretDesign dAdd inp st :=
  compileAndRun_correct dAdd dAdd_compiles

#eval (dAdd_step #[mk_bv 8 3, mk_bv 8 4] noState).outputs   -- expect 7
#print axioms dAdd_step_correct
