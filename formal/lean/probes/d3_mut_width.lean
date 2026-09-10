import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler Compiler.Residual

def RM2 : ResidualProgram :=
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
    ]
    outputs  := #[{ slot := 17, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

def fastM2 (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let s0 := bv_resize 8 (i[0]?.getD (mk_bv 8 0))
  let s1 := bv_resize 8 (i[1]?.getD (mk_bv 8 0))
  let n0 := randV 8 [s0, s1]
  let n1 := randV 8 [n0, s1]
  let n2 := randV 8 [n1, s1]
  let n3 := randV 8 [n2, s1]
  let n4 := randV 8 [n3, s1]
  let n5 := randV 8 [n4, s1]
  let n6 := randV 8 [n5, s1]
  let n7 := randV 4 [n6, s1]
  let n8 := randV 8 [n7, s1]
  let n9 := randV 8 [n8, s1]
  let n10 := randV 8 [n9, s1]
  let n11 := randV 8 [n10, s1]
  let n12 := randV 8 [n11, s1]
  let n13 := randV 8 [n12, s1]
  let n14 := randV 8 [n13, s1]
  let n15 := randV 8 [n14, s1]
  { outputs := #[bv_resize 8 n15]
    nextState := { flops := #[], mems := #[] } }

theorem fastM2_correct : ∀ i s, fastM2 i s = denoteResidual RM2 i s := by
  intro i s
  simp [fastM2, denoteResidual, RM2, runBindings, sourceEnvArr, denoteExpr, refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
