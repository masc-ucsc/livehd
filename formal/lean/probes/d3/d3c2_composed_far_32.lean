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
    ]
    outputs  := #[{ slot := 33, width := 8 }]
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
  { outputs := #[bv_resize 8 v31]
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
  clear_value E32 E31 E30 E29 E28 E27 E26 E25 E24 E23 E22 E21 E20 E19 E18 E17 E16 E15 E14 E13 E12 E11 E10 E9 E8 E7 E6 E5 E4 E3 E2 E1 E0
  simp only [runBindings]
  have rout : refBV E32 33 = v31 := by
    rw [hE32, show (33 : Nat) = E31.size from (hs31).symm, refBV_push_self]
    rfl
  simp only [Array.map_singleton, rout]
  simp only [Array.map_empty, Array.mapIdx_empty]
  rfl
