/-
# `prove_reified_incr` with FLOPS

The emitter now covers sequential next-state. The flop result is NOT restated on
the fast side: it goes through the same `flopNextV` the source semantics uses, so
reset priority, polarity and the enable-false fallback exist once. Only the
operand READS differ -- named values instead of environment lookups.

Flop operands are also why the walk's terminal facts had to grow: a flop's `din`,
`enable` and `resetPin` are read by nothing later in the binding list, so a
conclusion built from the outputs alone would drop exactly the facts the flop
result needs.

Memory NEXT-IMAGES remain refused.

Run: lake env lean probes/d3_flop_gen_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
set_option maxRecDepth 4000000
set_option maxHeartbeats 0
open Compiler

--------------------------------------------------------------------------------
-- TWO flops updating simultaneously, with the control refs crossed over:
--   flop 0  enable from a SOURCE, reset from a PRODUCED slot, active HIGH, rv 0
--   flop 1  enable from a PRODUCED slot, reset from a SOURCE, active LOW, rv 300
-- so both ref kinds appear in both control positions, and the resetValue needs
-- truncating at width 8.
--------------------------------------------------------------------------------
def twoF : DesignCert :=
  { sources := #[ SourceDesc.input 0 8      -- 0 data
                , SourceDesc.input 1 1      -- 1 a control bit
                , SourceDesc.flopQ 0 8      -- 2 old flop 0
                , SourceDesc.flopQ 1 8 ]    -- 3 old flop 1
    nodes   := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0, 2], origin := 0 }
                , { op := LGraphOp.Op_Ror, width := 1, deps := #[4],    origin := 1 }
                , { op := LGraphOp.Op_And, width := 8, deps := #[0, 3], origin := 2 } ]
    outputs := #[ { slot := 4, width := 8 } ]
    flops   := #[ { width := 8, din := 4, enable := some 1, resetPin := some 5,
                    resetValue := 0, resetActiveLow := false }
                , { width := 8, din := 6, enable := some 5, resetPin := some 1,
                    resetValue := 300, resetActiveLow := true } ]
    memories := #[] }

prove_reified_incr twoF as tf
d3_proof_gate tf.correct

-- the flop operands really are terminal: nothing later in the binding list reads
-- slot 5 (flop 0's reset / flop 1's enable) or slot 6 (flop 1's din)
def twoR : ResidualProgram := match compileDesign twoF with | .ok R => R | .error _ => default
#guard (ReifyProof.terminalRefs twoR).contains (4, false)   -- output AND flop 0 din
#guard (ReifyProof.terminalRefs twoR).contains (5, false)   -- control only
#guard (ReifyProof.terminalRefs twoR).contains (6, false)   -- flop 1 din only
#guard ((ReifyProof.liveAfter twoR)[2]!).contains (6, false)

-- PARITY is by EVALUATION here, not by composing the two correctness theorems,
-- and the reason is itself a finding: `prove_reified` CANNOT prove this design.
-- Its single `simp` does not close the flop case, and the theorem it emits
-- depends on `sorryAx` (checked directly: `prove_reified twoF as ...` leaves
-- unsolved goals and `#print axioms` reports sorryAx). So there is no old
-- theorem to compose with.
--
-- This is caught rather than silently credited: `d3_proof_gate` THROWS on
-- `sorryAx`, so a sweep running `--prove` over a flop design gets no marker and
-- records proof=0. What is misleading is only `prove_reified`'s own
-- "proved" log line, which fires regardless.
--
-- `reify_design`'s FUNCTION is fine -- only the proof fails -- so it is still
-- the right thing to compare the emitted function against.
reify_design twoF as tfOld

--------------------------------------------------------------------------------
-- Evaluation, including a state with FEWER entries than flops so the missing
-- index falls back to zero rather than to some neighbouring flop's value.
--------------------------------------------------------------------------------
private def inp (a b : Int) : RuntimeInput := #[mk_bv 8 a, mk_bv 1 b]
private def stFull : RuntimeState := { flops := #[mk_bv 8 170, mk_bv 8 85], mems := #[] }
private def stShort : RuntimeState := { flops := #[mk_bv 8 170], mems := #[] }
private def stNone : RuntimeState := { flops := #[], mems := #[] }

#guard (List.range 6).all fun k =>
  let i := inp (Int.ofNat (k * 37 + 1)) (Int.ofNat (k % 2))
  (tfOld i stFull).nextState.flops == (tf i stFull).nextState.flops
  && (tfOld i stFull).outputs == (tf i stFull).outputs
-- the edge stimuli, and the two short states
#guard (tfOld (inp 0 0) stNone).nextState.flops == (tf (inp 0 0) stNone).nextState.flops
#guard (tfOld (inp (-1) (-1)) stNone).nextState.flops == (tf (inp (-1) (-1)) stNone).nextState.flops
#guard (tfOld (inp 0 0) stShort).nextState.flops == (tf (inp 0 0) stShort).nextState.flops
#guard (tfOld (inp (-1) 1) stShort).nextState.flops == (tf (inp (-1) 1) stShort).nextState.flops
-- two flops really are produced, in order
#guard (tf (inp 5 1) stFull).nextState.flops.size == 2

--------------------------------------------------------------------------------
-- No reset and no enable at all: the plain case must still work.
--------------------------------------------------------------------------------
def plainF : DesignCert :=
  { sources := #[ SourceDesc.input 0 8, SourceDesc.flopQ 0 8 ]
    nodes   := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 } ]
    outputs := #[ { slot := 2, width := 8 } ]
    flops   := #[ { width := 8, din := 2, enable := none, resetPin := none,
                    resetValue := 0, resetActiveLow := false } ]
    memories := #[] }
prove_reified_incr plainF as pf
d3_proof_gate pf.correct

--------------------------------------------------------------------------------
-- A flop whose din is a SOURCE, not a produced slot.
--------------------------------------------------------------------------------
def srcDinF : DesignCert :=
  { sources := #[ SourceDesc.input 0 8, SourceDesc.flopQ 0 8 ]
    nodes   := #[ { op := LGraphOp.Op_Not, width := 8, deps := #[1], origin := 0 } ]
    outputs := #[ { slot := 2, width := 8 } ]
    flops   := #[ { width := 8, din := 0, enable := none, resetPin := none,
                    resetValue := 0, resetActiveLow := false } ]
    memories := #[] }
prove_reified_incr srcDinF as sf
d3_proof_gate sf.correct

#eval IO.println "D3FLOPGEN OK"
