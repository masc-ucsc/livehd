/-
# The shared-let reifier against the named one

`reify_design_shared` must emit a model with the SAME meaning as
`reify_design_named`; only the sharing changes.  Each fixture below is emitted
both ways and the two results are compared with the checker's own
`sameResult`, on several stimuli, so a difference in any output, flop or memory
image fails the build.

The shapes are chosen to be the ones where a let-chain and a call-per-operand
could come apart: reconvergence (where the named form re-evaluates), sequential
state, and a memory-valued intermediate.

Run: lake env lean probes/d3_chunked_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyGen
import LeanSemanticPrimitives.Compiler.D3Harness

open Compiler

-- 1. a plain chain: no reconvergence at all
def fxChain : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8 ]
    nodes    := #[ { op := LGraphOp.Op_Not, width := 8, deps := #[0], origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[1], origin := 1 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[1,2], origin := 2 } ]
    outputs  := #[ { slot := 3, width := 8 } ]
    flops    := #[]
    memories := #[] }

-- 2. DIAMOND: slot 1 feeds both 2 and 3, which reconverge at 4. This is the
--    shape the named form pays for twice and the chunked form once.
def fxDiamond : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.input 1 8 ]
    nodes    := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0,1], origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],   origin := 1 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],   origin := 2 }
                 , { op := LGraphOp.Op_Or,  width := 8, deps := #[3,4], origin := 3 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[5,2], origin := 4 } ]
    outputs  := #[ { slot := 6, width := 8 } ]
    flops    := #[]
    memories := #[] }

-- 3. a flop with an enable and an active-low reset, plus a reconvergent din
def fxFlop : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.input 1 1, SourceDesc.input 2 1
                 , SourceDesc.flopQ 0 8 ]
    nodes    := #[ { op := LGraphOp.Op_Not, width := 8, deps := #[0], origin := 0 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[4,3], origin := 1 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops    := #[ { width := 8, din := 5, enable := some 1, resetPin := some 2,
                     resetValue := 7, resetActiveLow := true } ]
    memories := #[] }

-- 4. a memory-valued intermediate, read back in the same cycle
def fxMem : DesignCert :=
  { sources  := #[ SourceDesc.memImg 0 4 8, SourceDesc.input 0 4
                 , SourceDesc.input 1 8, SourceDesc.input 2 1 ]
    nodes    := #[ { op := LGraphOp.Op_MemWrite, width := 8, deps := #[0,1,2,3], origin := 0 }
                 , { op := LGraphOp.Op_MemRead,  width := 8, deps := #[4,1,3],   origin := 1 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops    := #[]
    memories := #[ { aw := 4, dw := 8, nextImg := 4 } ] }

reify_design_named   fxChain   as nChain
reify_design_shared fxChain   as cChain
reify_design_named   fxDiamond as nDiamond
reify_design_shared fxDiamond as cDiamond
reify_design_named   fxFlop    as nFlop
reify_design_shared fxFlop    as cFlop
reify_design_named   fxMem     as nMem
reify_design_shared fxMem     as cMem

open Compiler.D3 in
/-- Same result on stimulus `k`, judged by the checker's own comparison so that
outputs, flop next-state AND memory images all count. -/
def eqOn (D : DesignCert)
    (a b : RuntimeInput → RuntimeState → RuntimeResult) (k : Nat) : Bool :=
  sameResult (a (stimIn D k) (stimSt D k)) (b (stimIn D k) (stimSt D k)) (addrPlan D)

#guard eqOn fxChain   nChain   cChain   0
#guard eqOn fxChain   nChain   cChain   1
#guard eqOn fxChain   nChain   cChain   7
#guard eqOn fxDiamond nDiamond cDiamond 0
#guard eqOn fxDiamond nDiamond cDiamond 1
#guard eqOn fxDiamond nDiamond cDiamond 5
#guard eqOn fxDiamond nDiamond cDiamond 9
#guard eqOn fxFlop    nFlop    cFlop    0
#guard eqOn fxFlop    nFlop    cFlop    1
#guard eqOn fxFlop    nFlop    cFlop    4
#guard eqOn fxFlop    nFlop    cFlop    6
#guard eqOn fxMem     nMem     cMem     0
#guard eqOn fxMem     nMem     cMem     1
#guard eqOn fxMem     nMem     cMem     3
#guard eqOn fxMem     nMem     cMem     8

-- and against the CERTIFICATE, not just against each other: a shared mistake in
-- both reifiers would pass every guard above.
#guard Compiler.D3.agree fxChain   cChain   (match compileDesign fxChain   with | .ok R => R | _ => default) 16
#guard Compiler.D3.agree fxDiamond cDiamond (match compileDesign fxDiamond with | .ok R => R | _ => default) 16
#guard Compiler.D3.agree fxFlop    cFlop    (match compileDesign fxFlop    with | .ok R => R | _ => default) 16
#guard Compiler.D3.agree fxMem     cMem     (match compileDesign fxMem     with | .ok R => R | _ => default) 16

-- The CHUNKED model, with deliberately small chunk sizes so that composition
-- across several chunks is exercised rather than a single chunk standing in
-- for the whole design.
reify_design_chunked fxChain   as kChain   size 2
reify_design_chunked fxDiamond as kDiamond size 2
reify_design_chunked fxFlop    as kFlop    size 1
reify_design_chunked fxMem     as kMem     size 1

#guard eqOn fxChain   nChain   kChain   0
#guard eqOn fxChain   nChain   kChain   3
#guard eqOn fxDiamond nDiamond kDiamond 0
#guard eqOn fxDiamond nDiamond kDiamond 1
#guard eqOn fxDiamond nDiamond kDiamond 9
#guard eqOn fxFlop    nFlop    kFlop    0
#guard eqOn fxFlop    nFlop    kFlop    4
#guard eqOn fxFlop    nFlop    kFlop    6
#guard eqOn fxMem     nMem     kMem     0
#guard eqOn fxMem     nMem     kMem     3
#guard eqOn fxMem     nMem     kMem     8

#guard Compiler.D3.agree fxChain   kChain   (match compileDesign fxChain   with | .ok R => R | _ => default) 16
#guard Compiler.D3.agree fxDiamond kDiamond (match compileDesign fxDiamond with | .ok R => R | _ => default) 16
#guard Compiler.D3.agree fxFlop    kFlop    (match compileDesign fxFlop    with | .ok R => R | _ => default) 16
#guard Compiler.D3.agree fxMem     kMem     (match compileDesign fxMem     with | .ok R => R | _ => default) 16

#eval IO.println "D3SHARED OK"
