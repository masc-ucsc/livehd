/-
# `prove_reified_incr`: the GENERATED incremental walk

`prove_reified` hands the whole binding chain to one `simp`. This command emits
the fast function directly from named per-binding values and then proves it
against `runBindings` one binding at a time, carrying only the facts `liveAfter`
says are still needed and generalizing every intermediate environment away.

Both paths stay available. `reify_design` / `prove_reified` are unchanged and are
what every sweep still uses; this exists to be compared against them.

Run: lake env lean probes/d3_incremental_gen_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
set_option maxRecDepth 4000000
set_option maxHeartbeats 0
open Compiler

--------------------------------------------------------------------------------
-- DIAMOND: a re-read, an early-dead value, and a terminal-only output
--
--   b0 -> slot 2   reads two SOURCES
--   b1 -> slot 3   reads b0          \  both read b0, so it is RE-READ
--   b2 -> slot 4   reads b0          /  and EARLY-DEAD once b2 has run
--   b3 -> slot 5   reads b1 and source 0 -- the FAR source read
--   output slot 5  read by NOTHING but the output
--------------------------------------------------------------------------------
def dia : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.const 8 (-1) ]
    nodes    := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],    origin := 1 }
                 , { op := LGraphOp.Op_Ror, width := 1, deps := #[2],    origin := 2 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[3, 0], origin := 3 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops    := #[]
    memories := #[] }

prove_reified_incr dia as dfi

-- the walk closed, and on nothing but the standard axioms
#print axioms dfi.walk
#print axioms dfi.correct

-- THE EARLY-DEAD FACT IS NOT TRANSPORTED. `liveAfter` is what the generator
-- reads, so asserting it here asserts what the emitted proof carries: slot 2 is
-- gone from the live set the moment its last consumer has run, and the cut never
-- exceeds 2 even though there are 4 bindings.
def diaR : ResidualProgram := match compileDesign dia with | .ok R => R | .error _ => default
#guard ((ReifyProof.liveAfter diaR)[2]!).contains (2, false) == false
#guard ((ReifyProof.liveAfter diaR).map (fun a => a.size)).toList.foldl max 0 == 2
-- and the TERMINAL-ONLY slot is retained to the end
#guard ((ReifyProof.liveAfter diaR)[3]!) == #[(5, false)]

-- the diamond really reconverges: val1 and val2 both name val0
#guard (dfi.val1 (sourceEnvArr dia.sources #[mk_bv 8 7] { flops := #[], mems := #[] }))
     == Residual.rnotV 8 (dfi.val0 (sourceEnvArr dia.sources #[mk_bv 8 7]
                                      { flops := #[], mems := #[] }))
#guard (dfi.val2 (sourceEnvArr dia.sources #[mk_bv 8 7] { flops := #[], mems := #[] }))
     == Residual.rredOrV 1 [dfi.val0 (sourceEnvArr dia.sources #[mk_bv 8 7]
                                        { flops := #[], mems := #[] })]

-- PARITY with the proven path, on the same design: the two fast functions agree
-- everywhere, by composing the two correctness theorems.
reify_design dia as dold
prove_reified dia as dold
theorem dia_agree : ∀ i st, dold i st = dfi i st := by
  intro i st; rw [dold.correct i st, dfi.correct i st]
#print axioms dia_agree

#eval IO.println "D3INCRGEN OK"
