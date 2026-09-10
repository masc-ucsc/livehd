import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler

def D8 : DesignCert :=
  { sources  := #[SourceDesc.input 0 8, SourceDesc.input 1 8]
    nodes    := #[
    { op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[2, 1], origin := 1 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[3, 1], origin := 2 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[4, 1], origin := 3 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[5, 1], origin := 4 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[6, 1], origin := 5 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[7, 1], origin := 6 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[8, 1], origin := 7 }
    ]
    outputs  := #[{ slot := 9, width := 8 }]
    flops := #[], memories := #[] }

def R8 : ResidualProgram :=
  { sources  := #[SourceDesc.input 0 8, SourceDesc.input 1 8]
    bindings := #[
    { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[0, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[2, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[3, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[4, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[5, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[6, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[7, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[8, 1] }
    ]
    outputs  := #[{ slot := 9, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

-- (a) THE TRAP TEST: does naming a ResidualProgram literal blow up?
theorem hc8 : compileDesign D8 = .ok R8 := by native_decide
