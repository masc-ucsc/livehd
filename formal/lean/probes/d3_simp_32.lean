import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler Compiler.Residual

def R32 : ResidualProgram :=
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
    ]
    outputs  := #[{ slot := 33, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

def fast32 (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let s0 := bv_resize 8 (i[0]?.getD (mk_bv 8 0))
  let s1 := bv_resize 8 (i[1]?.getD (mk_bv 8 0))
  let n0 := randV 8 [s0, s1]
  let n1 := randV 8 [n0, s1]
  let n2 := randV 8 [n1, s1]
  let n3 := randV 8 [n2, s1]
  let n4 := randV 8 [n3, s1]
  let n5 := randV 8 [n4, s1]
  let n6 := randV 8 [n5, s1]
  let n7 := randV 8 [n6, s1]
  let n8 := randV 8 [n7, s1]
  let n9 := randV 8 [n8, s1]
  let n10 := randV 8 [n9, s1]
  let n11 := randV 8 [n10, s1]
  let n12 := randV 8 [n11, s1]
  let n13 := randV 8 [n12, s1]
  let n14 := randV 8 [n13, s1]
  let n15 := randV 8 [n14, s1]
  let n16 := randV 8 [n15, s1]
  let n17 := randV 8 [n16, s1]
  let n18 := randV 8 [n17, s1]
  let n19 := randV 8 [n18, s1]
  let n20 := randV 8 [n19, s1]
  let n21 := randV 8 [n20, s1]
  let n22 := randV 8 [n21, s1]
  let n23 := randV 8 [n22, s1]
  let n24 := randV 8 [n23, s1]
  let n25 := randV 8 [n24, s1]
  let n26 := randV 8 [n25, s1]
  let n27 := randV 8 [n26, s1]
  let n28 := randV 8 [n27, s1]
  let n29 := randV 8 [n28, s1]
  let n30 := randV 8 [n29, s1]
  let n31 := randV 8 [n30, s1]
  { outputs := #[bv_resize 8 n31]
    nextState := { flops := #[], mems := #[] } }

theorem fast32_correct : ∀ i s, fast32 i s = denoteResidual R32 i s := by
  intro i s
  simp [fast32, denoteResidual, R32, runBindings, sourceEnvArr, denoteExpr, refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
