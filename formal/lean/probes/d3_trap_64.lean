import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler

def D64 : DesignCert :=
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
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[9, 1], origin := 8 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[10, 1], origin := 9 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[11, 1], origin := 10 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[12, 1], origin := 11 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[13, 1], origin := 12 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[14, 1], origin := 13 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[15, 1], origin := 14 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[16, 1], origin := 15 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[17, 1], origin := 16 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[18, 1], origin := 17 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[19, 1], origin := 18 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[20, 1], origin := 19 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[21, 1], origin := 20 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[22, 1], origin := 21 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[23, 1], origin := 22 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[24, 1], origin := 23 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[25, 1], origin := 24 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[26, 1], origin := 25 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[27, 1], origin := 26 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[28, 1], origin := 27 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[29, 1], origin := 28 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[30, 1], origin := 29 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[31, 1], origin := 30 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[32, 1], origin := 31 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[33, 1], origin := 32 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[34, 1], origin := 33 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[35, 1], origin := 34 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[36, 1], origin := 35 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[37, 1], origin := 36 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[38, 1], origin := 37 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[39, 1], origin := 38 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[40, 1], origin := 39 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[41, 1], origin := 40 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[42, 1], origin := 41 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[43, 1], origin := 42 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[44, 1], origin := 43 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[45, 1], origin := 44 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[46, 1], origin := 45 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[47, 1], origin := 46 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[48, 1], origin := 47 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[49, 1], origin := 48 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[50, 1], origin := 49 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[51, 1], origin := 50 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[52, 1], origin := 51 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[53, 1], origin := 52 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[54, 1], origin := 53 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[55, 1], origin := 54 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[56, 1], origin := 55 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[57, 1], origin := 56 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[58, 1], origin := 57 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[59, 1], origin := 58 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[60, 1], origin := 59 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[61, 1], origin := 60 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[62, 1], origin := 61 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[63, 1], origin := 62 }
  ,     { op := LGraphOp.Op_And, width := 8, deps := #[64, 1], origin := 63 }
    ]
    outputs  := #[{ slot := 65, width := 8 }]
    flops := #[], memories := #[] }

def R64 : ResidualProgram :=
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
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[9, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[10, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[11, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[12, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[13, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[14, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[15, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[16, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[17, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[18, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[19, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[20, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[21, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[22, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[23, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[24, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[25, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[26, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[27, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[28, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[29, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[30, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[31, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[32, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[33, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[34, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[35, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[36, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[37, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[38, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[39, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[40, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[41, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[42, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[43, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[44, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[45, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[46, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[47, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[48, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[49, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[50, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[51, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[52, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[53, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[54, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[55, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[56, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[57, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[58, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[59, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[60, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[61, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[62, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[63, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[64, 1] }
    ]
    outputs  := #[{ slot := 65, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

-- (a) THE TRAP TEST: does naming a ResidualProgram literal blow up?
theorem hc64 : compileDesign D64 = .ok R64 := by native_decide
