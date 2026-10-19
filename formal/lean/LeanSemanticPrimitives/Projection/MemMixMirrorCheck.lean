/-
# The memory primitives: object specializer vs host specializer

`MixProgram.primTable` is the OBJECT-language mirror of `evalPrim`: when the
object specializer (`MixProg.mixProgram`, the one the second projection
self-applies) meets a STATIC application of a primitive, it folds it through
that table.  The table had entries for `memRead` (32) and `memWriteBE` (33) but
not for `memWrite` (34), so a static plain write fell through to `hd nil` -- a
`typeError` -- in the object specializer while the host folded it.

Executable checks, on concrete runs, for all three memory primitives:

  * fully STATIC application: host `mixDriver` and the object `mixProgram`
    give the SAME residual, and that residual's value is `evalPrim`'s;
  * fully DYNAMIC application: both residualise the same primitive node.

Like `Proto/LookupMirrorCheck.lean`, these are checks, not
`mixProgram_implements_mixHost`.
-/
import LeanSemanticPrimitives.Projection.MixProgram
import LeanSemanticPrimitives.Projection.MemImage
import LeanSemanticPrimitives.Projection.BTA
import LeanSemanticPrimitives.Projection.PartialEvaluator

namespace Projection
namespace MemMixMirror

open Compiler Projection.MemImg

/-- A one-function program applying `p` to its `n` arguments, in order. -/
def progOf (p : Prim) (n : Nat) : Program :=
  ⟨[⟨n, .prim p ((List.range n).map Term.var)⟩], 0⟩

/-- Annotate it with every parameter static (`bt = .stat`) or dynamic. -/
def annOf (p : Prim) (n : Nat) (bt : BT) : AProgram :=
  match bta (progOf p n) [false] (List.replicate n bt) 50 with
  | .ok a    => a
  | .error _ => ⟨[], 0⟩

def hostRes (A : AProgram) (st : List Val) : Option Program :=
  (mixDriver 20000 200 A st).toOption

def objRes (A : AProgram) (st : List Val) : Option Program :=
  match evalFuel 10000000 MixProg.mixProgram []
          (.call MixProg.mixProgram.entry [.lit (encAProgram A), .lit (encVals st)]) with
  | .value v => decProgram v
  | _        => none

/-- Both specializers produce a residual, and the same one. -/
def agrees (A : AProgram) (st : List Val) : Bool :=
  let o := objRes A st
  o.isSome && o == hostRes A st

/-- The value a 0-argument residual computes. -/
def resValue (R : Program) : Option Val :=
  match evalFuel 1000 R [] (.call R.entry []) with
  | .value v => some v
  | _        => none

def img : Val := encMemImg (ofMemConst 32 #[11, 22, 33])

def readArgs  : List Val := [.int 32, img, ofBV (mk_bv 5 1), ofBV (mk_bv 1 1)]
def writeBEArgs : List Val :=
  [.int 32, img, ofBV (mk_bv 5 1), ofBV (mk_bv 32 0x12345678), ofBV (mk_bv 4 5), .int 8]
def writeArgs : List Val := [img, ofBV (mk_bv 5 1), ofBV (mk_bv 32 99), ofBV (mk_bv 1 1)]

/-- Static: same residual both ways, and it computes `evalPrim`'s value. -/
def staticOK (p : Prim) (args : List Val) : Bool :=
  let A := annOf p args.length .stat
  agrees A args &&
    (match hostRes A args, evalPrim p args with
     | some R, .ok v => resValue R == some v
     | _, _          => false)

/-- Dynamic: same residual both ways. -/
def dynamicOK (p : Prim) (n : Nat) : Bool := agrees (annOf p n .dyn) []

#guard staticOK .memRead readArgs
#guard staticOK .memWriteBE writeBEArgs
#guard staticOK .memWrite writeArgs
#guard dynamicOK .memRead 4
#guard dynamicOK .memWriteBE 6
#guard dynamicOK .memWrite 4

-- The plain write's fold really performed the write (word 1 becomes 99).
#guard (match evalPrim .memWrite writeArgs with
        | .ok v => (decMemImg v).map (·.words) == some [mk_bv 32 11, mk_bv 32 99, mk_bv 32 33]
        | _ => false)

end MemMixMirror
end Projection
