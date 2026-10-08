import LeanSemanticPrimitives.Translation.LGraphModel

-- A concrete two-node graph, the shape the emitter produces.
def G : GraphCert :=
  { topo := [16], sources := [12, 14],
    nodes := nodes_of_list [ { nid := 16, op := LGraphOp.Op_And, width := 8, deps := [12, 14] } ] }

def phi : Nat → BV := fun n => if n = 16 then mk_bv 8 5 else mk_bv 8 3

-- THE QUESTION: does the per-node `show` the emitter writes still close by DEFEQ
-- when evalNode goes through the NodeSemantics class?  If the instance projection
-- blocks whnf, this fails and Branch 2 is dead.
example : evalNode G phi 16 = eval_op LGraphOp.Op_And 8 [phi 12, phi 14] := rfl

-- the CertVal instance, same question
def phiC : Nat → CertVal := fun n => CertVal.bv (mk_bv 8 3)
example : evalNodeC G phiC 16 = eval_op_cert LGraphOp.Op_And 8 [phiC 12, phiC 14] := rfl

-- and the whole-graph fold, one step
example : evalGraph [16] G phi = envSet phi 16 (evalNode G phi 16) := rfl

-- the CertVal `show` shape the memory bridge emits (constructor + congrArg)
example : evalNodeC G phiC 16
        = CertVal.bv (eval_op LGraphOp.Op_And 8 [(phiC 12).asBV, (phiC 14).asBV]) := rfl
