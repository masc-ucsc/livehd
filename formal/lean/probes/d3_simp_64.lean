import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler Compiler.Residual

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

def fast64 (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
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
  let n32 := randV 8 [n31, s1]
  let n33 := randV 8 [n32, s1]
  let n34 := randV 8 [n33, s1]
  let n35 := randV 8 [n34, s1]
  let n36 := randV 8 [n35, s1]
  let n37 := randV 8 [n36, s1]
  let n38 := randV 8 [n37, s1]
  let n39 := randV 8 [n38, s1]
  let n40 := randV 8 [n39, s1]
  let n41 := randV 8 [n40, s1]
  let n42 := randV 8 [n41, s1]
  let n43 := randV 8 [n42, s1]
  let n44 := randV 8 [n43, s1]
  let n45 := randV 8 [n44, s1]
  let n46 := randV 8 [n45, s1]
  let n47 := randV 8 [n46, s1]
  let n48 := randV 8 [n47, s1]
  let n49 := randV 8 [n48, s1]
  let n50 := randV 8 [n49, s1]
  let n51 := randV 8 [n50, s1]
  let n52 := randV 8 [n51, s1]
  let n53 := randV 8 [n52, s1]
  let n54 := randV 8 [n53, s1]
  let n55 := randV 8 [n54, s1]
  let n56 := randV 8 [n55, s1]
  let n57 := randV 8 [n56, s1]
  let n58 := randV 8 [n57, s1]
  let n59 := randV 8 [n58, s1]
  let n60 := randV 8 [n59, s1]
  let n61 := randV 8 [n60, s1]
  let n62 := randV 8 [n61, s1]
  let n63 := randV 8 [n62, s1]
  { outputs := #[bv_resize 8 n63]
    nextState := { flops := #[], mems := #[] } }

theorem fast64_correct : ∀ i s, fast64 i s = denoteResidual R64 i s := by
  intro i s
  simp [fast64, denoteResidual, R64, runBindings, sourceEnvArr, denoteExpr, refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
