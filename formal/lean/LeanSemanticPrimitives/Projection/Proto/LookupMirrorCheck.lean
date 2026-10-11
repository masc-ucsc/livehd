/-
# The `nthD` lookup summary, integrated -- host/object mirror checks

Executable checks on CONCRETE runs pinning the PROVED host specializer
(`isNthD`, `nthSummaryEnv` in `PartialEvaluator.lean`) to its object mirror
(`isNthDL`, `nthEnvL` in `MixProgram.lean`):

1. host and object recognizers give the SAME verdict on every function of
   `hwAPVarT`, of `A_M` (mix itself) and of the toy interpreter;
2. exactly one function of `hwAPVarT` is recognised (`nthD`, index 2), none of
   mix's or the toy's, and the shared template is itself recognised;
3. on small hardware designs the OBJECT specializer yields the same residual as
   the host -- and the host yields the same residual as the S2a prototype that
   was measured (certio/PHASE6_PERF.md 36).

These are not `mixProgram_implements_mixHost`, which remains unproved.
-/
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorSummary
import LeanSemanticPrimitives.Projection.Proto.RewriteTotal
import LeanSemanticPrimitives.Projection.Gate0

namespace Projection
namespace LookupMirror
open Compiler Projection.ProtoVar

def objFn (name : String) : Nat :=
  (MixProg.mixS.funs.zipIdx.find? (·.1.name == name)).map (·.2) |>.getD 0

def recogHost (A : AProgram) : List Bool :=
  A.funs.zipIdx.map (fun (fd, i) => isNthD i fd)

def recogObj (A : AProgram) : List Bool :=
  A.funs.zipIdx.map (fun (fd, i) =>
    match evalFuel 100000 MixProg.mixProgram []
            (.call (objFn "isNthDL") [.lit (encAFunDef fd), .lit (encNat i)]) with
    | .value (.bool b) => b
    | _                => false)

-- 1. the recognizers agree, function by function
#guard recogHost hwAPVarT == recogObj hwAPVarT
#guard recogHost Gate0.A_M == recogObj Gate0.A_M
#guard recogHost Demo.interpA2P == recogObj Demo.interpA2P

-- 2. exactly `nthD`; nothing in mix or the toy; the template; a wrong self-index
#guard (recogHost hwAPVarT).count true == 1 && (recogHost hwAPVarT)[2]? == some true
#guard (recogHost Gate0.A_M).count true == 0
#guard (recogHost Demo.interpA2P).count true == 0
#guard isNthD 7 { params := [.dyn, .stat], ret := .dyn
                , body := .ite .dyn MixProg.nthCondT MixProg.nthBaseT (.ucall .dyn 7 MixProg.nthArgsT) }
#guard !isNthD 7 { params := [.dyn, .stat], ret := .dyn
                 , body := .ite .dyn MixProg.nthCondT MixProg.nthBaseT (.ucall .dyn 8 MixProg.nthArgsT) }

-- 3. residual agreement on small hardware designs
def gridD (nsrc nnode : Nat) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i =>
                if i < 2 then .input i 4 else .const 4 (Int.ofNat (i % 16)))
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4, deps := #[i % nsrc, (i + 1) % nsrc] })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

def objRes (D : DesignCert) : Option Program :=
  match evalFuel 10000000 MixProg.mixProgram []
          (.call MixProg.mixProgram.entry [.lit (encAProgram hwAPVarT), .lit (encVals [encDesign D])]) with
  | .value v => decProgram v
  | _        => none
def hostRes (D : DesignCert) : Option Program :=
  (mixDriver 200000 2000 hwAPVarT [encDesign D]).toOption
def protoRes (D : DesignCert) : Option Program :=
  (ProtoSum.mixDriver 200000 2000 hwAPVarT [encDesign D]).toOption

def agree (D : DesignCert) : Bool :=
  let o := objRes D
  o.isSome && o == hostRes D && hostRes D == protoRes D

#guard agree (gridD 2 1)
#guard agree (gridD 2 8)
#guard agree (gridD 8 8)
#guard agree (gridD 16 16)

end LookupMirror
end Projection
