/-
Both mix paths for the new memory primitive, checked against the ACTUAL
PartialEvaluator code rather than inferred from it.

SCOPE, stated because the file name invites a stronger reading: these checks call
`evalPrim`, `primStruct` and `fragB` DIRECTLY.  They do NOT run `mixTerm`,
`mixDriver` or the object-level mix.  What they establish is that the components
`mixTerm`'s `.prim` case dispatches to behave as required; an actual mix
generation/execution run is separate evidence and is recorded in the ledger with
its own provenance, not produced here.

`mixTerm`'s `.prim` case does two different things:
  * `.stat` -> `allStatic` then `evalPrim p vs`, so a fully static application is
    FOLDED by the evaluator;
  * `.dyn`  -> `primStruct p rs`, and on `none` an opaque `.code (.prim p ...)`
    node, so a dynamic application RESIDUALISES generically.
-/
import LeanSemanticPrimitives.Projection.PartialEvaluator
import LeanSemanticPrimitives.Projection.MemImage
import LeanSemanticPrimitives.Projection.ResidualFragment
open Projection Projection.MemImg Projection.Hw Compiler

-- STATIC: the evaluator folds it, giving the ROM's initialised word.
#guard (match evalPrim .memRead
          [.int 32, encMemImg (ofMemConst 32 #[11, 22, 33]),
           ofBV (mk_bv 5 1), ofBV (mk_bv 1 1)] with
        | .ok v => v == ofBV (mk_bv 32 22)
        | _     => false)

-- DYNAMIC: no structural rule, so mix emits an opaque primitive node.
#guard (primStruct .memRead
          [.code (.var 0), .code (.var 1), .code (.var 2), .code (.var 3)]).isNone

-- and the residual node mix would build is a PRIMITIVE application, which is
-- inside `fragB`'s fragment: no call, no recursion.
#guard fragB 4 (.prim .memRead [.var 0, .var 1, .var 2, .var 3])

-- the arity gate `fragB` applies agrees with the declared arity
#guard Prim.arity .memRead == 4
#guard fragB 4 (.prim .memRead [.var 0, .var 1, .var 2]) == false

-- The two WRITE primitives, by the same component checks: a dynamic application
-- has no structural rule (so it residualises), the residual node is inside the
-- fragment at its declared arity, and the codes are the ones `MixProgram`'s
-- `primTable` mirrors (32/33/34).  The host-vs-object agreement for all three
-- is in `MemMixMirrorCheck.lean`.
#guard (primStruct .memWriteBE
          [.code (.var 0), .code (.var 1), .code (.var 2),
           .code (.var 3), .code (.var 4), .code (.var 5)]).isNone
#guard (primStruct .memWrite
          [.code (.var 0), .code (.var 1), .code (.var 2), .code (.var 3)]).isNone
#guard fragB 6 (.prim .memWriteBE [.var 0, .var 1, .var 2, .var 3, .var 4, .var 5])
#guard fragB 4 (.prim .memWrite [.var 0, .var 1, .var 2, .var 3])
#guard Prim.arity .memWriteBE == 6 && Prim.arity .memWrite == 4
#guard fragB 4 (.prim .memWrite [.var 0, .var 1, .var 2]) == false
#guard primCode .memRead == 32 && primCode .memWriteBE == 33 && primCode .memWrite == 34
