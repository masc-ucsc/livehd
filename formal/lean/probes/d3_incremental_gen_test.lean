/-
# `prove_reified_incr`: the GENERATED incremental walk

`prove_reified` hands the whole binding chain to one `simp`. This command emits
the fast function directly from named per-binding values and then proves it
against `runBindings` one binding at a time, carrying only the facts `liveAfter`
says are still needed and generalizing every intermediate environment away.

Both paths stay available. `reify_design` / `prove_reified` are unchanged and are
what every sweep still uses; this exists to be compared against them.

Every `correct` theorem below is checked with `d3_proof_gate`, not `#print
axioms`: the gate THROWS on a disallowed axiom, so a `sorryAx` or an
`ofReduceBool` fails this file rather than being printed and scrolled past.

Run: lake env lean probes/d3_incremental_gen_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
set_option maxRecDepth 4000000
set_option maxHeartbeats 0
open Compiler

--------------------------------------------------------------------------------
-- 1. DIAMOND: a re-read, an early-dead value, a terminal-only output
--------------------------------------------------------------------------------
def dia : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.const 8 (-1) ]
    nodes    := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],    origin := 1 }
                 , { op := LGraphOp.Op_Ror, width := 1, deps := #[2],    origin := 2 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[3, 0], origin := 3 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops := #[], memories := #[] }
prove_reified_incr dia as dfi
d3_proof_gate dfi.correct

def diaR : ResidualProgram := match compileDesign dia with | .ok R => R | .error _ => default
-- the early-dead fact is NOT transported: slot 2 leaves the live set the moment
-- its last consumer has run, and the cut stays at 2 for 4 bindings
#guard ((ReifyProof.liveAfter diaR)[2]!).contains (2, false) == false
#guard ((ReifyProof.liveAfter diaR).map (fun a => a.size)).toList.foldl max 0 == 2
-- the terminal-only slot is retained to the end
#guard ((ReifyProof.liveAfter diaR)[3]!) == #[(5, false)]
-- the diamond reconverges: val1 and val2 both NAME val0 rather than inlining it
#guard (dfi.val1 (sourceEnvArr dia.sources #[mk_bv 8 7] { flops := #[], mems := #[] }))
     == Residual.rnotV 8 (dfi.val0 (sourceEnvArr dia.sources #[mk_bv 8 7]
                                      { flops := #[], mems := #[] }))
#guard (dfi.val2 (sourceEnvArr dia.sources #[mk_bv 8 7] { flops := #[], mems := #[] }))
     == Residual.rredOrV 1 [dfi.val0 (sourceEnvArr dia.sources #[mk_bv 8 7]
                                        { flops := #[], mems := #[] })]

-- PARITY with the proven path: the two fast functions agree EVERYWHERE, by
-- composing the two correctness theorems.
reify_design dia as dold
prove_reified dia as dold
theorem dia_agree : ∀ i st, dold i st = dfi i st := by
  intro i st; rw [dold.correct i st, dfi.correct i st]
d3_proof_gate dia_agree

--------------------------------------------------------------------------------
-- 2. ZERO terminal slots. The conclusion is `True` and the walk must still
--    construct and destructure it; a conjunction fold that assumed at least one
--    conjunct produced a shape neither `exact ⟨..⟩` nor `obtain` could take apart.
--------------------------------------------------------------------------------
namespace Nested
def zeroCert : DesignCert :=
  { sources := #[ SourceDesc.input 0 8 ]
    nodes   := #[ { op := LGraphOp.Op_Not, width := 8, deps := #[0], origin := 0 } ]
    outputs := #[], flops := #[], memories := #[] }
end Nested
prove_reified_incr Nested.zeroCert as zeroOut
d3_proof_gate zeroOut.correct

-- ... and through an ABBREV ALIAS. The join used to unfold the design identifier,
-- which made it sensitive to how the certificate was named; it now works from
-- generated projection lemmas only.
abbrev AliasCert := Nested.zeroCert
prove_reified_incr AliasCert as aliasOut
d3_proof_gate aliasOut.correct

--------------------------------------------------------------------------------
-- 3. THREE outputs: a SOURCE-only slot, a binding slot, and that binding slot
--    AGAIN at a different width. The duplicate must collapse to one walk fact.
--------------------------------------------------------------------------------
def multiOut : DesignCert :=
  { sources := #[ SourceDesc.input 0 8, SourceDesc.const 8 (-1) ]
    nodes   := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 }
                , { op := LGraphOp.Op_Not, width := 8, deps := #[2],    origin := 1 } ]
    outputs := #[ { slot := 0, width := 8 }, { slot := 3, width := 8 },
                  { slot := 3, width := 4 } ]
    flops := #[], memories := #[] }
prove_reified_incr multiOut as mo
d3_proof_gate mo.correct

--------------------------------------------------------------------------------
-- 4. A MEMORY-VALUED INTERMEDIATE, carried across an unrelated push.
--
--   slot 4  Op_MemWrite -> memory-valued
--   slot 5  Op_Not      -> an unrelated push in between
--   slot 6  Op_MemRead  -> reads slot 4, two pushes later
--
-- so slot 4 is transported by `bindAgreeMem_push`, not `refMem_push_self`.
-- `memories := #[]`, so there is no update path and the combinational command
-- still applies.
--------------------------------------------------------------------------------
def memD : DesignCert :=
  { sources := #[ SourceDesc.memConst 2 8 #[10, 20, 30, 40]
                , SourceDesc.input 0 2
                , SourceDesc.input 1 8
                , SourceDesc.const 1 (1) ]
    nodes   := #[ { op := LGraphOp.Op_MemWrite, width := 8, deps := #[0,1,2,3], origin := 0 }
                , { op := LGraphOp.Op_Not,      width := 2, deps := #[1],       origin := 1 }
                , { op := LGraphOp.Op_MemRead,  width := 8, deps := #[4,5,3],   origin := 2 } ]
    outputs := #[ { slot := 6, width := 8 } ]
    flops := #[], memories := #[] }
def memR : ResidualProgram := match compileDesign memD with | .ok R => R | .error _ => default
-- the intermediate really is memory-valued, and really is live across step 1
#guard memR.bindings[0]!.ty == ValueType.mem 0 8
#guard ((ReifyProof.liveAfter memR)[1]!).contains (4, true)
#guard ((ReifyProof.liveAfter memR)[1]!).contains (4, false) == false
prove_reified_incr memD as md
d3_proof_gate md.correct

#eval IO.println "D3INCRGEN OK"
