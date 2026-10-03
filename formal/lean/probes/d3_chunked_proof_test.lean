/-
# The chunked model, PROVED

Each fixture is emitted by `reify_design_chunked` with a deliberately small
chunk size, so composition across several chunks is exercised rather than one
chunk standing in for the design, and then proved by `prove_reified_chunked`.

`d3_proof_gate` prints the marker only after the axiom audit passes, so a
`proof=1` line here is a kernel-checked `<F> = interpretDesign <D>` depending on
nothing outside the allowed three.

Run: lake env lean probes/d3_chunked_proof_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
import LeanSemanticPrimitives.Compiler.D3Harness

open Compiler

def pChain : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8 ]
    nodes    := #[ { op := LGraphOp.Op_Not, width := 8, deps := #[0],   origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[1],   origin := 1 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[1,2], origin := 2 } ]
    outputs  := #[ { slot := 3, width := 8 } ]
    flops    := #[]
    memories := #[] }

-- the shape the named reifier re-evaluates: slot 2 feeds 3 and 4, reconverging at 5
def pDiamond : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.input 1 8 ]
    nodes    := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0,1], origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],   origin := 1 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],   origin := 2 }
                 , { op := LGraphOp.Op_Or,  width := 8, deps := #[3,4], origin := 3 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[5,2], origin := 4 } ]
    outputs  := #[ { slot := 6, width := 8 } ]
    flops    := #[]
    memories := #[] }

def pFlop : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.input 1 1, SourceDesc.input 2 1
                 , SourceDesc.flopQ 0 8 ]
    nodes    := #[ { op := LGraphOp.Op_Not, width := 8, deps := #[0],   origin := 0 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[4,3], origin := 1 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops    := #[ { width := 8, din := 5, enable := some 1, resetPin := some 2,
                     resetValue := 7, resetActiveLow := true } ]
    memories := #[] }

def pMem : DesignCert :=
  { sources  := #[ SourceDesc.memImg 0 4 8, SourceDesc.input 0 4
                 , SourceDesc.input 1 8, SourceDesc.input 2 1 ]
    nodes    := #[ { op := LGraphOp.Op_MemWrite, width := 8, deps := #[0,1,2,3], origin := 0 }
                 , { op := LGraphOp.Op_MemRead,  width := 8, deps := #[4,1,3],   origin := 1 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops    := #[]
    memories := #[ { aw := 4, dw := 8, nextImg := 4 } ] }

reify_design_chunked pChain   as qChain   size 2
prove_reified_chunked pChain  as qChain   size 2
d3_proof_gate qChain.correct

reify_design_chunked pDiamond as qDiamond size 2
prove_reified_chunked pDiamond as qDiamond size 2
d3_proof_gate qDiamond.correct

reify_design_chunked pFlop    as qFlop    size 1
prove_reified_chunked pFlop   as qFlop    size 1
d3_proof_gate qFlop.correct

reify_design_chunked pMem     as qMem     size 1
prove_reified_chunked pMem    as qMem     size 1
d3_proof_gate qMem.correct

#eval IO.println "D3CHUNKEDPROOF OK"
