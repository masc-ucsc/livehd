import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler Compiler.Residual

namespace Compiler
theorem runBindings_step (b : ResidualBinding) (bs : List ResidualBinding)
    (env : SlotEnv) (v : CertVal) (hv : denoteExpr env b.rhs = v) :
    runBindings (b :: bs) env = runBindings bs (env.push v) := by
  simp only [runBindings, hv]

theorem refBV_push_lt (env : SlotEnv) (v : CertVal) (j : Nat) (h : j < env.size) :
    refBV (env.push v) j = refBV env j := by
  simp only [refBV, denoteRef, Array.getElem?_push_lt h, Array.getElem?_eq_getElem h,
    Option.getD_some]

theorem refBV_push_self (env : SlotEnv) (v : CertVal) :
    refBV (env.push v) env.size = v.asBV := by
  simp only [refBV, denoteRef, Array.getElem?_push_size, Option.getD_some]

theorem srcAgree_push {env base : SlotEnv} (v : CertVal)
    (hsz : base.size ≤ env.size)
    (h : ∀ j, j < base.size → refBV env j = refBV base j) :
    ∀ j, j < base.size → refBV (env.push v) j = refBV base j := by
  intro j hj
  rw [refBV_push_lt env v j (Nat.lt_of_lt_of_le hj hsz)]
  exact h j hj
end Compiler

