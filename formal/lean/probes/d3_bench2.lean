import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 400000
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

def shallowA64 (i : RuntimeInput) (_s : RuntimeState) : RuntimeResult :=
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
  { outputs := #[bv_resize 8 n63], nextState := { flops := #[], mems := #[] } }

def shallowB64 (i : RuntimeInput) (_s : RuntimeState) : RuntimeResult :=
  let s0 := bv_resize 8 (i[0]?.getD (mk_bv 8 0))
  let s1 := bv_resize 8 (i[1]?.getD (mk_bv 8 0))
  let n0 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 s0) s1
  let n1 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n0) s1
  let n2 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n1) s1
  let n3 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n2) s1
  let n4 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n3) s1
  let n5 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n4) s1
  let n6 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n5) s1
  let n7 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n6) s1
  let n8 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n7) s1
  let n9 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n8) s1
  let n10 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n9) s1
  let n11 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n10) s1
  let n12 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n11) s1
  let n13 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n12) s1
  let n14 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n13) s1
  let n15 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n14) s1
  let n16 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n15) s1
  let n17 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n16) s1
  let n18 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n17) s1
  let n19 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n18) s1
  let n20 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n19) s1
  let n21 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n20) s1
  let n22 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n21) s1
  let n23 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n22) s1
  let n24 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n23) s1
  let n25 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n24) s1
  let n26 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n25) s1
  let n27 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n26) s1
  let n28 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n27) s1
  let n29 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n28) s1
  let n30 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n29) s1
  let n31 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n30) s1
  let n32 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n31) s1
  let n33 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n32) s1
  let n34 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n33) s1
  let n35 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n34) s1
  let n36 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n35) s1
  let n37 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n36) s1
  let n38 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n37) s1
  let n39 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n38) s1
  let n40 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n39) s1
  let n41 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n40) s1
  let n42 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n41) s1
  let n43 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n42) s1
  let n44 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n43) s1
  let n45 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n44) s1
  let n46 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n45) s1
  let n47 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n46) s1
  let n48 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n47) s1
  let n49 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n48) s1
  let n50 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n49) s1
  let n51 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n50) s1
  let n52 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n51) s1
  let n53 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n52) s1
  let n54 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n53) s1
  let n55 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n54) s1
  let n56 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n55) s1
  let n57 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n56) s1
  let n58 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n57) s1
  let n59 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n58) s1
  let n60 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n59) s1
  let n61 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n60) s1
  let n62 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n61) s1
  let n63 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n62) s1
  { outputs := #[bv_resize 8 n63], nextState := { flops := #[], mems := #[] } }

def R256 : ResidualProgram :=
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
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[129, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[130, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[131, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[132, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[133, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[134, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[135, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[136, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[137, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[138, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[139, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[140, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[141, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[142, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[143, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[144, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[145, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[146, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[147, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[148, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[149, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[150, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[151, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[152, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[153, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[154, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[155, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[156, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[157, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[158, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[159, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[160, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[161, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[162, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[163, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[164, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[165, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[166, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[167, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[168, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[169, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[170, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[171, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[172, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[173, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[174, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[175, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[176, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[177, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[178, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[179, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[180, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[181, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[182, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[183, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[184, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[185, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[186, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[187, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[188, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[189, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[190, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[191, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[192, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[193, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[194, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[195, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[196, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[197, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[198, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[199, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[200, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[201, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[202, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[203, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[204, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[205, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[206, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[207, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[208, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[209, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[210, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[211, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[212, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[213, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[214, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[215, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[216, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[217, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[218, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[219, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[220, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[221, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[222, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[223, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[224, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[225, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[226, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[227, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[228, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[229, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[230, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[231, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[232, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[233, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[234, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[235, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[236, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[237, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[238, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[239, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[240, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[241, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[242, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[243, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[244, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[245, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[246, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[247, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[248, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[249, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[250, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[251, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[252, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[253, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[254, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[255, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[256, 1] }
    ]
    outputs  := #[{ slot := 257, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

def shallowA256 (i : RuntimeInput) (_s : RuntimeState) : RuntimeResult :=
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
  let n128 := randV 8 [n127, s1]
  let n129 := randV 8 [n128, s1]
  let n130 := randV 8 [n129, s1]
  let n131 := randV 8 [n130, s1]
  let n132 := randV 8 [n131, s1]
  let n133 := randV 8 [n132, s1]
  let n134 := randV 8 [n133, s1]
  let n135 := randV 8 [n134, s1]
  let n136 := randV 8 [n135, s1]
  let n137 := randV 8 [n136, s1]
  let n138 := randV 8 [n137, s1]
  let n139 := randV 8 [n138, s1]
  let n140 := randV 8 [n139, s1]
  let n141 := randV 8 [n140, s1]
  let n142 := randV 8 [n141, s1]
  let n143 := randV 8 [n142, s1]
  let n144 := randV 8 [n143, s1]
  let n145 := randV 8 [n144, s1]
  let n146 := randV 8 [n145, s1]
  let n147 := randV 8 [n146, s1]
  let n148 := randV 8 [n147, s1]
  let n149 := randV 8 [n148, s1]
  let n150 := randV 8 [n149, s1]
  let n151 := randV 8 [n150, s1]
  let n152 := randV 8 [n151, s1]
  let n153 := randV 8 [n152, s1]
  let n154 := randV 8 [n153, s1]
  let n155 := randV 8 [n154, s1]
  let n156 := randV 8 [n155, s1]
  let n157 := randV 8 [n156, s1]
  let n158 := randV 8 [n157, s1]
  let n159 := randV 8 [n158, s1]
  let n160 := randV 8 [n159, s1]
  let n161 := randV 8 [n160, s1]
  let n162 := randV 8 [n161, s1]
  let n163 := randV 8 [n162, s1]
  let n164 := randV 8 [n163, s1]
  let n165 := randV 8 [n164, s1]
  let n166 := randV 8 [n165, s1]
  let n167 := randV 8 [n166, s1]
  let n168 := randV 8 [n167, s1]
  let n169 := randV 8 [n168, s1]
  let n170 := randV 8 [n169, s1]
  let n171 := randV 8 [n170, s1]
  let n172 := randV 8 [n171, s1]
  let n173 := randV 8 [n172, s1]
  let n174 := randV 8 [n173, s1]
  let n175 := randV 8 [n174, s1]
  let n176 := randV 8 [n175, s1]
  let n177 := randV 8 [n176, s1]
  let n178 := randV 8 [n177, s1]
  let n179 := randV 8 [n178, s1]
  let n180 := randV 8 [n179, s1]
  let n181 := randV 8 [n180, s1]
  let n182 := randV 8 [n181, s1]
  let n183 := randV 8 [n182, s1]
  let n184 := randV 8 [n183, s1]
  let n185 := randV 8 [n184, s1]
  let n186 := randV 8 [n185, s1]
  let n187 := randV 8 [n186, s1]
  let n188 := randV 8 [n187, s1]
  let n189 := randV 8 [n188, s1]
  let n190 := randV 8 [n189, s1]
  let n191 := randV 8 [n190, s1]
  let n192 := randV 8 [n191, s1]
  let n193 := randV 8 [n192, s1]
  let n194 := randV 8 [n193, s1]
  let n195 := randV 8 [n194, s1]
  let n196 := randV 8 [n195, s1]
  let n197 := randV 8 [n196, s1]
  let n198 := randV 8 [n197, s1]
  let n199 := randV 8 [n198, s1]
  let n200 := randV 8 [n199, s1]
  let n201 := randV 8 [n200, s1]
  let n202 := randV 8 [n201, s1]
  let n203 := randV 8 [n202, s1]
  let n204 := randV 8 [n203, s1]
  let n205 := randV 8 [n204, s1]
  let n206 := randV 8 [n205, s1]
  let n207 := randV 8 [n206, s1]
  let n208 := randV 8 [n207, s1]
  let n209 := randV 8 [n208, s1]
  let n210 := randV 8 [n209, s1]
  let n211 := randV 8 [n210, s1]
  let n212 := randV 8 [n211, s1]
  let n213 := randV 8 [n212, s1]
  let n214 := randV 8 [n213, s1]
  let n215 := randV 8 [n214, s1]
  let n216 := randV 8 [n215, s1]
  let n217 := randV 8 [n216, s1]
  let n218 := randV 8 [n217, s1]
  let n219 := randV 8 [n218, s1]
  let n220 := randV 8 [n219, s1]
  let n221 := randV 8 [n220, s1]
  let n222 := randV 8 [n221, s1]
  let n223 := randV 8 [n222, s1]
  let n224 := randV 8 [n223, s1]
  let n225 := randV 8 [n224, s1]
  let n226 := randV 8 [n225, s1]
  let n227 := randV 8 [n226, s1]
  let n228 := randV 8 [n227, s1]
  let n229 := randV 8 [n228, s1]
  let n230 := randV 8 [n229, s1]
  let n231 := randV 8 [n230, s1]
  let n232 := randV 8 [n231, s1]
  let n233 := randV 8 [n232, s1]
  let n234 := randV 8 [n233, s1]
  let n235 := randV 8 [n234, s1]
  let n236 := randV 8 [n235, s1]
  let n237 := randV 8 [n236, s1]
  let n238 := randV 8 [n237, s1]
  let n239 := randV 8 [n238, s1]
  let n240 := randV 8 [n239, s1]
  let n241 := randV 8 [n240, s1]
  let n242 := randV 8 [n241, s1]
  let n243 := randV 8 [n242, s1]
  let n244 := randV 8 [n243, s1]
  let n245 := randV 8 [n244, s1]
  let n246 := randV 8 [n245, s1]
  let n247 := randV 8 [n246, s1]
  let n248 := randV 8 [n247, s1]
  let n249 := randV 8 [n248, s1]
  let n250 := randV 8 [n249, s1]
  let n251 := randV 8 [n250, s1]
  let n252 := randV 8 [n251, s1]
  let n253 := randV 8 [n252, s1]
  let n254 := randV 8 [n253, s1]
  let n255 := randV 8 [n254, s1]
  { outputs := #[bv_resize 8 n255], nextState := { flops := #[], mems := #[] } }

def shallowB256 (i : RuntimeInput) (_s : RuntimeState) : RuntimeResult :=
  let s0 := bv_resize 8 (i[0]?.getD (mk_bv 8 0))
  let s1 := bv_resize 8 (i[1]?.getD (mk_bv 8 0))
  let n0 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 s0) s1
  let n1 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n0) s1
  let n2 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n1) s1
  let n3 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n2) s1
  let n4 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n3) s1
  let n5 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n4) s1
  let n6 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n5) s1
  let n7 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n6) s1
  let n8 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n7) s1
  let n9 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n8) s1
  let n10 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n9) s1
  let n11 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n10) s1
  let n12 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n11) s1
  let n13 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n12) s1
  let n14 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n13) s1
  let n15 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n14) s1
  let n16 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n15) s1
  let n17 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n16) s1
  let n18 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n17) s1
  let n19 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n18) s1
  let n20 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n19) s1
  let n21 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n20) s1
  let n22 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n21) s1
  let n23 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n22) s1
  let n24 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n23) s1
  let n25 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n24) s1
  let n26 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n25) s1
  let n27 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n26) s1
  let n28 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n27) s1
  let n29 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n28) s1
  let n30 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n29) s1
  let n31 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n30) s1
  let n32 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n31) s1
  let n33 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n32) s1
  let n34 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n33) s1
  let n35 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n34) s1
  let n36 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n35) s1
  let n37 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n36) s1
  let n38 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n37) s1
  let n39 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n38) s1
  let n40 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n39) s1
  let n41 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n40) s1
  let n42 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n41) s1
  let n43 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n42) s1
  let n44 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n43) s1
  let n45 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n44) s1
  let n46 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n45) s1
  let n47 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n46) s1
  let n48 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n47) s1
  let n49 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n48) s1
  let n50 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n49) s1
  let n51 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n50) s1
  let n52 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n51) s1
  let n53 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n52) s1
  let n54 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n53) s1
  let n55 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n54) s1
  let n56 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n55) s1
  let n57 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n56) s1
  let n58 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n57) s1
  let n59 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n58) s1
  let n60 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n59) s1
  let n61 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n60) s1
  let n62 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n61) s1
  let n63 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n62) s1
  let n64 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n63) s1
  let n65 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n64) s1
  let n66 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n65) s1
  let n67 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n66) s1
  let n68 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n67) s1
  let n69 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n68) s1
  let n70 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n69) s1
  let n71 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n70) s1
  let n72 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n71) s1
  let n73 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n72) s1
  let n74 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n73) s1
  let n75 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n74) s1
  let n76 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n75) s1
  let n77 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n76) s1
  let n78 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n77) s1
  let n79 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n78) s1
  let n80 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n79) s1
  let n81 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n80) s1
  let n82 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n81) s1
  let n83 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n82) s1
  let n84 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n83) s1
  let n85 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n84) s1
  let n86 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n85) s1
  let n87 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n86) s1
  let n88 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n87) s1
  let n89 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n88) s1
  let n90 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n89) s1
  let n91 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n90) s1
  let n92 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n91) s1
  let n93 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n92) s1
  let n94 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n93) s1
  let n95 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n94) s1
  let n96 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n95) s1
  let n97 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n96) s1
  let n98 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n97) s1
  let n99 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n98) s1
  let n100 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n99) s1
  let n101 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n100) s1
  let n102 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n101) s1
  let n103 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n102) s1
  let n104 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n103) s1
  let n105 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n104) s1
  let n106 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n105) s1
  let n107 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n106) s1
  let n108 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n107) s1
  let n109 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n108) s1
  let n110 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n109) s1
  let n111 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n110) s1
  let n112 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n111) s1
  let n113 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n112) s1
  let n114 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n113) s1
  let n115 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n114) s1
  let n116 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n115) s1
  let n117 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n116) s1
  let n118 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n117) s1
  let n119 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n118) s1
  let n120 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n119) s1
  let n121 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n120) s1
  let n122 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n121) s1
  let n123 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n122) s1
  let n124 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n123) s1
  let n125 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n124) s1
  let n126 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n125) s1
  let n127 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n126) s1
  let n128 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n127) s1
  let n129 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n128) s1
  let n130 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n129) s1
  let n131 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n130) s1
  let n132 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n131) s1
  let n133 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n132) s1
  let n134 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n133) s1
  let n135 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n134) s1
  let n136 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n135) s1
  let n137 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n136) s1
  let n138 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n137) s1
  let n139 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n138) s1
  let n140 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n139) s1
  let n141 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n140) s1
  let n142 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n141) s1
  let n143 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n142) s1
  let n144 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n143) s1
  let n145 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n144) s1
  let n146 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n145) s1
  let n147 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n146) s1
  let n148 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n147) s1
  let n149 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n148) s1
  let n150 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n149) s1
  let n151 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n150) s1
  let n152 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n151) s1
  let n153 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n152) s1
  let n154 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n153) s1
  let n155 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n154) s1
  let n156 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n155) s1
  let n157 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n156) s1
  let n158 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n157) s1
  let n159 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n158) s1
  let n160 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n159) s1
  let n161 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n160) s1
  let n162 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n161) s1
  let n163 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n162) s1
  let n164 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n163) s1
  let n165 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n164) s1
  let n166 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n165) s1
  let n167 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n166) s1
  let n168 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n167) s1
  let n169 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n168) s1
  let n170 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n169) s1
  let n171 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n170) s1
  let n172 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n171) s1
  let n173 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n172) s1
  let n174 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n173) s1
  let n175 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n174) s1
  let n176 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n175) s1
  let n177 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n176) s1
  let n178 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n177) s1
  let n179 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n178) s1
  let n180 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n179) s1
  let n181 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n180) s1
  let n182 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n181) s1
  let n183 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n182) s1
  let n184 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n183) s1
  let n185 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n184) s1
  let n186 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n185) s1
  let n187 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n186) s1
  let n188 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n187) s1
  let n189 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n188) s1
  let n190 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n189) s1
  let n191 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n190) s1
  let n192 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n191) s1
  let n193 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n192) s1
  let n194 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n193) s1
  let n195 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n194) s1
  let n196 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n195) s1
  let n197 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n196) s1
  let n198 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n197) s1
  let n199 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n198) s1
  let n200 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n199) s1
  let n201 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n200) s1
  let n202 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n201) s1
  let n203 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n202) s1
  let n204 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n203) s1
  let n205 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n204) s1
  let n206 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n205) s1
  let n207 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n206) s1
  let n208 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n207) s1
  let n209 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n208) s1
  let n210 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n209) s1
  let n211 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n210) s1
  let n212 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n211) s1
  let n213 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n212) s1
  let n214 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n213) s1
  let n215 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n214) s1
  let n216 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n215) s1
  let n217 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n216) s1
  let n218 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n217) s1
  let n219 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n218) s1
  let n220 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n219) s1
  let n221 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n220) s1
  let n222 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n221) s1
  let n223 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n222) s1
  let n224 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n223) s1
  let n225 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n224) s1
  let n226 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n225) s1
  let n227 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n226) s1
  let n228 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n227) s1
  let n229 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n228) s1
  let n230 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n229) s1
  let n231 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n230) s1
  let n232 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n231) s1
  let n233 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n232) s1
  let n234 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n233) s1
  let n235 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n234) s1
  let n236 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n235) s1
  let n237 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n236) s1
  let n238 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n237) s1
  let n239 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n238) s1
  let n240 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n239) s1
  let n241 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n240) s1
  let n242 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n241) s1
  let n243 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n242) s1
  let n244 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n243) s1
  let n245 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n244) s1
  let n246 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n245) s1
  let n247 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n246) s1
  let n248 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n247) s1
  let n249 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n248) s1
  let n250 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n249) s1
  let n251 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n250) s1
  let n252 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n251) s1
  let n253 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n252) s1
  let n254 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n253) s1
  let n255 := bv_bitwise 8 (fun x y => x && y) (bv_resize 8 n254) s1
  { outputs := #[bv_resize 8 n255], nextState := { flops := #[], mems := #[] } }

def stz : RuntimeState := ⟨#[], #[]⟩
def benchD (nm : String) (n it : Nat) (f : RuntimeInput → RuntimeState → RuntimeResult) : IO Unit := do
  let t0 ← IO.monoMsNow
  let mut acc : Int := 0
  for k in [0:it] do
    acc := acc + ((f #[mk_bv 8 (Int.ofNat k), mk_bv 8 1] stz).outputs[0]!.value)
  let t1 ← IO.monoMsNow
  IO.println s!"{nm}\tn={n}\tms={t1-t0}\tus_per_node={((t1-t0)*1000)/(it*n)}\tacc={acc}"
def main : IO Unit := do
  benchD "deep    " 64 20000 (denoteResidual R64)
  benchD "shallowA" 64 20000 shallowA64
  benchD "shallowB" 64 20000 shallowB64
  benchD "deep    " 256 20000 (denoteResidual R256)
  benchD "shallowA" 256 20000 shallowA256
  benchD "shallowB" 256 20000 shallowB256
#eval main
