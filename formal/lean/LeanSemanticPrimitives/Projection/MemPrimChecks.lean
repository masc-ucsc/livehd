/-
Both mix paths for the new memory primitive, checked against the ACTUAL
PartialEvaluator code rather than inferred from it.

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