def R : ResidualProgram :=
  { sources  := #[SourceDesc.input 0 8, SourceDesc.input 1 8]
    bindings := #[
      { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[0, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[2, 2] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[3, 3] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[4, 3] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[5, 4] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[6, 4] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[7, 5] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[8, 5] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[9, 6] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[10, 6] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[11, 7] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[12, 7] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[13, 8] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[14, 8] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[15, 9] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[16, 9] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[17, 10] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[18, 10] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[19, 11] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[20, 11] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[21, 12] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[22, 12] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[23, 13] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[24, 13] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[25, 14] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[26, 14] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[27, 15] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[28, 15] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[29, 16] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[30, 16] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[31, 17] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[32, 17] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[33, 18] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[34, 18] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[35, 19] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[36, 19] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[37, 20] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[38, 20] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[39, 21] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[40, 21] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[41, 22] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[42, 22] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[43, 23] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[44, 23] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[45, 24] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[46, 24] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[47, 25] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[48, 25] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[49, 26] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[50, 26] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[51, 27] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[52, 27] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[53, 28] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[54, 28] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[55, 29] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[56, 29] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[57, 30] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[58, 30] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[59, 31] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[60, 31] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[61, 32] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[62, 32] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[63, 33] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[64, 33] }
    ]
    outputs  := #[{ slot := 65, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

def fast (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let s0 := bv_resize 8 (i[0]?.getD (mk_bv 8 0))
  let s1 := bv_resize 8 (i[1]?.getD (mk_bv 8 0))
  let v0 := randV 8 [s0, s1]
  let v1 := randV 8 [v0, v0]
  let v2 := randV 8 [v1, v1]
  let v3 := randV 8 [v2, v1]
  let v4 := randV 8 [v3, v2]
  let v5 := randV 8 [v4, v2]
  let v6 := randV 8 [v5, v3]
  let v7 := randV 8 [v6, v3]
  let v8 := randV 8 [v7, v4]
  let v9 := randV 8 [v8, v4]
  let v10 := randV 8 [v9, v5]
  let v11 := randV 8 [v10, v5]
  let v12 := randV 8 [v11, v6]
  let v13 := randV 8 [v12, v6]
  let v14 := randV 8 [v13, v7]
  let v15 := randV 8 [v14, v7]
  let v16 := randV 8 [v15, v8]
  let v17 := randV 8 [v16, v8]
  let v18 := randV 8 [v17, v9]
  let v19 := randV 8 [v18, v9]
  let v20 := randV 8 [v19, v10]
  let v21 := randV 8 [v20, v10]
  let v22 := randV 8 [v21, v11]
  let v23 := randV 8 [v22, v11]
  let v24 := randV 8 [v23, v12]
  let v25 := randV 8 [v24, v12]
  let v26 := randV 8 [v25, v13]
  let v27 := randV 8 [v26, v13]
  let v28 := randV 8 [v27, v14]
  let v29 := randV 8 [v28, v14]
  let v30 := randV 8 [v29, v15]
  let v31 := randV 8 [v30, v15]
  let v32 := randV 8 [v31, v16]
  let v33 := randV 8 [v32, v16]
  let v34 := randV 8 [v33, v17]
  let v35 := randV 8 [v34, v17]
  let v36 := randV 8 [v35, v18]
  let v37 := randV 8 [v36, v18]
  let v38 := randV 8 [v37, v19]
  let v39 := randV 8 [v38, v19]
  let v40 := randV 8 [v39, v20]
  let v41 := randV 8 [v40, v20]
  let v42 := randV 8 [v41, v21]
  let v43 := randV 8 [v42, v21]
  let v44 := randV 8 [v43, v22]
  let v45 := randV 8 [v44, v22]
  let v46 := randV 8 [v45, v23]
  let v47 := randV 8 [v46, v23]
  let v48 := randV 8 [v47, v24]
  let v49 := randV 8 [v48, v24]
  let v50 := randV 8 [v49, v25]
  let v51 := randV 8 [v50, v25]
  let v52 := randV 8 [v51, v26]
  let v53 := randV 8 [v52, v26]
  let v54 := randV 8 [v53, v27]
  let v55 := randV 8 [v54, v27]
  let v56 := randV 8 [v55, v28]
  let v57 := randV 8 [v56, v28]
  let v58 := randV 8 [v57, v29]
  let v59 := randV 8 [v58, v29]
  let v60 := randV 8 [v59, v30]
  let v61 := randV 8 [v60, v30]
  let v62 := randV 8 [v61, v31]
  let v63 := randV 8 [v62, v31]
  { outputs := #[bv_resize 8 v63]
    nextState := { flops := #[], mems := #[] } }

theorem fast_correct : ∀ e i s, fast i s = denoteResidual R e i s := by
  intro e i s
  set s0 : BV := bv_resize 8 (i[0]?.getD (mk_bv 8 0)) with hsd0
  set s1 : BV := bv_resize 8 (i[1]?.getD (mk_bv 8 0)) with hsd1
  set v0 : BV := randV 8 [s0, s1] with hvd0
  set v1 : BV := randV 8 [v0, v0] with hvd1
  set v2 : BV := randV 8 [v1, v1] with hvd2
  set v3 : BV := randV 8 [v2, v1] with hvd3
  set v4 : BV := randV 8 [v3, v2] with hvd4
  set v5 : BV := randV 8 [v4, v2] with hvd5
  set v6 : BV := randV 8 [v5, v3] with hvd6
  set v7 : BV := randV 8 [v6, v3] with hvd7
  set v8 : BV := randV 8 [v7, v4] with hvd8
  set v9 : BV := randV 8 [v8, v4] with hvd9
  set v10 : BV := randV 8 [v9, v5] with hvd10
  set v11 : BV := randV 8 [v10, v5] with hvd11
  set v12 : BV := randV 8 [v11, v6] with hvd12
  set v13 : BV := randV 8 [v12, v6] with hvd13
  set v14 : BV := randV 8 [v13, v7] with hvd14
  set v15 : BV := randV 8 [v14, v7] with hvd15
  set v16 : BV := randV 8 [v15, v8] with hvd16
  set v17 : BV := randV 8 [v16, v8] with hvd17
  set v18 : BV := randV 8 [v17, v9] with hvd18
  set v19 : BV := randV 8 [v18, v9] with hvd19
  set v20 : BV := randV 8 [v19, v10] with hvd20
  set v21 : BV := randV 8 [v20, v10] with hvd21
  set v22 : BV := randV 8 [v21, v11] with hvd22
  set v23 : BV := randV 8 [v22, v11] with hvd23
  set v24 : BV := randV 8 [v23, v12] with hvd24
  set v25 : BV := randV 8 [v24, v12] with hvd25
  set v26 : BV := randV 8 [v25, v13] with hvd26
  set v27 : BV := randV 8 [v26, v13] with hvd27
  set v28 : BV := randV 8 [v27, v14] with hvd28
  set v29 : BV := randV 8 [v28, v14] with hvd29
  set v30 : BV := randV 8 [v29, v15] with hvd30
  set v31 : BV := randV 8 [v30, v15] with hvd31
  set v32 : BV := randV 8 [v31, v16] with hvd32
  set v33 : BV := randV 8 [v32, v16] with hvd33
  set v34 : BV := randV 8 [v33, v17] with hvd34
  set v35 : BV := randV 8 [v34, v17] with hvd35
  set v36 : BV := randV 8 [v35, v18] with hvd36
  set v37 : BV := randV 8 [v36, v18] with hvd37
  set v38 : BV := randV 8 [v37, v19] with hvd38
  set v39 : BV := randV 8 [v38, v19] with hvd39
  set v40 : BV := randV 8 [v39, v20] with hvd40
  set v41 : BV := randV 8 [v40, v20] with hvd41
  set v42 : BV := randV 8 [v41, v21] with hvd42
  set v43 : BV := randV 8 [v42, v21] with hvd43
  set v44 : BV := randV 8 [v43, v22] with hvd44
  set v45 : BV := randV 8 [v44, v22] with hvd45
  set v46 : BV := randV 8 [v45, v23] with hvd46
  set v47 : BV := randV 8 [v46, v23] with hvd47
  set v48 : BV := randV 8 [v47, v24] with hvd48
  set v49 : BV := randV 8 [v48, v24] with hvd49
  set v50 : BV := randV 8 [v49, v25] with hvd50
  set v51 : BV := randV 8 [v50, v25] with hvd51
  set v52 : BV := randV 8 [v51, v26] with hvd52
  set v53 : BV := randV 8 [v52, v26] with hvd53
  set v54 : BV := randV 8 [v53, v27] with hvd54
  set v55 : BV := randV 8 [v54, v27] with hvd55
  set v56 : BV := randV 8 [v55, v28] with hvd56
  set v57 : BV := randV 8 [v56, v28] with hvd57
  set v58 : BV := randV 8 [v57, v29] with hvd58
  set v59 : BV := randV 8 [v58, v29] with hvd59
  set v60 : BV := randV 8 [v59, v30] with hvd60
  set v61 : BV := randV 8 [v60, v30] with hvd61
  set v62 : BV := randV 8 [v61, v31] with hvd62
  set v63 : BV := randV 8 [v62, v31] with hvd63
  simp only [denoteResidual, R]
  set E0 : SlotEnv := sourceEnvArr (#[SourceDesc.input 0 8, SourceDesc.input 1 8]) i s with hE0
  have hs0 : E0.size = 2 := by simp [hE0, sourceEnvArr]
  have hb0 : refBV E0 0 = s0 := by simp [hE0, hsd0, sourceEnvArr, refBV, denoteRef, sourceValue, CertVal.asBV]
  have hb1 : refBV E0 1 = s1 := by simp [hE0, hsd1, sourceEnvArr, refBV, denoteRef, sourceValue, CertVal.asBV]
  have hag0 : ∀ j, j < E0.size → refBV E0 j = refBV E0 j := fun _ _ => rfl
  have ra0 : refBV E0 0 = s0 := by
    rw [hag0 0 (by rw [hs0]; omega), hb0]
  have rb0 : refBV E0 1 = s1 := by
    rw [hag0 1 (by rw [hs0]; omega), hb1]
  have hv0 : denoteExpr E0 (ResidualExpr.rand 8 #[0, 1]) = CertVal.bv v0 := by
    have hu : denoteExpr E0 (ResidualExpr.rand 8 #[0, 1]) = CertVal.bv (randV 8 [refBV E0 0, refBV E0 1]) := rfl
    rw [hu, ra0, rb0]
  rw [runBindings_step _ _ _ _ hv0]
  set E1 : SlotEnv := E0.push (CertVal.bv v0) with hE1
  have hs1 : E1.size = 3 := by rw [hE1, Array.size_push, hs0]
  have hag1 : ∀ j, j < E0.size → refBV E1 j = refBV E0 j :=
    fun j hj => by rw [hE1]; exact srcAgree_push _ (by omega) hag0 j hj
  clear ra0 rb0 hv0 hag0
  have ra1 : refBV E1 2 = v0 := by
    rw [hE1, show (2 : Nat) = E0.size from (hs0).symm, refBV_push_self]
    rfl
  have hv1 : denoteExpr E1 (ResidualExpr.rand 8 #[2, 2]) = CertVal.bv v1 := by
    have hu : denoteExpr E1 (ResidualExpr.rand 8 #[2, 2]) = CertVal.bv (randV 8 [refBV E1 2, refBV E1 2]) := rfl
    rw [hu, ra1]
  rw [runBindings_step _ _ _ _ hv1]
  set E2 : SlotEnv := E1.push (CertVal.bv v1) with hE2
  have hs2 : E2.size = 4 := by rw [hE2, Array.size_push, hs1]
  have hag2 : ∀ j, j < E0.size → refBV E2 j = refBV E0 j :=
    fun j hj => by rw [hE2]; exact srcAgree_push _ (by omega) hag1 j hj
  clear ra1 hv1 hag1
  have ra2 : refBV E2 3 = v1 := by
    rw [hE2, show (3 : Nat) = E1.size from (hs1).symm, refBV_push_self]
    rfl
  have hv2 : denoteExpr E2 (ResidualExpr.rand 8 #[3, 3]) = CertVal.bv v2 := by
    have hu : denoteExpr E2 (ResidualExpr.rand 8 #[3, 3]) = CertVal.bv (randV 8 [refBV E2 3, refBV E2 3]) := rfl
    rw [hu, ra2]
  rw [runBindings_step _ _ _ _ hv2]
  set E3 : SlotEnv := E2.push (CertVal.bv v2) with hE3
  have hs3 : E3.size = 5 := by rw [hE3, Array.size_push, hs2]
  have hag3 : ∀ j, j < E0.size → refBV E3 j = refBV E0 j :=
    fun j hj => by rw [hE3]; exact srcAgree_push _ (by omega) hag2 j hj
  clear ra2 hv2 hag2
  have ra3 : refBV E3 4 = v2 := by
    rw [hE3, show (4 : Nat) = E2.size from (hs2).symm, refBV_push_self]
    rfl
  have rb3 : refBV E3 3 = v1 := by
    rw [hE3, refBV_push_lt _ _ _ (by rw [hs2]; omega)]
    rw [hE2, show (3 : Nat) = E1.size from (hs1).symm, refBV_push_self]
    rfl
  have hv3 : denoteExpr E3 (ResidualExpr.rand 8 #[4, 3]) = CertVal.bv v3 := by
    have hu : denoteExpr E3 (ResidualExpr.rand 8 #[4, 3]) = CertVal.bv (randV 8 [refBV E3 4, refBV E3 3]) := rfl
    rw [hu, ra3, rb3]
  rw [runBindings_step _ _ _ _ hv3]
  set E4 : SlotEnv := E3.push (CertVal.bv v3) with hE4
  have hs4 : E4.size = 6 := by rw [hE4, Array.size_push, hs3]
  have hag4 : ∀ j, j < E0.size → refBV E4 j = refBV E0 j :=
    fun j hj => by rw [hE4]; exact srcAgree_push _ (by omega) hag3 j hj
  clear ra3 rb3 hv3 hag3
  have ra4 : refBV E4 5 = v3 := by
    rw [hE4, show (5 : Nat) = E3.size from (hs3).symm, refBV_push_self]
    rfl
  have rb4 : refBV E4 4 = v2 := by
    rw [hE4, refBV_push_lt _ _ _ (by rw [hs3]; omega)]
    rw [hE3, show (4 : Nat) = E2.size from (hs2).symm, refBV_push_self]
    rfl
  have hv4 : denoteExpr E4 (ResidualExpr.rand 8 #[5, 4]) = CertVal.bv v4 := by
    have hu : denoteExpr E4 (ResidualExpr.rand 8 #[5, 4]) = CertVal.bv (randV 8 [refBV E4 5, refBV E4 4]) := rfl
    rw [hu, ra4, rb4]
  rw [runBindings_step _ _ _ _ hv4]
  set E5 : SlotEnv := E4.push (CertVal.bv v4) with hE5
  have hs5 : E5.size = 7 := by rw [hE5, Array.size_push, hs4]
  have hag5 : ∀ j, j < E0.size → refBV E5 j = refBV E0 j :=
    fun j hj => by rw [hE5]; exact srcAgree_push _ (by omega) hag4 j hj
  clear ra4 rb4 hv4 hag4
  have ra5 : refBV E5 6 = v4 := by
    rw [hE5, show (6 : Nat) = E4.size from (hs4).symm, refBV_push_self]
    rfl
  have rb5 : refBV E5 4 = v2 := by
    rw [hE5, refBV_push_lt _ _ _ (by rw [hs4]; omega)]
    rw [hE4, refBV_push_lt _ _ _ (by rw [hs3]; omega)]
    rw [hE3, show (4 : Nat) = E2.size from (hs2).symm, refBV_push_self]
    rfl
  have hv5 : denoteExpr E5 (ResidualExpr.rand 8 #[6, 4]) = CertVal.bv v5 := by
    have hu : denoteExpr E5 (ResidualExpr.rand 8 #[6, 4]) = CertVal.bv (randV 8 [refBV E5 6, refBV E5 4]) := rfl
    rw [hu, ra5, rb5]
  rw [runBindings_step _ _ _ _ hv5]
  set E6 : SlotEnv := E5.push (CertVal.bv v5) with hE6
  have hs6 : E6.size = 8 := by rw [hE6, Array.size_push, hs5]
  have hag6 : ∀ j, j < E0.size → refBV E6 j = refBV E0 j :=
    fun j hj => by rw [hE6]; exact srcAgree_push _ (by omega) hag5 j hj
  clear ra5 rb5 hv5 hag5
  have ra6 : refBV E6 7 = v5 := by
    rw [hE6, show (7 : Nat) = E5.size from (hs5).symm, refBV_push_self]
    rfl
  have rb6 : refBV E6 5 = v3 := by
    rw [hE6, refBV_push_lt _ _ _ (by rw [hs5]; omega)]
    rw [hE5, refBV_push_lt _ _ _ (by rw [hs4]; omega)]
    rw [hE4, show (5 : Nat) = E3.size from (hs3).symm, refBV_push_self]
    rfl
  have hv6 : denoteExpr E6 (ResidualExpr.rand 8 #[7, 5]) = CertVal.bv v6 := by
    have hu : denoteExpr E6 (ResidualExpr.rand 8 #[7, 5]) = CertVal.bv (randV 8 [refBV E6 7, refBV E6 5]) := rfl
    rw [hu, ra6, rb6]
  rw [runBindings_step _ _ _ _ hv6]
  set E7 : SlotEnv := E6.push (CertVal.bv v6) with hE7
  have hs7 : E7.size = 9 := by rw [hE7, Array.size_push, hs6]
  have hag7 : ∀ j, j < E0.size → refBV E7 j = refBV E0 j :=
    fun j hj => by rw [hE7]; exact srcAgree_push _ (by omega) hag6 j hj
  clear ra6 rb6 hv6 hag6
  have ra7 : refBV E7 8 = v6 := by
    rw [hE7, show (8 : Nat) = E6.size from (hs6).symm, refBV_push_self]
    rfl
  have rb7 : refBV E7 5 = v3 := by
    rw [hE7, refBV_push_lt _ _ _ (by rw [hs6]; omega)]
    rw [hE6, refBV_push_lt _ _ _ (by rw [hs5]; omega)]
    rw [hE5, refBV_push_lt _ _ _ (by rw [hs4]; omega)]
    rw [hE4, show (5 : Nat) = E3.size from (hs3).symm, refBV_push_self]
    rfl
  have hv7 : denoteExpr E7 (ResidualExpr.rand 8 #[8, 5]) = CertVal.bv v7 := by
    have hu : denoteExpr E7 (ResidualExpr.rand 8 #[8, 5]) = CertVal.bv (randV 8 [refBV E7 8, refBV E7 5]) := rfl
    rw [hu, ra7, rb7]
  rw [runBindings_step _ _ _ _ hv7]
  set E8 : SlotEnv := E7.push (CertVal.bv v7) with hE8
  have hs8 : E8.size = 10 := by rw [hE8, Array.size_push, hs7]
  have hag8 : ∀ j, j < E0.size → refBV E8 j = refBV E0 j :=
    fun j hj => by rw [hE8]; exact srcAgree_push _ (by omega) hag7 j hj
  clear ra7 rb7 hv7 hag7
  have ra8 : refBV E8 9 = v7 := by
    rw [hE8, show (9 : Nat) = E7.size from (hs7).symm, refBV_push_self]
    rfl
  have rb8 : refBV E8 6 = v4 := by
    rw [hE8, refBV_push_lt _ _ _ (by rw [hs7]; omega)]
    rw [hE7, refBV_push_lt _ _ _ (by rw [hs6]; omega)]
    rw [hE6, refBV_push_lt _ _ _ (by rw [hs5]; omega)]
    rw [hE5, show (6 : Nat) = E4.size from (hs4).symm, refBV_push_self]
    rfl
  have hv8 : denoteExpr E8 (ResidualExpr.rand 8 #[9, 6]) = CertVal.bv v8 := by
    have hu : denoteExpr E8 (ResidualExpr.rand 8 #[9, 6]) = CertVal.bv (randV 8 [refBV E8 9, refBV E8 6]) := rfl
    rw [hu, ra8, rb8]
  rw [runBindings_step _ _ _ _ hv8]
  set E9 : SlotEnv := E8.push (CertVal.bv v8) with hE9
  have hs9 : E9.size = 11 := by rw [hE9, Array.size_push, hs8]
  have hag9 : ∀ j, j < E0.size → refBV E9 j = refBV E0 j :=
    fun j hj => by rw [hE9]; exact srcAgree_push _ (by omega) hag8 j hj
  clear ra8 rb8 hv8 hag8
  have ra9 : refBV E9 10 = v8 := by
    rw [hE9, show (10 : Nat) = E8.size from (hs8).symm, refBV_push_self]
    rfl
  have rb9 : refBV E9 6 = v4 := by
    rw [hE9, refBV_push_lt _ _ _ (by rw [hs8]; omega)]
    rw [hE8, refBV_push_lt _ _ _ (by rw [hs7]; omega)]
    rw [hE7, refBV_push_lt _ _ _ (by rw [hs6]; omega)]
    rw [hE6, refBV_push_lt _ _ _ (by rw [hs5]; omega)]
    rw [hE5, show (6 : Nat) = E4.size from (hs4).symm, refBV_push_self]
    rfl
  have hv9 : denoteExpr E9 (ResidualExpr.rand 8 #[10, 6]) = CertVal.bv v9 := by
    have hu : denoteExpr E9 (ResidualExpr.rand 8 #[10, 6]) = CertVal.bv (randV 8 [refBV E9 10, refBV E9 6]) := rfl
    rw [hu, ra9, rb9]
  rw [runBindings_step _ _ _ _ hv9]
  set E10 : SlotEnv := E9.push (CertVal.bv v9) with hE10
  have hs10 : E10.size = 12 := by rw [hE10, Array.size_push, hs9]
  have hag10 : ∀ j, j < E0.size → refBV E10 j = refBV E0 j :=
    fun j hj => by rw [hE10]; exact srcAgree_push _ (by omega) hag9 j hj
  clear ra9 rb9 hv9 hag9
  have ra10 : refBV E10 11 = v9 := by
    rw [hE10, show (11 : Nat) = E9.size from (hs9).symm, refBV_push_self]
    rfl
  have rb10 : refBV E10 7 = v5 := by
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, refBV_push_lt _ _ _ (by rw [hs8]; omega)]
    rw [hE8, refBV_push_lt _ _ _ (by rw [hs7]; omega)]
    rw [hE7, refBV_push_lt _ _ _ (by rw [hs6]; omega)]
    rw [hE6, show (7 : Nat) = E5.size from (hs5).symm, refBV_push_self]
    rfl
  have hv10 : denoteExpr E10 (ResidualExpr.rand 8 #[11, 7]) = CertVal.bv v10 := by
    have hu : denoteExpr E10 (ResidualExpr.rand 8 #[11, 7]) = CertVal.bv (randV 8 [refBV E10 11, refBV E10 7]) := rfl
    rw [hu, ra10, rb10]
  rw [runBindings_step _ _ _ _ hv10]
  set E11 : SlotEnv := E10.push (CertVal.bv v10) with hE11
  have hs11 : E11.size = 13 := by rw [hE11, Array.size_push, hs10]
  have hag11 : ∀ j, j < E0.size → refBV E11 j = refBV E0 j :=
    fun j hj => by rw [hE11]; exact srcAgree_push _ (by omega) hag10 j hj
  clear ra10 rb10 hv10 hag10
  have ra11 : refBV E11 12 = v10 := by
    rw [hE11, show (12 : Nat) = E10.size from (hs10).symm, refBV_push_self]
    rfl
  have rb11 : refBV E11 7 = v5 := by
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, refBV_push_lt _ _ _ (by rw [hs8]; omega)]
    rw [hE8, refBV_push_lt _ _ _ (by rw [hs7]; omega)]
    rw [hE7, refBV_push_lt _ _ _ (by rw [hs6]; omega)]
    rw [hE6, show (7 : Nat) = E5.size from (hs5).symm, refBV_push_self]
    rfl
  have hv11 : denoteExpr E11 (ResidualExpr.rand 8 #[12, 7]) = CertVal.bv v11 := by
    have hu : denoteExpr E11 (ResidualExpr.rand 8 #[12, 7]) = CertVal.bv (randV 8 [refBV E11 12, refBV E11 7]) := rfl
    rw [hu, ra11, rb11]
  rw [runBindings_step _ _ _ _ hv11]
  set E12 : SlotEnv := E11.push (CertVal.bv v11) with hE12
  have hs12 : E12.size = 14 := by rw [hE12, Array.size_push, hs11]
  have hag12 : ∀ j, j < E0.size → refBV E12 j = refBV E0 j :=
    fun j hj => by rw [hE12]; exact srcAgree_push _ (by omega) hag11 j hj
  clear ra11 rb11 hv11 hag11
  have ra12 : refBV E12 13 = v11 := by
    rw [hE12, show (13 : Nat) = E11.size from (hs11).symm, refBV_push_self]
    rfl
  have rb12 : refBV E12 8 = v6 := by
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, refBV_push_lt _ _ _ (by rw [hs8]; omega)]
    rw [hE8, refBV_push_lt _ _ _ (by rw [hs7]; omega)]
    rw [hE7, show (8 : Nat) = E6.size from (hs6).symm, refBV_push_self]
    rfl
  have hv12 : denoteExpr E12 (ResidualExpr.rand 8 #[13, 8]) = CertVal.bv v12 := by
    have hu : denoteExpr E12 (ResidualExpr.rand 8 #[13, 8]) = CertVal.bv (randV 8 [refBV E12 13, refBV E12 8]) := rfl
    rw [hu, ra12, rb12]
  rw [runBindings_step _ _ _ _ hv12]
  set E13 : SlotEnv := E12.push (CertVal.bv v12) with hE13
  have hs13 : E13.size = 15 := by rw [hE13, Array.size_push, hs12]
  have hag13 : ∀ j, j < E0.size → refBV E13 j = refBV E0 j :=
    fun j hj => by rw [hE13]; exact srcAgree_push _ (by omega) hag12 j hj
  clear ra12 rb12 hv12 hag12
  have ra13 : refBV E13 14 = v12 := by
    rw [hE13, show (14 : Nat) = E12.size from (hs12).symm, refBV_push_self]
    rfl
  have rb13 : refBV E13 8 = v6 := by
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, refBV_push_lt _ _ _ (by rw [hs8]; omega)]
    rw [hE8, refBV_push_lt _ _ _ (by rw [hs7]; omega)]
    rw [hE7, show (8 : Nat) = E6.size from (hs6).symm, refBV_push_self]
    rfl
  have hv13 : denoteExpr E13 (ResidualExpr.rand 8 #[14, 8]) = CertVal.bv v13 := by
    have hu : denoteExpr E13 (ResidualExpr.rand 8 #[14, 8]) = CertVal.bv (randV 8 [refBV E13 14, refBV E13 8]) := rfl
    rw [hu, ra13, rb13]
  rw [runBindings_step _ _ _ _ hv13]
  set E14 : SlotEnv := E13.push (CertVal.bv v13) with hE14
  have hs14 : E14.size = 16 := by rw [hE14, Array.size_push, hs13]
  have hag14 : ∀ j, j < E0.size → refBV E14 j = refBV E0 j :=
    fun j hj => by rw [hE14]; exact srcAgree_push _ (by omega) hag13 j hj
  clear ra13 rb13 hv13 hag13
  have ra14 : refBV E14 15 = v13 := by
    rw [hE14, show (15 : Nat) = E13.size from (hs13).symm, refBV_push_self]
    rfl
  have rb14 : refBV E14 9 = v7 := by
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, refBV_push_lt _ _ _ (by rw [hs8]; omega)]
    rw [hE8, show (9 : Nat) = E7.size from (hs7).symm, refBV_push_self]
    rfl
  have hv14 : denoteExpr E14 (ResidualExpr.rand 8 #[15, 9]) = CertVal.bv v14 := by
    have hu : denoteExpr E14 (ResidualExpr.rand 8 #[15, 9]) = CertVal.bv (randV 8 [refBV E14 15, refBV E14 9]) := rfl
    rw [hu, ra14, rb14]
  rw [runBindings_step _ _ _ _ hv14]
  set E15 : SlotEnv := E14.push (CertVal.bv v14) with hE15
  have hs15 : E15.size = 17 := by rw [hE15, Array.size_push, hs14]
  have hag15 : ∀ j, j < E0.size → refBV E15 j = refBV E0 j :=
    fun j hj => by rw [hE15]; exact srcAgree_push _ (by omega) hag14 j hj
  clear ra14 rb14 hv14 hag14
  have ra15 : refBV E15 16 = v14 := by
    rw [hE15, show (16 : Nat) = E14.size from (hs14).symm, refBV_push_self]
    rfl
  have rb15 : refBV E15 9 = v7 := by
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, refBV_push_lt _ _ _ (by rw [hs8]; omega)]
    rw [hE8, show (9 : Nat) = E7.size from (hs7).symm, refBV_push_self]
    rfl
  have hv15 : denoteExpr E15 (ResidualExpr.rand 8 #[16, 9]) = CertVal.bv v15 := by
    have hu : denoteExpr E15 (ResidualExpr.rand 8 #[16, 9]) = CertVal.bv (randV 8 [refBV E15 16, refBV E15 9]) := rfl
    rw [hu, ra15, rb15]
  rw [runBindings_step _ _ _ _ hv15]
  set E16 : SlotEnv := E15.push (CertVal.bv v15) with hE16
  have hs16 : E16.size = 18 := by rw [hE16, Array.size_push, hs15]
  have hag16 : ∀ j, j < E0.size → refBV E16 j = refBV E0 j :=
    fun j hj => by rw [hE16]; exact srcAgree_push _ (by omega) hag15 j hj
  clear ra15 rb15 hv15 hag15
  have ra16 : refBV E16 17 = v15 := by
    rw [hE16, show (17 : Nat) = E15.size from (hs15).symm, refBV_push_self]
    rfl
  have rb16 : refBV E16 10 = v8 := by
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, show (10 : Nat) = E8.size from (hs8).symm, refBV_push_self]
    rfl
  have hv16 : denoteExpr E16 (ResidualExpr.rand 8 #[17, 10]) = CertVal.bv v16 := by
    have hu : denoteExpr E16 (ResidualExpr.rand 8 #[17, 10]) = CertVal.bv (randV 8 [refBV E16 17, refBV E16 10]) := rfl
    rw [hu, ra16, rb16]
  rw [runBindings_step _ _ _ _ hv16]
  set E17 : SlotEnv := E16.push (CertVal.bv v16) with hE17
  have hs17 : E17.size = 19 := by rw [hE17, Array.size_push, hs16]
  have hag17 : ∀ j, j < E0.size → refBV E17 j = refBV E0 j :=
    fun j hj => by rw [hE17]; exact srcAgree_push _ (by omega) hag16 j hj
  clear ra16 rb16 hv16 hag16
  have ra17 : refBV E17 18 = v16 := by
    rw [hE17, show (18 : Nat) = E16.size from (hs16).symm, refBV_push_self]
    rfl
  have rb17 : refBV E17 10 = v8 := by
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, refBV_push_lt _ _ _ (by rw [hs9]; omega)]
    rw [hE9, show (10 : Nat) = E8.size from (hs8).symm, refBV_push_self]
    rfl
  have hv17 : denoteExpr E17 (ResidualExpr.rand 8 #[18, 10]) = CertVal.bv v17 := by
    have hu : denoteExpr E17 (ResidualExpr.rand 8 #[18, 10]) = CertVal.bv (randV 8 [refBV E17 18, refBV E17 10]) := rfl
    rw [hu, ra17, rb17]
  rw [runBindings_step _ _ _ _ hv17]
  set E18 : SlotEnv := E17.push (CertVal.bv v17) with hE18
  have hs18 : E18.size = 20 := by rw [hE18, Array.size_push, hs17]
  have hag18 : ∀ j, j < E0.size → refBV E18 j = refBV E0 j :=
    fun j hj => by rw [hE18]; exact srcAgree_push _ (by omega) hag17 j hj
  clear ra17 rb17 hv17 hag17
  have ra18 : refBV E18 19 = v17 := by
    rw [hE18, show (19 : Nat) = E17.size from (hs17).symm, refBV_push_self]
    rfl
  have rb18 : refBV E18 11 = v9 := by
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, show (11 : Nat) = E9.size from (hs9).symm, refBV_push_self]
    rfl
  have hv18 : denoteExpr E18 (ResidualExpr.rand 8 #[19, 11]) = CertVal.bv v18 := by
    have hu : denoteExpr E18 (ResidualExpr.rand 8 #[19, 11]) = CertVal.bv (randV 8 [refBV E18 19, refBV E18 11]) := rfl
    rw [hu, ra18, rb18]
  rw [runBindings_step _ _ _ _ hv18]
  set E19 : SlotEnv := E18.push (CertVal.bv v18) with hE19
  have hs19 : E19.size = 21 := by rw [hE19, Array.size_push, hs18]
  have hag19 : ∀ j, j < E0.size → refBV E19 j = refBV E0 j :=
    fun j hj => by rw [hE19]; exact srcAgree_push _ (by omega) hag18 j hj
  clear ra18 rb18 hv18 hag18
  have ra19 : refBV E19 20 = v18 := by
    rw [hE19, show (20 : Nat) = E18.size from (hs18).symm, refBV_push_self]
    rfl
  have rb19 : refBV E19 11 = v9 := by
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, refBV_push_lt _ _ _ (by rw [hs10]; omega)]
    rw [hE10, show (11 : Nat) = E9.size from (hs9).symm, refBV_push_self]
    rfl
  have hv19 : denoteExpr E19 (ResidualExpr.rand 8 #[20, 11]) = CertVal.bv v19 := by
    have hu : denoteExpr E19 (ResidualExpr.rand 8 #[20, 11]) = CertVal.bv (randV 8 [refBV E19 20, refBV E19 11]) := rfl
    rw [hu, ra19, rb19]
  rw [runBindings_step _ _ _ _ hv19]
  set E20 : SlotEnv := E19.push (CertVal.bv v19) with hE20
  have hs20 : E20.size = 22 := by rw [hE20, Array.size_push, hs19]
  have hag20 : ∀ j, j < E0.size → refBV E20 j = refBV E0 j :=
    fun j hj => by rw [hE20]; exact srcAgree_push _ (by omega) hag19 j hj
  clear ra19 rb19 hv19 hag19
  have ra20 : refBV E20 21 = v19 := by
    rw [hE20, show (21 : Nat) = E19.size from (hs19).symm, refBV_push_self]
    rfl
  have rb20 : refBV E20 12 = v10 := by
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, show (12 : Nat) = E10.size from (hs10).symm, refBV_push_self]
    rfl
  have hv20 : denoteExpr E20 (ResidualExpr.rand 8 #[21, 12]) = CertVal.bv v20 := by
    have hu : denoteExpr E20 (ResidualExpr.rand 8 #[21, 12]) = CertVal.bv (randV 8 [refBV E20 21, refBV E20 12]) := rfl
    rw [hu, ra20, rb20]
  rw [runBindings_step _ _ _ _ hv20]
  set E21 : SlotEnv := E20.push (CertVal.bv v20) with hE21
  have hs21 : E21.size = 23 := by rw [hE21, Array.size_push, hs20]
  have hag21 : ∀ j, j < E0.size → refBV E21 j = refBV E0 j :=
    fun j hj => by rw [hE21]; exact srcAgree_push _ (by omega) hag20 j hj
  clear ra20 rb20 hv20 hag20
  have ra21 : refBV E21 22 = v20 := by
    rw [hE21, show (22 : Nat) = E20.size from (hs20).symm, refBV_push_self]
    rfl
  have rb21 : refBV E21 12 = v10 := by
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, refBV_push_lt _ _ _ (by rw [hs11]; omega)]
    rw [hE11, show (12 : Nat) = E10.size from (hs10).symm, refBV_push_self]
    rfl
  have hv21 : denoteExpr E21 (ResidualExpr.rand 8 #[22, 12]) = CertVal.bv v21 := by
    have hu : denoteExpr E21 (ResidualExpr.rand 8 #[22, 12]) = CertVal.bv (randV 8 [refBV E21 22, refBV E21 12]) := rfl
    rw [hu, ra21, rb21]
  rw [runBindings_step _ _ _ _ hv21]
  set E22 : SlotEnv := E21.push (CertVal.bv v21) with hE22
  have hs22 : E22.size = 24 := by rw [hE22, Array.size_push, hs21]
  have hag22 : ∀ j, j < E0.size → refBV E22 j = refBV E0 j :=
    fun j hj => by rw [hE22]; exact srcAgree_push _ (by omega) hag21 j hj
  clear ra21 rb21 hv21 hag21
  have ra22 : refBV E22 23 = v21 := by
    rw [hE22, show (23 : Nat) = E21.size from (hs21).symm, refBV_push_self]
    rfl
  have rb22 : refBV E22 13 = v11 := by
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, show (13 : Nat) = E11.size from (hs11).symm, refBV_push_self]
    rfl
  have hv22 : denoteExpr E22 (ResidualExpr.rand 8 #[23, 13]) = CertVal.bv v22 := by
    have hu : denoteExpr E22 (ResidualExpr.rand 8 #[23, 13]) = CertVal.bv (randV 8 [refBV E22 23, refBV E22 13]) := rfl
    rw [hu, ra22, rb22]
  rw [runBindings_step _ _ _ _ hv22]
  set E23 : SlotEnv := E22.push (CertVal.bv v22) with hE23
  have hs23 : E23.size = 25 := by rw [hE23, Array.size_push, hs22]
  have hag23 : ∀ j, j < E0.size → refBV E23 j = refBV E0 j :=
    fun j hj => by rw [hE23]; exact srcAgree_push _ (by omega) hag22 j hj
  clear ra22 rb22 hv22 hag22
  have ra23 : refBV E23 24 = v22 := by
    rw [hE23, show (24 : Nat) = E22.size from (hs22).symm, refBV_push_self]
    rfl
  have rb23 : refBV E23 13 = v11 := by
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, refBV_push_lt _ _ _ (by rw [hs12]; omega)]
    rw [hE12, show (13 : Nat) = E11.size from (hs11).symm, refBV_push_self]
    rfl
  have hv23 : denoteExpr E23 (ResidualExpr.rand 8 #[24, 13]) = CertVal.bv v23 := by
    have hu : denoteExpr E23 (ResidualExpr.rand 8 #[24, 13]) = CertVal.bv (randV 8 [refBV E23 24, refBV E23 13]) := rfl
    rw [hu, ra23, rb23]
  rw [runBindings_step _ _ _ _ hv23]
  set E24 : SlotEnv := E23.push (CertVal.bv v23) with hE24
  have hs24 : E24.size = 26 := by rw [hE24, Array.size_push, hs23]
  have hag24 : ∀ j, j < E0.size → refBV E24 j = refBV E0 j :=
    fun j hj => by rw [hE24]; exact srcAgree_push _ (by omega) hag23 j hj
  clear ra23 rb23 hv23 hag23
  have ra24 : refBV E24 25 = v23 := by
    rw [hE24, show (25 : Nat) = E23.size from (hs23).symm, refBV_push_self]
    rfl
  have rb24 : refBV E24 14 = v12 := by
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, show (14 : Nat) = E12.size from (hs12).symm, refBV_push_self]
    rfl
  have hv24 : denoteExpr E24 (ResidualExpr.rand 8 #[25, 14]) = CertVal.bv v24 := by
    have hu : denoteExpr E24 (ResidualExpr.rand 8 #[25, 14]) = CertVal.bv (randV 8 [refBV E24 25, refBV E24 14]) := rfl
    rw [hu, ra24, rb24]
  rw [runBindings_step _ _ _ _ hv24]
  set E25 : SlotEnv := E24.push (CertVal.bv v24) with hE25
  have hs25 : E25.size = 27 := by rw [hE25, Array.size_push, hs24]
  have hag25 : ∀ j, j < E0.size → refBV E25 j = refBV E0 j :=
    fun j hj => by rw [hE25]; exact srcAgree_push _ (by omega) hag24 j hj
  clear ra24 rb24 hv24 hag24
  have ra25 : refBV E25 26 = v24 := by
    rw [hE25, show (26 : Nat) = E24.size from (hs24).symm, refBV_push_self]
    rfl
  have rb25 : refBV E25 14 = v12 := by
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, refBV_push_lt _ _ _ (by rw [hs13]; omega)]
    rw [hE13, show (14 : Nat) = E12.size from (hs12).symm, refBV_push_self]
    rfl
  have hv25 : denoteExpr E25 (ResidualExpr.rand 8 #[26, 14]) = CertVal.bv v25 := by
    have hu : denoteExpr E25 (ResidualExpr.rand 8 #[26, 14]) = CertVal.bv (randV 8 [refBV E25 26, refBV E25 14]) := rfl
    rw [hu, ra25, rb25]
  rw [runBindings_step _ _ _ _ hv25]
  set E26 : SlotEnv := E25.push (CertVal.bv v25) with hE26
  have hs26 : E26.size = 28 := by rw [hE26, Array.size_push, hs25]
  have hag26 : ∀ j, j < E0.size → refBV E26 j = refBV E0 j :=
    fun j hj => by rw [hE26]; exact srcAgree_push _ (by omega) hag25 j hj
  clear ra25 rb25 hv25 hag25
  have ra26 : refBV E26 27 = v25 := by
    rw [hE26, show (27 : Nat) = E25.size from (hs25).symm, refBV_push_self]
    rfl
  have rb26 : refBV E26 15 = v13 := by
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, show (15 : Nat) = E13.size from (hs13).symm, refBV_push_self]
    rfl
  have hv26 : denoteExpr E26 (ResidualExpr.rand 8 #[27, 15]) = CertVal.bv v26 := by
    have hu : denoteExpr E26 (ResidualExpr.rand 8 #[27, 15]) = CertVal.bv (randV 8 [refBV E26 27, refBV E26 15]) := rfl
    rw [hu, ra26, rb26]
  rw [runBindings_step _ _ _ _ hv26]
  set E27 : SlotEnv := E26.push (CertVal.bv v26) with hE27
  have hs27 : E27.size = 29 := by rw [hE27, Array.size_push, hs26]
  have hag27 : ∀ j, j < E0.size → refBV E27 j = refBV E0 j :=
    fun j hj => by rw [hE27]; exact srcAgree_push _ (by omega) hag26 j hj
  clear ra26 rb26 hv26 hag26
  have ra27 : refBV E27 28 = v26 := by
    rw [hE27, show (28 : Nat) = E26.size from (hs26).symm, refBV_push_self]
    rfl
  have rb27 : refBV E27 15 = v13 := by
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, refBV_push_lt _ _ _ (by rw [hs14]; omega)]
    rw [hE14, show (15 : Nat) = E13.size from (hs13).symm, refBV_push_self]
    rfl
  have hv27 : denoteExpr E27 (ResidualExpr.rand 8 #[28, 15]) = CertVal.bv v27 := by
    have hu : denoteExpr E27 (ResidualExpr.rand 8 #[28, 15]) = CertVal.bv (randV 8 [refBV E27 28, refBV E27 15]) := rfl
    rw [hu, ra27, rb27]
  rw [runBindings_step _ _ _ _ hv27]
  set E28 : SlotEnv := E27.push (CertVal.bv v27) with hE28
  have hs28 : E28.size = 30 := by rw [hE28, Array.size_push, hs27]
  have hag28 : ∀ j, j < E0.size → refBV E28 j = refBV E0 j :=
    fun j hj => by rw [hE28]; exact srcAgree_push _ (by omega) hag27 j hj
  clear ra27 rb27 hv27 hag27
  have ra28 : refBV E28 29 = v27 := by
    rw [hE28, show (29 : Nat) = E27.size from (hs27).symm, refBV_push_self]
    rfl
  have rb28 : refBV E28 16 = v14 := by
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, show (16 : Nat) = E14.size from (hs14).symm, refBV_push_self]
    rfl
  have hv28 : denoteExpr E28 (ResidualExpr.rand 8 #[29, 16]) = CertVal.bv v28 := by
    have hu : denoteExpr E28 (ResidualExpr.rand 8 #[29, 16]) = CertVal.bv (randV 8 [refBV E28 29, refBV E28 16]) := rfl
    rw [hu, ra28, rb28]
  rw [runBindings_step _ _ _ _ hv28]
  set E29 : SlotEnv := E28.push (CertVal.bv v28) with hE29
  have hs29 : E29.size = 31 := by rw [hE29, Array.size_push, hs28]
  have hag29 : ∀ j, j < E0.size → refBV E29 j = refBV E0 j :=
    fun j hj => by rw [hE29]; exact srcAgree_push _ (by omega) hag28 j hj
  clear ra28 rb28 hv28 hag28
  have ra29 : refBV E29 30 = v28 := by
    rw [hE29, show (30 : Nat) = E28.size from (hs28).symm, refBV_push_self]
    rfl
  have rb29 : refBV E29 16 = v14 := by
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, refBV_push_lt _ _ _ (by rw [hs15]; omega)]
    rw [hE15, show (16 : Nat) = E14.size from (hs14).symm, refBV_push_self]
    rfl
  have hv29 : denoteExpr E29 (ResidualExpr.rand 8 #[30, 16]) = CertVal.bv v29 := by
    have hu : denoteExpr E29 (ResidualExpr.rand 8 #[30, 16]) = CertVal.bv (randV 8 [refBV E29 30, refBV E29 16]) := rfl
    rw [hu, ra29, rb29]
  rw [runBindings_step _ _ _ _ hv29]
  set E30 : SlotEnv := E29.push (CertVal.bv v29) with hE30
  have hs30 : E30.size = 32 := by rw [hE30, Array.size_push, hs29]
  have hag30 : ∀ j, j < E0.size → refBV E30 j = refBV E0 j :=
    fun j hj => by rw [hE30]; exact srcAgree_push _ (by omega) hag29 j hj
  clear ra29 rb29 hv29 hag29
  have ra30 : refBV E30 31 = v29 := by
    rw [hE30, show (31 : Nat) = E29.size from (hs29).symm, refBV_push_self]
    rfl
  have rb30 : refBV E30 17 = v15 := by
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, show (17 : Nat) = E15.size from (hs15).symm, refBV_push_self]
    rfl
  have hv30 : denoteExpr E30 (ResidualExpr.rand 8 #[31, 17]) = CertVal.bv v30 := by
    have hu : denoteExpr E30 (ResidualExpr.rand 8 #[31, 17]) = CertVal.bv (randV 8 [refBV E30 31, refBV E30 17]) := rfl
    rw [hu, ra30, rb30]
  rw [runBindings_step _ _ _ _ hv30]
  set E31 : SlotEnv := E30.push (CertVal.bv v30) with hE31
  have hs31 : E31.size = 33 := by rw [hE31, Array.size_push, hs30]
  have hag31 : ∀ j, j < E0.size → refBV E31 j = refBV E0 j :=
    fun j hj => by rw [hE31]; exact srcAgree_push _ (by omega) hag30 j hj
  clear ra30 rb30 hv30 hag30
  have ra31 : refBV E31 32 = v30 := by
    rw [hE31, show (32 : Nat) = E30.size from (hs30).symm, refBV_push_self]
    rfl
  have rb31 : refBV E31 17 = v15 := by
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, refBV_push_lt _ _ _ (by rw [hs16]; omega)]
    rw [hE16, show (17 : Nat) = E15.size from (hs15).symm, refBV_push_self]
    rfl
  have hv31 : denoteExpr E31 (ResidualExpr.rand 8 #[32, 17]) = CertVal.bv v31 := by
    have hu : denoteExpr E31 (ResidualExpr.rand 8 #[32, 17]) = CertVal.bv (randV 8 [refBV E31 32, refBV E31 17]) := rfl
    rw [hu, ra31, rb31]
  rw [runBindings_step _ _ _ _ hv31]
  set E32 : SlotEnv := E31.push (CertVal.bv v31) with hE32
  have hs32 : E32.size = 34 := by rw [hE32, Array.size_push, hs31]
  have hag32 : ∀ j, j < E0.size → refBV E32 j = refBV E0 j :=
    fun j hj => by rw [hE32]; exact srcAgree_push _ (by omega) hag31 j hj
  clear ra31 rb31 hv31 hag31
  have ra32 : refBV E32 33 = v31 := by
    rw [hE32, show (33 : Nat) = E31.size from (hs31).symm, refBV_push_self]
    rfl
  have rb32 : refBV E32 18 = v16 := by
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, show (18 : Nat) = E16.size from (hs16).symm, refBV_push_self]
    rfl
  have hv32 : denoteExpr E32 (ResidualExpr.rand 8 #[33, 18]) = CertVal.bv v32 := by
    have hu : denoteExpr E32 (ResidualExpr.rand 8 #[33, 18]) = CertVal.bv (randV 8 [refBV E32 33, refBV E32 18]) := rfl
    rw [hu, ra32, rb32]
  rw [runBindings_step _ _ _ _ hv32]
  set E33 : SlotEnv := E32.push (CertVal.bv v32) with hE33
  have hs33 : E33.size = 35 := by rw [hE33, Array.size_push, hs32]
  have hag33 : ∀ j, j < E0.size → refBV E33 j = refBV E0 j :=
    fun j hj => by rw [hE33]; exact srcAgree_push _ (by omega) hag32 j hj
  clear ra32 rb32 hv32 hag32
  have ra33 : refBV E33 34 = v32 := by
    rw [hE33, show (34 : Nat) = E32.size from (hs32).symm, refBV_push_self]
    rfl
  have rb33 : refBV E33 18 = v16 := by
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, refBV_push_lt _ _ _ (by rw [hs17]; omega)]
    rw [hE17, show (18 : Nat) = E16.size from (hs16).symm, refBV_push_self]
    rfl
  have hv33 : denoteExpr E33 (ResidualExpr.rand 8 #[34, 18]) = CertVal.bv v33 := by
    have hu : denoteExpr E33 (ResidualExpr.rand 8 #[34, 18]) = CertVal.bv (randV 8 [refBV E33 34, refBV E33 18]) := rfl
    rw [hu, ra33, rb33]
  rw [runBindings_step _ _ _ _ hv33]
  set E34 : SlotEnv := E33.push (CertVal.bv v33) with hE34
  have hs34 : E34.size = 36 := by rw [hE34, Array.size_push, hs33]
  have hag34 : ∀ j, j < E0.size → refBV E34 j = refBV E0 j :=
    fun j hj => by rw [hE34]; exact srcAgree_push _ (by omega) hag33 j hj
  clear ra33 rb33 hv33 hag33
  have ra34 : refBV E34 35 = v33 := by
    rw [hE34, show (35 : Nat) = E33.size from (hs33).symm, refBV_push_self]
    rfl
  have rb34 : refBV E34 19 = v17 := by
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, show (19 : Nat) = E17.size from (hs17).symm, refBV_push_self]
    rfl
  have hv34 : denoteExpr E34 (ResidualExpr.rand 8 #[35, 19]) = CertVal.bv v34 := by
    have hu : denoteExpr E34 (ResidualExpr.rand 8 #[35, 19]) = CertVal.bv (randV 8 [refBV E34 35, refBV E34 19]) := rfl
    rw [hu, ra34, rb34]
  rw [runBindings_step _ _ _ _ hv34]
  set E35 : SlotEnv := E34.push (CertVal.bv v34) with hE35
  have hs35 : E35.size = 37 := by rw [hE35, Array.size_push, hs34]
  have hag35 : ∀ j, j < E0.size → refBV E35 j = refBV E0 j :=
    fun j hj => by rw [hE35]; exact srcAgree_push _ (by omega) hag34 j hj
  clear ra34 rb34 hv34 hag34
  have ra35 : refBV E35 36 = v34 := by
    rw [hE35, show (36 : Nat) = E34.size from (hs34).symm, refBV_push_self]
    rfl
  have rb35 : refBV E35 19 = v17 := by
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, refBV_push_lt _ _ _ (by rw [hs18]; omega)]
    rw [hE18, show (19 : Nat) = E17.size from (hs17).symm, refBV_push_self]
    rfl
  have hv35 : denoteExpr E35 (ResidualExpr.rand 8 #[36, 19]) = CertVal.bv v35 := by
    have hu : denoteExpr E35 (ResidualExpr.rand 8 #[36, 19]) = CertVal.bv (randV 8 [refBV E35 36, refBV E35 19]) := rfl
    rw [hu, ra35, rb35]
  rw [runBindings_step _ _ _ _ hv35]
  set E36 : SlotEnv := E35.push (CertVal.bv v35) with hE36
  have hs36 : E36.size = 38 := by rw [hE36, Array.size_push, hs35]
  have hag36 : ∀ j, j < E0.size → refBV E36 j = refBV E0 j :=
    fun j hj => by rw [hE36]; exact srcAgree_push _ (by omega) hag35 j hj
  clear ra35 rb35 hv35 hag35
  have ra36 : refBV E36 37 = v35 := by
    rw [hE36, show (37 : Nat) = E35.size from (hs35).symm, refBV_push_self]
    rfl
  have rb36 : refBV E36 20 = v18 := by
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, show (20 : Nat) = E18.size from (hs18).symm, refBV_push_self]
    rfl
  have hv36 : denoteExpr E36 (ResidualExpr.rand 8 #[37, 20]) = CertVal.bv v36 := by
    have hu : denoteExpr E36 (ResidualExpr.rand 8 #[37, 20]) = CertVal.bv (randV 8 [refBV E36 37, refBV E36 20]) := rfl
    rw [hu, ra36, rb36]
  rw [runBindings_step _ _ _ _ hv36]
  set E37 : SlotEnv := E36.push (CertVal.bv v36) with hE37
  have hs37 : E37.size = 39 := by rw [hE37, Array.size_push, hs36]
  have hag37 : ∀ j, j < E0.size → refBV E37 j = refBV E0 j :=
    fun j hj => by rw [hE37]; exact srcAgree_push _ (by omega) hag36 j hj
  clear ra36 rb36 hv36 hag36
  have ra37 : refBV E37 38 = v36 := by
    rw [hE37, show (38 : Nat) = E36.size from (hs36).symm, refBV_push_self]
    rfl
  have rb37 : refBV E37 20 = v18 := by
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, refBV_push_lt _ _ _ (by rw [hs19]; omega)]
    rw [hE19, show (20 : Nat) = E18.size from (hs18).symm, refBV_push_self]
    rfl
  have hv37 : denoteExpr E37 (ResidualExpr.rand 8 #[38, 20]) = CertVal.bv v37 := by
    have hu : denoteExpr E37 (ResidualExpr.rand 8 #[38, 20]) = CertVal.bv (randV 8 [refBV E37 38, refBV E37 20]) := rfl
    rw [hu, ra37, rb37]
  rw [runBindings_step _ _ _ _ hv37]
  set E38 : SlotEnv := E37.push (CertVal.bv v37) with hE38
  have hs38 : E38.size = 40 := by rw [hE38, Array.size_push, hs37]
  have hag38 : ∀ j, j < E0.size → refBV E38 j = refBV E0 j :=
    fun j hj => by rw [hE38]; exact srcAgree_push _ (by omega) hag37 j hj
  clear ra37 rb37 hv37 hag37
  have ra38 : refBV E38 39 = v37 := by
    rw [hE38, show (39 : Nat) = E37.size from (hs37).symm, refBV_push_self]
    rfl
  have rb38 : refBV E38 21 = v19 := by
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, show (21 : Nat) = E19.size from (hs19).symm, refBV_push_self]
    rfl
  have hv38 : denoteExpr E38 (ResidualExpr.rand 8 #[39, 21]) = CertVal.bv v38 := by
    have hu : denoteExpr E38 (ResidualExpr.rand 8 #[39, 21]) = CertVal.bv (randV 8 [refBV E38 39, refBV E38 21]) := rfl
    rw [hu, ra38, rb38]
  rw [runBindings_step _ _ _ _ hv38]
  set E39 : SlotEnv := E38.push (CertVal.bv v38) with hE39
  have hs39 : E39.size = 41 := by rw [hE39, Array.size_push, hs38]
  have hag39 : ∀ j, j < E0.size → refBV E39 j = refBV E0 j :=
    fun j hj => by rw [hE39]; exact srcAgree_push _ (by omega) hag38 j hj
  clear ra38 rb38 hv38 hag38
  have ra39 : refBV E39 40 = v38 := by
    rw [hE39, show (40 : Nat) = E38.size from (hs38).symm, refBV_push_self]
    rfl
  have rb39 : refBV E39 21 = v19 := by
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, refBV_push_lt _ _ _ (by rw [hs20]; omega)]
    rw [hE20, show (21 : Nat) = E19.size from (hs19).symm, refBV_push_self]
    rfl
  have hv39 : denoteExpr E39 (ResidualExpr.rand 8 #[40, 21]) = CertVal.bv v39 := by
    have hu : denoteExpr E39 (ResidualExpr.rand 8 #[40, 21]) = CertVal.bv (randV 8 [refBV E39 40, refBV E39 21]) := rfl
    rw [hu, ra39, rb39]
  rw [runBindings_step _ _ _ _ hv39]
  set E40 : SlotEnv := E39.push (CertVal.bv v39) with hE40
  have hs40 : E40.size = 42 := by rw [hE40, Array.size_push, hs39]
  have hag40 : ∀ j, j < E0.size → refBV E40 j = refBV E0 j :=
    fun j hj => by rw [hE40]; exact srcAgree_push _ (by omega) hag39 j hj
  clear ra39 rb39 hv39 hag39
  have ra40 : refBV E40 41 = v39 := by
    rw [hE40, show (41 : Nat) = E39.size from (hs39).symm, refBV_push_self]
    rfl
  have rb40 : refBV E40 22 = v20 := by
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, show (22 : Nat) = E20.size from (hs20).symm, refBV_push_self]
    rfl
  have hv40 : denoteExpr E40 (ResidualExpr.rand 8 #[41, 22]) = CertVal.bv v40 := by
    have hu : denoteExpr E40 (ResidualExpr.rand 8 #[41, 22]) = CertVal.bv (randV 8 [refBV E40 41, refBV E40 22]) := rfl
    rw [hu, ra40, rb40]
  rw [runBindings_step _ _ _ _ hv40]
  set E41 : SlotEnv := E40.push (CertVal.bv v40) with hE41
  have hs41 : E41.size = 43 := by rw [hE41, Array.size_push, hs40]
  have hag41 : ∀ j, j < E0.size → refBV E41 j = refBV E0 j :=
    fun j hj => by rw [hE41]; exact srcAgree_push _ (by omega) hag40 j hj
  clear ra40 rb40 hv40 hag40
  have ra41 : refBV E41 42 = v40 := by
    rw [hE41, show (42 : Nat) = E40.size from (hs40).symm, refBV_push_self]
    rfl
  have rb41 : refBV E41 22 = v20 := by
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, refBV_push_lt _ _ _ (by rw [hs21]; omega)]
    rw [hE21, show (22 : Nat) = E20.size from (hs20).symm, refBV_push_self]
    rfl
  have hv41 : denoteExpr E41 (ResidualExpr.rand 8 #[42, 22]) = CertVal.bv v41 := by
    have hu : denoteExpr E41 (ResidualExpr.rand 8 #[42, 22]) = CertVal.bv (randV 8 [refBV E41 42, refBV E41 22]) := rfl
    rw [hu, ra41, rb41]
  rw [runBindings_step _ _ _ _ hv41]
  set E42 : SlotEnv := E41.push (CertVal.bv v41) with hE42
  have hs42 : E42.size = 44 := by rw [hE42, Array.size_push, hs41]
  have hag42 : ∀ j, j < E0.size → refBV E42 j = refBV E0 j :=
    fun j hj => by rw [hE42]; exact srcAgree_push _ (by omega) hag41 j hj
  clear ra41 rb41 hv41 hag41
  have ra42 : refBV E42 43 = v41 := by
    rw [hE42, show (43 : Nat) = E41.size from (hs41).symm, refBV_push_self]
    rfl
  have rb42 : refBV E42 23 = v21 := by
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, show (23 : Nat) = E21.size from (hs21).symm, refBV_push_self]
    rfl
  have hv42 : denoteExpr E42 (ResidualExpr.rand 8 #[43, 23]) = CertVal.bv v42 := by
    have hu : denoteExpr E42 (ResidualExpr.rand 8 #[43, 23]) = CertVal.bv (randV 8 [refBV E42 43, refBV E42 23]) := rfl
    rw [hu, ra42, rb42]
  rw [runBindings_step _ _ _ _ hv42]
  set E43 : SlotEnv := E42.push (CertVal.bv v42) with hE43
  have hs43 : E43.size = 45 := by rw [hE43, Array.size_push, hs42]
  have hag43 : ∀ j, j < E0.size → refBV E43 j = refBV E0 j :=
    fun j hj => by rw [hE43]; exact srcAgree_push _ (by omega) hag42 j hj
  clear ra42 rb42 hv42 hag42
  have ra43 : refBV E43 44 = v42 := by
    rw [hE43, show (44 : Nat) = E42.size from (hs42).symm, refBV_push_self]
    rfl
  have rb43 : refBV E43 23 = v21 := by
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, refBV_push_lt _ _ _ (by rw [hs22]; omega)]
    rw [hE22, show (23 : Nat) = E21.size from (hs21).symm, refBV_push_self]
    rfl
  have hv43 : denoteExpr E43 (ResidualExpr.rand 8 #[44, 23]) = CertVal.bv v43 := by
    have hu : denoteExpr E43 (ResidualExpr.rand 8 #[44, 23]) = CertVal.bv (randV 8 [refBV E43 44, refBV E43 23]) := rfl
    rw [hu, ra43, rb43]
  rw [runBindings_step _ _ _ _ hv43]
  set E44 : SlotEnv := E43.push (CertVal.bv v43) with hE44
  have hs44 : E44.size = 46 := by rw [hE44, Array.size_push, hs43]
  have hag44 : ∀ j, j < E0.size → refBV E44 j = refBV E0 j :=
    fun j hj => by rw [hE44]; exact srcAgree_push _ (by omega) hag43 j hj
  clear ra43 rb43 hv43 hag43
  have ra44 : refBV E44 45 = v43 := by
    rw [hE44, show (45 : Nat) = E43.size from (hs43).symm, refBV_push_self]
    rfl
  have rb44 : refBV E44 24 = v22 := by
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, show (24 : Nat) = E22.size from (hs22).symm, refBV_push_self]
    rfl
  have hv44 : denoteExpr E44 (ResidualExpr.rand 8 #[45, 24]) = CertVal.bv v44 := by
    have hu : denoteExpr E44 (ResidualExpr.rand 8 #[45, 24]) = CertVal.bv (randV 8 [refBV E44 45, refBV E44 24]) := rfl
    rw [hu, ra44, rb44]
  rw [runBindings_step _ _ _ _ hv44]
  set E45 : SlotEnv := E44.push (CertVal.bv v44) with hE45
  have hs45 : E45.size = 47 := by rw [hE45, Array.size_push, hs44]
  have hag45 : ∀ j, j < E0.size → refBV E45 j = refBV E0 j :=
    fun j hj => by rw [hE45]; exact srcAgree_push _ (by omega) hag44 j hj
  clear ra44 rb44 hv44 hag44
  have ra45 : refBV E45 46 = v44 := by
    rw [hE45, show (46 : Nat) = E44.size from (hs44).symm, refBV_push_self]
    rfl
  have rb45 : refBV E45 24 = v22 := by
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, refBV_push_lt _ _ _ (by rw [hs23]; omega)]
    rw [hE23, show (24 : Nat) = E22.size from (hs22).symm, refBV_push_self]
    rfl
  have hv45 : denoteExpr E45 (ResidualExpr.rand 8 #[46, 24]) = CertVal.bv v45 := by
    have hu : denoteExpr E45 (ResidualExpr.rand 8 #[46, 24]) = CertVal.bv (randV 8 [refBV E45 46, refBV E45 24]) := rfl
    rw [hu, ra45, rb45]
  rw [runBindings_step _ _ _ _ hv45]
  set E46 : SlotEnv := E45.push (CertVal.bv v45) with hE46
  have hs46 : E46.size = 48 := by rw [hE46, Array.size_push, hs45]
  have hag46 : ∀ j, j < E0.size → refBV E46 j = refBV E0 j :=
    fun j hj => by rw [hE46]; exact srcAgree_push _ (by omega) hag45 j hj
  clear ra45 rb45 hv45 hag45
  have ra46 : refBV E46 47 = v45 := by
    rw [hE46, show (47 : Nat) = E45.size from (hs45).symm, refBV_push_self]
    rfl
  have rb46 : refBV E46 25 = v23 := by
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, show (25 : Nat) = E23.size from (hs23).symm, refBV_push_self]
    rfl
  have hv46 : denoteExpr E46 (ResidualExpr.rand 8 #[47, 25]) = CertVal.bv v46 := by
    have hu : denoteExpr E46 (ResidualExpr.rand 8 #[47, 25]) = CertVal.bv (randV 8 [refBV E46 47, refBV E46 25]) := rfl
    rw [hu, ra46, rb46]
  rw [runBindings_step _ _ _ _ hv46]
  set E47 : SlotEnv := E46.push (CertVal.bv v46) with hE47
  have hs47 : E47.size = 49 := by rw [hE47, Array.size_push, hs46]
  have hag47 : ∀ j, j < E0.size → refBV E47 j = refBV E0 j :=
    fun j hj => by rw [hE47]; exact srcAgree_push _ (by omega) hag46 j hj
  clear ra46 rb46 hv46 hag46
  have ra47 : refBV E47 48 = v46 := by
    rw [hE47, show (48 : Nat) = E46.size from (hs46).symm, refBV_push_self]
    rfl
  have rb47 : refBV E47 25 = v23 := by
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, refBV_push_lt _ _ _ (by rw [hs24]; omega)]
    rw [hE24, show (25 : Nat) = E23.size from (hs23).symm, refBV_push_self]
    rfl
  have hv47 : denoteExpr E47 (ResidualExpr.rand 8 #[48, 25]) = CertVal.bv v47 := by
    have hu : denoteExpr E47 (ResidualExpr.rand 8 #[48, 25]) = CertVal.bv (randV 8 [refBV E47 48, refBV E47 25]) := rfl
    rw [hu, ra47, rb47]
  rw [runBindings_step _ _ _ _ hv47]
  set E48 : SlotEnv := E47.push (CertVal.bv v47) with hE48
  have hs48 : E48.size = 50 := by rw [hE48, Array.size_push, hs47]
  have hag48 : ∀ j, j < E0.size → refBV E48 j = refBV E0 j :=
    fun j hj => by rw [hE48]; exact srcAgree_push _ (by omega) hag47 j hj
  clear ra47 rb47 hv47 hag47
  have ra48 : refBV E48 49 = v47 := by
    rw [hE48, show (49 : Nat) = E47.size from (hs47).symm, refBV_push_self]
    rfl
  have rb48 : refBV E48 26 = v24 := by
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, show (26 : Nat) = E24.size from (hs24).symm, refBV_push_self]
    rfl
  have hv48 : denoteExpr E48 (ResidualExpr.rand 8 #[49, 26]) = CertVal.bv v48 := by
    have hu : denoteExpr E48 (ResidualExpr.rand 8 #[49, 26]) = CertVal.bv (randV 8 [refBV E48 49, refBV E48 26]) := rfl
    rw [hu, ra48, rb48]
  rw [runBindings_step _ _ _ _ hv48]
  set E49 : SlotEnv := E48.push (CertVal.bv v48) with hE49
  have hs49 : E49.size = 51 := by rw [hE49, Array.size_push, hs48]
  have hag49 : ∀ j, j < E0.size → refBV E49 j = refBV E0 j :=
    fun j hj => by rw [hE49]; exact srcAgree_push _ (by omega) hag48 j hj
  clear ra48 rb48 hv48 hag48
  have ra49 : refBV E49 50 = v48 := by
    rw [hE49, show (50 : Nat) = E48.size from (hs48).symm, refBV_push_self]
    rfl
  have rb49 : refBV E49 26 = v24 := by
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, refBV_push_lt _ _ _ (by rw [hs25]; omega)]
    rw [hE25, show (26 : Nat) = E24.size from (hs24).symm, refBV_push_self]
    rfl
  have hv49 : denoteExpr E49 (ResidualExpr.rand 8 #[50, 26]) = CertVal.bv v49 := by
    have hu : denoteExpr E49 (ResidualExpr.rand 8 #[50, 26]) = CertVal.bv (randV 8 [refBV E49 50, refBV E49 26]) := rfl
    rw [hu, ra49, rb49]
  rw [runBindings_step _ _ _ _ hv49]
  set E50 : SlotEnv := E49.push (CertVal.bv v49) with hE50
  have hs50 : E50.size = 52 := by rw [hE50, Array.size_push, hs49]
  have hag50 : ∀ j, j < E0.size → refBV E50 j = refBV E0 j :=
    fun j hj => by rw [hE50]; exact srcAgree_push _ (by omega) hag49 j hj
  clear ra49 rb49 hv49 hag49
  have ra50 : refBV E50 51 = v49 := by
    rw [hE50, show (51 : Nat) = E49.size from (hs49).symm, refBV_push_self]
    rfl
  have rb50 : refBV E50 27 = v25 := by
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, show (27 : Nat) = E25.size from (hs25).symm, refBV_push_self]
    rfl
  have hv50 : denoteExpr E50 (ResidualExpr.rand 8 #[51, 27]) = CertVal.bv v50 := by
    have hu : denoteExpr E50 (ResidualExpr.rand 8 #[51, 27]) = CertVal.bv (randV 8 [refBV E50 51, refBV E50 27]) := rfl
    rw [hu, ra50, rb50]
  rw [runBindings_step _ _ _ _ hv50]
  set E51 : SlotEnv := E50.push (CertVal.bv v50) with hE51
  have hs51 : E51.size = 53 := by rw [hE51, Array.size_push, hs50]
  have hag51 : ∀ j, j < E0.size → refBV E51 j = refBV E0 j :=
    fun j hj => by rw [hE51]; exact srcAgree_push _ (by omega) hag50 j hj
  clear ra50 rb50 hv50 hag50
  have ra51 : refBV E51 52 = v50 := by
    rw [hE51, show (52 : Nat) = E50.size from (hs50).symm, refBV_push_self]
    rfl
  have rb51 : refBV E51 27 = v25 := by
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, refBV_push_lt _ _ _ (by rw [hs26]; omega)]
    rw [hE26, show (27 : Nat) = E25.size from (hs25).symm, refBV_push_self]
    rfl
  have hv51 : denoteExpr E51 (ResidualExpr.rand 8 #[52, 27]) = CertVal.bv v51 := by
    have hu : denoteExpr E51 (ResidualExpr.rand 8 #[52, 27]) = CertVal.bv (randV 8 [refBV E51 52, refBV E51 27]) := rfl
    rw [hu, ra51, rb51]
  rw [runBindings_step _ _ _ _ hv51]
  set E52 : SlotEnv := E51.push (CertVal.bv v51) with hE52
  have hs52 : E52.size = 54 := by rw [hE52, Array.size_push, hs51]
  have hag52 : ∀ j, j < E0.size → refBV E52 j = refBV E0 j :=
    fun j hj => by rw [hE52]; exact srcAgree_push _ (by omega) hag51 j hj
  clear ra51 rb51 hv51 hag51
  have ra52 : refBV E52 53 = v51 := by
    rw [hE52, show (53 : Nat) = E51.size from (hs51).symm, refBV_push_self]
    rfl
  have rb52 : refBV E52 28 = v26 := by
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, show (28 : Nat) = E26.size from (hs26).symm, refBV_push_self]
    rfl
  have hv52 : denoteExpr E52 (ResidualExpr.rand 8 #[53, 28]) = CertVal.bv v52 := by
    have hu : denoteExpr E52 (ResidualExpr.rand 8 #[53, 28]) = CertVal.bv (randV 8 [refBV E52 53, refBV E52 28]) := rfl
    rw [hu, ra52, rb52]
  rw [runBindings_step _ _ _ _ hv52]
  set E53 : SlotEnv := E52.push (CertVal.bv v52) with hE53
  have hs53 : E53.size = 55 := by rw [hE53, Array.size_push, hs52]
  have hag53 : ∀ j, j < E0.size → refBV E53 j = refBV E0 j :=
    fun j hj => by rw [hE53]; exact srcAgree_push _ (by omega) hag52 j hj
  clear ra52 rb52 hv52 hag52
  have ra53 : refBV E53 54 = v52 := by
    rw [hE53, show (54 : Nat) = E52.size from (hs52).symm, refBV_push_self]
    rfl
  have rb53 : refBV E53 28 = v26 := by
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, refBV_push_lt _ _ _ (by rw [hs27]; omega)]
    rw [hE27, show (28 : Nat) = E26.size from (hs26).symm, refBV_push_self]
    rfl
  have hv53 : denoteExpr E53 (ResidualExpr.rand 8 #[54, 28]) = CertVal.bv v53 := by
    have hu : denoteExpr E53 (ResidualExpr.rand 8 #[54, 28]) = CertVal.bv (randV 8 [refBV E53 54, refBV E53 28]) := rfl
    rw [hu, ra53, rb53]
  rw [runBindings_step _ _ _ _ hv53]
  set E54 : SlotEnv := E53.push (CertVal.bv v53) with hE54
  have hs54 : E54.size = 56 := by rw [hE54, Array.size_push, hs53]
  have hag54 : ∀ j, j < E0.size → refBV E54 j = refBV E0 j :=
    fun j hj => by rw [hE54]; exact srcAgree_push _ (by omega) hag53 j hj
  clear ra53 rb53 hv53 hag53
  have ra54 : refBV E54 55 = v53 := by
    rw [hE54, show (55 : Nat) = E53.size from (hs53).symm, refBV_push_self]
    rfl
  have rb54 : refBV E54 29 = v27 := by
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, show (29 : Nat) = E27.size from (hs27).symm, refBV_push_self]
    rfl
  have hv54 : denoteExpr E54 (ResidualExpr.rand 8 #[55, 29]) = CertVal.bv v54 := by
    have hu : denoteExpr E54 (ResidualExpr.rand 8 #[55, 29]) = CertVal.bv (randV 8 [refBV E54 55, refBV E54 29]) := rfl
    rw [hu, ra54, rb54]
  rw [runBindings_step _ _ _ _ hv54]
  set E55 : SlotEnv := E54.push (CertVal.bv v54) with hE55
  have hs55 : E55.size = 57 := by rw [hE55, Array.size_push, hs54]
  have hag55 : ∀ j, j < E0.size → refBV E55 j = refBV E0 j :=
    fun j hj => by rw [hE55]; exact srcAgree_push _ (by omega) hag54 j hj
  clear ra54 rb54 hv54 hag54
  have ra55 : refBV E55 56 = v54 := by
    rw [hE55, show (56 : Nat) = E54.size from (hs54).symm, refBV_push_self]
    rfl
  have rb55 : refBV E55 29 = v27 := by
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, refBV_push_lt _ _ _ (by rw [hs28]; omega)]
    rw [hE28, show (29 : Nat) = E27.size from (hs27).symm, refBV_push_self]
    rfl
  have hv55 : denoteExpr E55 (ResidualExpr.rand 8 #[56, 29]) = CertVal.bv v55 := by
    have hu : denoteExpr E55 (ResidualExpr.rand 8 #[56, 29]) = CertVal.bv (randV 8 [refBV E55 56, refBV E55 29]) := rfl
    rw [hu, ra55, rb55]
  rw [runBindings_step _ _ _ _ hv55]
  set E56 : SlotEnv := E55.push (CertVal.bv v55) with hE56
  have hs56 : E56.size = 58 := by rw [hE56, Array.size_push, hs55]
  have hag56 : ∀ j, j < E0.size → refBV E56 j = refBV E0 j :=
    fun j hj => by rw [hE56]; exact srcAgree_push _ (by omega) hag55 j hj
  clear ra55 rb55 hv55 hag55
  have ra56 : refBV E56 57 = v55 := by
    rw [hE56, show (57 : Nat) = E55.size from (hs55).symm, refBV_push_self]
    rfl
  have rb56 : refBV E56 30 = v28 := by
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, show (30 : Nat) = E28.size from (hs28).symm, refBV_push_self]
    rfl
  have hv56 : denoteExpr E56 (ResidualExpr.rand 8 #[57, 30]) = CertVal.bv v56 := by
    have hu : denoteExpr E56 (ResidualExpr.rand 8 #[57, 30]) = CertVal.bv (randV 8 [refBV E56 57, refBV E56 30]) := rfl
    rw [hu, ra56, rb56]
  rw [runBindings_step _ _ _ _ hv56]
  set E57 : SlotEnv := E56.push (CertVal.bv v56) with hE57
  have hs57 : E57.size = 59 := by rw [hE57, Array.size_push, hs56]
  have hag57 : ∀ j, j < E0.size → refBV E57 j = refBV E0 j :=
    fun j hj => by rw [hE57]; exact srcAgree_push _ (by omega) hag56 j hj
  clear ra56 rb56 hv56 hag56
  have ra57 : refBV E57 58 = v56 := by
    rw [hE57, show (58 : Nat) = E56.size from (hs56).symm, refBV_push_self]
    rfl
  have rb57 : refBV E57 30 = v28 := by
    rw [hE57, refBV_push_lt _ _ _ (by rw [hs56]; omega)]
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, refBV_push_lt _ _ _ (by rw [hs29]; omega)]
    rw [hE29, show (30 : Nat) = E28.size from (hs28).symm, refBV_push_self]
    rfl
  have hv57 : denoteExpr E57 (ResidualExpr.rand 8 #[58, 30]) = CertVal.bv v57 := by
    have hu : denoteExpr E57 (ResidualExpr.rand 8 #[58, 30]) = CertVal.bv (randV 8 [refBV E57 58, refBV E57 30]) := rfl
    rw [hu, ra57, rb57]
  rw [runBindings_step _ _ _ _ hv57]
  set E58 : SlotEnv := E57.push (CertVal.bv v57) with hE58
  have hs58 : E58.size = 60 := by rw [hE58, Array.size_push, hs57]
  have hag58 : ∀ j, j < E0.size → refBV E58 j = refBV E0 j :=
    fun j hj => by rw [hE58]; exact srcAgree_push _ (by omega) hag57 j hj
  clear ra57 rb57 hv57 hag57
  have ra58 : refBV E58 59 = v57 := by
    rw [hE58, show (59 : Nat) = E57.size from (hs57).symm, refBV_push_self]
    rfl
  have rb58 : refBV E58 31 = v29 := by
    rw [hE58, refBV_push_lt _ _ _ (by rw [hs57]; omega)]
    rw [hE57, refBV_push_lt _ _ _ (by rw [hs56]; omega)]
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, show (31 : Nat) = E29.size from (hs29).symm, refBV_push_self]
    rfl
  have hv58 : denoteExpr E58 (ResidualExpr.rand 8 #[59, 31]) = CertVal.bv v58 := by
    have hu : denoteExpr E58 (ResidualExpr.rand 8 #[59, 31]) = CertVal.bv (randV 8 [refBV E58 59, refBV E58 31]) := rfl
    rw [hu, ra58, rb58]
  rw [runBindings_step _ _ _ _ hv58]
  set E59 : SlotEnv := E58.push (CertVal.bv v58) with hE59
  have hs59 : E59.size = 61 := by rw [hE59, Array.size_push, hs58]
  have hag59 : ∀ j, j < E0.size → refBV E59 j = refBV E0 j :=
    fun j hj => by rw [hE59]; exact srcAgree_push _ (by omega) hag58 j hj
  clear ra58 rb58 hv58 hag58
  have ra59 : refBV E59 60 = v58 := by
    rw [hE59, show (60 : Nat) = E58.size from (hs58).symm, refBV_push_self]
    rfl
  have rb59 : refBV E59 31 = v29 := by
    rw [hE59, refBV_push_lt _ _ _ (by rw [hs58]; omega)]
    rw [hE58, refBV_push_lt _ _ _ (by rw [hs57]; omega)]
    rw [hE57, refBV_push_lt _ _ _ (by rw [hs56]; omega)]
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, refBV_push_lt _ _ _ (by rw [hs30]; omega)]
    rw [hE30, show (31 : Nat) = E29.size from (hs29).symm, refBV_push_self]
    rfl
  have hv59 : denoteExpr E59 (ResidualExpr.rand 8 #[60, 31]) = CertVal.bv v59 := by
    have hu : denoteExpr E59 (ResidualExpr.rand 8 #[60, 31]) = CertVal.bv (randV 8 [refBV E59 60, refBV E59 31]) := rfl
    rw [hu, ra59, rb59]
  rw [runBindings_step _ _ _ _ hv59]
  set E60 : SlotEnv := E59.push (CertVal.bv v59) with hE60
  have hs60 : E60.size = 62 := by rw [hE60, Array.size_push, hs59]
  have hag60 : ∀ j, j < E0.size → refBV E60 j = refBV E0 j :=
    fun j hj => by rw [hE60]; exact srcAgree_push _ (by omega) hag59 j hj
  clear ra59 rb59 hv59 hag59
  have ra60 : refBV E60 61 = v59 := by
    rw [hE60, show (61 : Nat) = E59.size from (hs59).symm, refBV_push_self]
    rfl
  have rb60 : refBV E60 32 = v30 := by
    rw [hE60, refBV_push_lt _ _ _ (by rw [hs59]; omega)]
    rw [hE59, refBV_push_lt _ _ _ (by rw [hs58]; omega)]
    rw [hE58, refBV_push_lt _ _ _ (by rw [hs57]; omega)]
    rw [hE57, refBV_push_lt _ _ _ (by rw [hs56]; omega)]
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, show (32 : Nat) = E30.size from (hs30).symm, refBV_push_self]
    rfl
  have hv60 : denoteExpr E60 (ResidualExpr.rand 8 #[61, 32]) = CertVal.bv v60 := by
    have hu : denoteExpr E60 (ResidualExpr.rand 8 #[61, 32]) = CertVal.bv (randV 8 [refBV E60 61, refBV E60 32]) := rfl
    rw [hu, ra60, rb60]
  rw [runBindings_step _ _ _ _ hv60]
  set E61 : SlotEnv := E60.push (CertVal.bv v60) with hE61
  have hs61 : E61.size = 63 := by rw [hE61, Array.size_push, hs60]
  have hag61 : ∀ j, j < E0.size → refBV E61 j = refBV E0 j :=
    fun j hj => by rw [hE61]; exact srcAgree_push _ (by omega) hag60 j hj
  clear ra60 rb60 hv60 hag60
  have ra61 : refBV E61 62 = v60 := by
    rw [hE61, show (62 : Nat) = E60.size from (hs60).symm, refBV_push_self]
    rfl
  have rb61 : refBV E61 32 = v30 := by
    rw [hE61, refBV_push_lt _ _ _ (by rw [hs60]; omega)]
    rw [hE60, refBV_push_lt _ _ _ (by rw [hs59]; omega)]
    rw [hE59, refBV_push_lt _ _ _ (by rw [hs58]; omega)]
    rw [hE58, refBV_push_lt _ _ _ (by rw [hs57]; omega)]
    rw [hE57, refBV_push_lt _ _ _ (by rw [hs56]; omega)]
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, refBV_push_lt _ _ _ (by rw [hs31]; omega)]
    rw [hE31, show (32 : Nat) = E30.size from (hs30).symm, refBV_push_self]
    rfl
  have hv61 : denoteExpr E61 (ResidualExpr.rand 8 #[62, 32]) = CertVal.bv v61 := by
    have hu : denoteExpr E61 (ResidualExpr.rand 8 #[62, 32]) = CertVal.bv (randV 8 [refBV E61 62, refBV E61 32]) := rfl
    rw [hu, ra61, rb61]
  rw [runBindings_step _ _ _ _ hv61]
  set E62 : SlotEnv := E61.push (CertVal.bv v61) with hE62
  have hs62 : E62.size = 64 := by rw [hE62, Array.size_push, hs61]
  have hag62 : ∀ j, j < E0.size → refBV E62 j = refBV E0 j :=
    fun j hj => by rw [hE62]; exact srcAgree_push _ (by omega) hag61 j hj
  clear ra61 rb61 hv61 hag61
  have ra62 : refBV E62 63 = v61 := by
    rw [hE62, show (63 : Nat) = E61.size from (hs61).symm, refBV_push_self]
    rfl
  have rb62 : refBV E62 33 = v31 := by
    rw [hE62, refBV_push_lt _ _ _ (by rw [hs61]; omega)]
    rw [hE61, refBV_push_lt _ _ _ (by rw [hs60]; omega)]
    rw [hE60, refBV_push_lt _ _ _ (by rw [hs59]; omega)]
    rw [hE59, refBV_push_lt _ _ _ (by rw [hs58]; omega)]
    rw [hE58, refBV_push_lt _ _ _ (by rw [hs57]; omega)]
    rw [hE57, refBV_push_lt _ _ _ (by rw [hs56]; omega)]
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, show (33 : Nat) = E31.size from (hs31).symm, refBV_push_self]
    rfl
  have hv62 : denoteExpr E62 (ResidualExpr.rand 8 #[63, 33]) = CertVal.bv v62 := by
    have hu : denoteExpr E62 (ResidualExpr.rand 8 #[63, 33]) = CertVal.bv (randV 8 [refBV E62 63, refBV E62 33]) := rfl
    rw [hu, ra62, rb62]
  rw [runBindings_step _ _ _ _ hv62]
  set E63 : SlotEnv := E62.push (CertVal.bv v62) with hE63
  have hs63 : E63.size = 65 := by rw [hE63, Array.size_push, hs62]
  have hag63 : ∀ j, j < E0.size → refBV E63 j = refBV E0 j :=
    fun j hj => by rw [hE63]; exact srcAgree_push _ (by omega) hag62 j hj
  clear ra62 rb62 hv62 hag62
  have ra63 : refBV E63 64 = v62 := by
    rw [hE63, show (64 : Nat) = E62.size from (hs62).symm, refBV_push_self]
    rfl
  have rb63 : refBV E63 33 = v31 := by
    rw [hE63, refBV_push_lt _ _ _ (by rw [hs62]; omega)]
    rw [hE62, refBV_push_lt _ _ _ (by rw [hs61]; omega)]
    rw [hE61, refBV_push_lt _ _ _ (by rw [hs60]; omega)]
    rw [hE60, refBV_push_lt _ _ _ (by rw [hs59]; omega)]
    rw [hE59, refBV_push_lt _ _ _ (by rw [hs58]; omega)]
    rw [hE58, refBV_push_lt _ _ _ (by rw [hs57]; omega)]
    rw [hE57, refBV_push_lt _ _ _ (by rw [hs56]; omega)]
    rw [hE56, refBV_push_lt _ _ _ (by rw [hs55]; omega)]
    rw [hE55, refBV_push_lt _ _ _ (by rw [hs54]; omega)]
    rw [hE54, refBV_push_lt _ _ _ (by rw [hs53]; omega)]
    rw [hE53, refBV_push_lt _ _ _ (by rw [hs52]; omega)]
    rw [hE52, refBV_push_lt _ _ _ (by rw [hs51]; omega)]
    rw [hE51, refBV_push_lt _ _ _ (by rw [hs50]; omega)]
    rw [hE50, refBV_push_lt _ _ _ (by rw [hs49]; omega)]
    rw [hE49, refBV_push_lt _ _ _ (by rw [hs48]; omega)]
    rw [hE48, refBV_push_lt _ _ _ (by rw [hs47]; omega)]
    rw [hE47, refBV_push_lt _ _ _ (by rw [hs46]; omega)]
    rw [hE46, refBV_push_lt _ _ _ (by rw [hs45]; omega)]
    rw [hE45, refBV_push_lt _ _ _ (by rw [hs44]; omega)]
    rw [hE44, refBV_push_lt _ _ _ (by rw [hs43]; omega)]
    rw [hE43, refBV_push_lt _ _ _ (by rw [hs42]; omega)]
    rw [hE42, refBV_push_lt _ _ _ (by rw [hs41]; omega)]
    rw [hE41, refBV_push_lt _ _ _ (by rw [hs40]; omega)]
    rw [hE40, refBV_push_lt _ _ _ (by rw [hs39]; omega)]
    rw [hE39, refBV_push_lt _ _ _ (by rw [hs38]; omega)]
    rw [hE38, refBV_push_lt _ _ _ (by rw [hs37]; omega)]
    rw [hE37, refBV_push_lt _ _ _ (by rw [hs36]; omega)]
    rw [hE36, refBV_push_lt _ _ _ (by rw [hs35]; omega)]
    rw [hE35, refBV_push_lt _ _ _ (by rw [hs34]; omega)]
    rw [hE34, refBV_push_lt _ _ _ (by rw [hs33]; omega)]
    rw [hE33, refBV_push_lt _ _ _ (by rw [hs32]; omega)]
    rw [hE32, show (33 : Nat) = E31.size from (hs31).symm, refBV_push_self]
    rfl
  have hv63 : denoteExpr E63 (ResidualExpr.rand 8 #[64, 33]) = CertVal.bv v63 := by
    have hu : denoteExpr E63 (ResidualExpr.rand 8 #[64, 33]) = CertVal.bv (randV 8 [refBV E63 64, refBV E63 33]) := rfl
    rw [hu, ra63, rb63]
  rw [runBindings_step _ _ _ _ hv63]
  set E64 : SlotEnv := E63.push (CertVal.bv v63) with hE64
  have hs64 : E64.size = 66 := by rw [hE64, Array.size_push, hs63]
  have hag64 : ∀ j, j < E0.size → refBV E64 j = refBV E0 j :=
    fun j hj => by rw [hE64]; exact srcAgree_push _ (by omega) hag63 j hj
  clear ra63 rb63 hv63 hag63
  clear_value E64 E63 E62 E61 E60 E59 E58 E57 E56 E55 E54 E53 E52 E51 E50 E49 E48 E47 E46 E45 E44 E43 E42 E41 E40 E39 E38 E37 E36 E35 E34 E33 E32 E31 E30 E29 E28 E27 E26 E25 E24 E23 E22 E21 E20 E19 E18 E17 E16 E15 E14 E13 E12 E11 E10 E9 E8 E7 E6 E5 E4 E3 E2 E1 E0
  simp only [runBindings]
  have rout : refBV E64 65 = v63 := by
    rw [hE64, show (65 : Nat) = E63.size from (hs63).symm, refBV_push_self]
    rfl
  simp only [Array.map_singleton, rout]
  simp only [Array.map_empty, Array.mapIdx_empty]
  rfl
