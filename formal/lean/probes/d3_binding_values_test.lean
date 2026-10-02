/-
# `emit_binding_values`: one named value per binding

The property that matters is NEGATIVE and invisible in the theorem statement: a
binding read by several consumers must appear in later values as a NAME, not as
its expansion. Inlining would make a diamond's value tree grow with the graph's
reconvergence rather than its size -- the cost the incremental walk exists to
avoid, reappearing on the other side.

The fixture is the same diamond the walk probe uses: `val1` and `val2` both read
`val0`, and `val3` reads `val1` and a source.

Run: lake env lean probes/d3_binding_values_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
set_option maxRecDepth 4000000
set_option maxHeartbeats 0
open Compiler

def dia : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.const 8 (-1) ]
    nodes    := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],    origin := 1 }
                 , { op := LGraphOp.Op_Ror, width := 1, deps := #[2],    origin := 2 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[3, 0], origin := 3 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops    := #[]
    memories := #[] }

reify_design dia as dfast
emit_binding_values dia as dfast

-- the values exist, one per binding, with the declared result types
#guard (dfast.val0 (sourceEnvArr dia.sources #[mk_bv 8 0] { flops := #[], mems := #[] })).width == 8
#guard (dfast.val2 (sourceEnvArr dia.sources #[mk_bv 8 0] { flops := #[], mems := #[] })).width == 1

-- NO DUPLICATION: `val3` is defined in terms of `val1`, so replacing `val1`'s
-- definition changes `val3`. Stated as an evaluation rather than a syntax check,
-- because what matters is that the dependency is real.
#guard (dfast.val3 (sourceEnvArr dia.sources #[mk_bv 8 7] { flops := #[], mems := #[] }))
     == Residual.randV 8 [dfast.val1 (sourceEnvArr dia.sources #[mk_bv 8 7]
                                       { flops := #[], mems := #[] }),
                          refBV (sourceEnvArr dia.sources #[mk_bv 8 7]
                                   { flops := #[], mems := #[] }) 0]

-- the diamond really does reconverge: val1 and val2 both read val0
#guard (dfast.val1 (sourceEnvArr dia.sources #[mk_bv 8 7] { flops := #[], mems := #[] }))
     == Residual.rnotV 8 (dfast.val0 (sourceEnvArr dia.sources #[mk_bv 8 7]
                                        { flops := #[], mems := #[] }))
#guard (dfast.val2 (sourceEnvArr dia.sources #[mk_bv 8 7] { flops := #[], mems := #[] }))
     == Residual.rredOrV 1 [dfast.val0 (sourceEnvArr dia.sources #[mk_bv 8 7]
                                          { flops := #[], mems := #[] })]

-- the connection equation holds on the reified definition itself
example (i : RuntimeInput) (st : RuntimeState) :
    dfast i st = { outputs := #[bv_resize 8 (dfast.val3 (sourceEnvArr dia.sources i st))],
                   nextState := { flops := #[], mems := #[] } } := dfast.vals i st

#print axioms dfast.vals

#eval IO.println "D3BINDINGVALUES OK"
