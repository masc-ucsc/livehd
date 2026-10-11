/-
# S2a mirror checks -- certio/PHASE6_PERF.md 35.4.  Executable, NOT proofs.

Pins the host prototype (`Proto/PartialEvaluatorSummary.lean`, `ProtoSum`) and
the object prototype (`Proto/MixProgramSummary.lean`, `MixProgSum`) together:

1. both forks resolve;
2. the host recognizer `isNthD` and the object recognizer `isNthDL` give the
   SAME verdict on every function of `hwAPVarT`, of `A_M` (mix itself) and of
   the toy interpreter -- run the object one through `evalFuel`;
3. exactly one function of `hwAPVarT` is recognised, and the template the
   object compares against is itself recognised by the host;
4. on small hardware designs the OBJECT specializer with the summary produces
   the SAME residual program as the host prototype, which is in turn the same
   as the PROVED S1 host's.

These are checks on concrete runs.  Nothing here is
`mixProgram_implements_mixHost`.
-/
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorSummary
import LeanSemanticPrimitives.Projection.Proto.MixProgramSummary
import LeanSemanticPrimitives.Projection.Proto.RewriteTotal
import LeanSemanticPrimitives.Projection.Gate0

namespace Projection
namespace S2aMirror
open Compiler Projection.ProtoVar

-- 1. both forks resolve
#guard MixProgSum.mixResolved.toOption.isSome

/-- Index of a named object function in the resolved summary fork. -/
def objFn (name : String) : Nat :=
  (MixProgSum.mixS.funs.zipIdx.find? (·.1.name == name)).map (·.2) |>.getD 0

def recogHost (A : AProgram) : List Bool :=
  A.funs.zipIdx.map (fun (fd, i) => ProtoSum.isNthD i fd)

def recogObj (A : AProgram) : List Bool :=
  A.funs.zipIdx.map (fun (fd, i) =>
    match evalFuel 100000 MixProgSum.mixProgram []
            (.call (objFn "isNthDL") [.lit (encAFunDef fd), .lit (encNat i)]) with
    | .value (.bool b) => b
    | _                => false)

-- 2. the recognizers agree, function by function, on three programs
#guard recogHost hwAPVarT == recogObj hwAPVarT
#guard recogHost Gate0.A_M == recogObj Gate0.A_M
#guard recogHost Demo.interpA2P == recogObj Demo.interpA2P

-- 3. exactly one recognised in the hardware interpreter, at index 2 (`nthD`);
--    none in mix itself or the toy; and the shared template is recognised
#guard (recogHost hwAPVarT).count true == 1 && (recogHost hwAPVarT)[2]? == some true
#guard (recogHost Gate0.A_M).count true == 0
#guard (recogHost Demo.interpA2P).count true == 0
#guard ProtoSum.isNthD 7 { params := [.dyn, .stat], ret := .dyn
                         , body := .ite .dyn MixProgSum.nthCondT MixProgSum.nthBaseT
                                     (.ucall .dyn 7 MixProgSum.nthArgsT) }
-- ...and a wrong self-index is NOT
#guard !ProtoSum.isNthD 7 { params := [.dyn, .stat], ret := .dyn
                          , body := .ite .dyn MixProgSum.nthCondT MixProgSum.nthBaseT
                                      (.ucall .dyn 8 MixProgSum.nthArgsT) }

-- 4. residual agreement on small hardware designs
def gridD (nsrc nnode : Nat) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i =>
                if i < 2 then .input i 4 else .const 4 (Int.ofNat (i % 16)))
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4, deps := #[i % nsrc, (i + 1) % nsrc] })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

def objSum (D : DesignCert) : Option Program :=
  match evalFuel 10000000 MixProgSum.mixProgram []
          (.call MixProgSum.mixProgram.entry
            [.lit (encAProgram hwAPVarT), .lit (encVals [encDesign D])]) with
  | .value v => decProgram v
  | _        => none
def hostSum (D : DesignCert) : Option Program :=
  (ProtoSum.mixDriver 200000 2000 hwAPVarT [encDesign D]).toOption
def hostS1 (D : DesignCert) : Option Program :=
  (mixDriver 200000 2000 hwAPVarT [encDesign D]).toOption

def agree3 (D : DesignCert) : Bool :=
  let o := objSum D
  o.isSome && o == hostSum D && hostSum D == hostS1 D

#guard agree3 (gridD 2 1)
#guard agree3 (gridD 2 8)
#guard agree3 (gridD 8 8)
#guard agree3 (gridD 16 16)

end S2aMirror
end Projection
