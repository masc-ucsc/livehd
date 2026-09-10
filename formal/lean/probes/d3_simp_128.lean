import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler Compiler.Residual

def R128 : ResidualProgram :=
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
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[65, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[66, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[67, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[68, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[69, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[70, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[71, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[72, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[73, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[74, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[75, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[76, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[77, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[78, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[79, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[80, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[81, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[82, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[83, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[84, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[85, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[86, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[87, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[88, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[89, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[90, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[91, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[92, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[93, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[94, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[95, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[96, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[97, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[98, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[99, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[100, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[101, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[102, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[103, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[104, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[105, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[106, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[107, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[108, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[109, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[110, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[111, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[112, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[113, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[114, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[115, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[116, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[117, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[118, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[119, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[120, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[121, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[122, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[123, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[124, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[125, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[126, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[127, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[128, 1] }
    ]
    outputs  := #[{ slot := 129, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

def fast128 (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
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
  let n64 := randV 8 [n63, s1]
  let n65 := randV 8 [n64, s1]
  let n66 := randV 8 [n65, s1]
  let n67 := randV 8 [n66, s1]
  let n68 := randV 8 [n67, s1]
  let n69 := randV 8 [n68, s1]
  let n70 := randV 8 [n69, s1]
  let n71 := randV 8 [n70, s1]
  let n72 := randV 8 [n71, s1]
  let n73 := randV 8 [n72, s1]
  let n74 := randV 8 [n73, s1]
  let n75 := randV 8 [n74, s1]
  let n76 := randV 8 [n75, s1]
  let n77 := randV 8 [n76, s1]
  let n78 := randV 8 [n77, s1]
  let n79 := randV 8 [n78, s1]
  let n80 := randV 8 [n79, s1]
  let n81 := randV 8 [n80, s1]
  let n82 := randV 8 [n81, s1]
  let n83 := randV 8 [n82, s1]
  let n84 := randV 8 [n83, s1]
  let n85 := randV 8 [n84, s1]
  let n86 := randV 8 [n85, s1]
  let n87 := randV 8 [n86, s1]
  let n88 := randV 8 [n87, s1]
  let n89 := randV 8 [n88, s1]
  let n90 := randV 8 [n89, s1]
  let n91 := randV 8 [n90, s1]
  let n92 := randV 8 [n91, s1]
  let n93 := randV 8 [n92, s1]
  let n94 := randV 8 [n93, s1]
  let n95 := randV 8 [n94, s1]
  let n96 := randV 8 [n95, s1]
  let n97 := randV 8 [n96, s1]
  let n98 := randV 8 [n97, s1]
  let n99 := randV 8 [n98, s1]
  let n100 := randV 8 [n99, s1]
  let n101 := randV 8 [n100, s1]
  let n102 := randV 8 [n101, s1]
  let n103 := randV 8 [n102, s1]
  let n104 := randV 8 [n103, s1]
  let n105 := randV 8 [n104, s1]
  let n106 := randV 8 [n105, s1]
  let n107 := randV 8 [n106, s1]
  let n108 := randV 8 [n107, s1]
  let n109 := randV 8 [n108, s1]
  let n110 := randV 8 [n109, s1]
  let n111 := randV 8 [n110, s1]
  let n112 := randV 8 [n111, s1]
  let n113 := randV 8 [n112, s1]
  let n114 := randV 8 [n113, s1]
  let n115 := randV 8 [n114, s1]
  let n116 := randV 8 [n115, s1]
  let n117 := randV 8 [n116, s1]
  let n118 := randV 8 [n117, s1]
  let n119 := randV 8 [n118, s1]
  let n120 := randV 8 [n119, s1]
  let n121 := randV 8 [n120, s1]
  let n122 := randV 8 [n121, s1]
  let n123 := randV 8 [n122, s1]
  let n124 := randV 8 [n123, s1]
  let n125 := randV 8 [n124, s1]
  let n126 := randV 8 [n125, s1]
  let n127 := randV 8 [n126, s1]
  { outputs := #[bv_resize 8 n127]
    nextState := { flops := #[], mems := #[] } }

theorem fast128_correct : ∀ i s, fast128 i s = denoteResidual R128 i s := by
  intro i s
  simp [fast128, denoteResidual, R128, runBindings, sourceEnvArr, denoteExpr, refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
