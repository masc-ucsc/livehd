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

def fast (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let s0 := bv_resize 8 (i[0]?.getD (mk_bv 8 0))
  let s1 := bv_resize 8 (i[1]?.getD (mk_bv 8 0))
  let v0 := randV 8 [s0, s1]
  let v1 := randV 8 [v0, s1]
  let v2 := randV 8 [v1, s1]
  let v3 := randV 8 [v2, s1]
  let v4 := randV 8 [v3, s1]
  let v5 := randV 8 [v4, s1]
  let v6 := randV 8 [v5, s1]
  let v7 := randV 8 [v6, s1]
  { outputs := #[bv_resize 8 v7]
    nextState := { flops := #[], mems := #[] } }

theorem fast_correct : ∀ i s, fast i s = denoteResidual R i s := by
  intro i s
  set s0 : BV := bv_resize 8 (i[0]?.getD (mk_bv 8 0)) with hsd0
  set s1 : BV := bv_resize 8 (i[1]?.getD (mk_bv 8 0)) with hsd1
  set v0 : BV := randV 8 [s0, s1] with hvd0
  set v1 : BV := randV 8 [v0, s1] with hvd1
  set v2 : BV := randV 8 [v1, s1] with hvd2
  set v3 : BV := randV 8 [v2, s1] with hvd3
  set v4 : BV := randV 8 [v3, s1] with hvd4
  set v5 : BV := randV 8 [v4, s1] with hvd5
  set v6 : BV := randV 8 [v5, s1] with hvd6
  set v7 : BV := randV 8 [v6, s1] with hvd7
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
  have rb1 : refBV E1 1 = s1 := by
    rw [hag1 1 (by rw [hs0]; omega), hb1]
  have hv1 : denoteExpr E1 (ResidualExpr.rand 8 #[2, 1]) = CertVal.bv v1 := by
    have hu : denoteExpr E1 (ResidualExpr.rand 8 #[2, 1]) = CertVal.bv (randV 8 [refBV E1 2, refBV E1 1]) := rfl
    rw [hu, ra1, rb1]
  rw [runBindings_step _ _ _ _ hv1]
  set E2 : SlotEnv := E1.push (CertVal.bv v1) with hE2
  have hs2 : E2.size = 4 := by rw [hE2, Array.size_push, hs1]
  have hag2 : ∀ j, j < E0.size → refBV E2 j = refBV E0 j :=
    fun j hj => by rw [hE2]; exact srcAgree_push _ (by omega) hag1 j hj
  clear ra1 rb1 hv1 hag1
  have ra2 : refBV E2 3 = v1 := by
    rw [hE2, show (3 : Nat) = E1.size from (hs1).symm, refBV_push_self]
    rfl
  have rb2 : refBV E2 1 = s1 := by
    rw [hag2 1 (by rw [hs0]; omega), hb1]
  have hv2 : denoteExpr E2 (ResidualExpr.rand 8 #[3, 1]) = CertVal.bv v2 := by
    have hu : denoteExpr E2 (ResidualExpr.rand 8 #[3, 1]) = CertVal.bv (randV 8 [refBV E2 3, refBV E2 1]) := rfl
    rw [hu, ra2, rb2]
  rw [runBindings_step _ _ _ _ hv2]
  set E3 : SlotEnv := E2.push (CertVal.bv v2) with hE3
  have hs3 : E3.size = 5 := by rw [hE3, Array.size_push, hs2]
  have hag3 : ∀ j, j < E0.size → refBV E3 j = refBV E0 j :=
    fun j hj => by rw [hE3]; exact srcAgree_push _ (by omega) hag2 j hj
  clear ra2 rb2 hv2 hag2
  have ra3 : refBV E3 4 = v2 := by
    rw [hE3, show (4 : Nat) = E2.size from (hs2).symm, refBV_push_self]
    rfl
  have rb3 : refBV E3 1 = s1 := by
    rw [hag3 1 (by rw [hs0]; omega), hb1]
  have hv3 : denoteExpr E3 (ResidualExpr.rand 8 #[4, 1]) = CertVal.bv v3 := by
    have hu : denoteExpr E3 (ResidualExpr.rand 8 #[4, 1]) = CertVal.bv (randV 8 [refBV E3 4, refBV E3 1]) := rfl
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
  have rb4 : refBV E4 1 = s1 := by
    rw [hag4 1 (by rw [hs0]; omega), hb1]
  have hv4 : denoteExpr E4 (ResidualExpr.rand 8 #[5, 1]) = CertVal.bv v4 := by
    have hu : denoteExpr E4 (ResidualExpr.rand 8 #[5, 1]) = CertVal.bv (randV 8 [refBV E4 5, refBV E4 1]) := rfl
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
  have rb5 : refBV E5 1 = s1 := by
    rw [hag5 1 (by rw [hs0]; omega), hb1]
  have hv5 : denoteExpr E5 (ResidualExpr.rand 8 #[6, 1]) = CertVal.bv v5 := by
    have hu : denoteExpr E5 (ResidualExpr.rand 8 #[6, 1]) = CertVal.bv (randV 8 [refBV E5 6, refBV E5 1]) := rfl
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
  have rb6 : refBV E6 1 = s1 := by
    rw [hag6 1 (by rw [hs0]; omega), hb1]
  have hv6 : denoteExpr E6 (ResidualExpr.rand 8 #[7, 1]) = CertVal.bv v6 := by
    have hu : denoteExpr E6 (ResidualExpr.rand 8 #[7, 1]) = CertVal.bv (randV 8 [refBV E6 7, refBV E6 1]) := rfl
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
  have rb7 : refBV E7 1 = s1 := by
    rw [hag7 1 (by rw [hs0]; omega), hb1]
  have hv7 : denoteExpr E7 (ResidualExpr.rand 8 #[8, 1]) = CertVal.bv v7 := by
    have hu : denoteExpr E7 (ResidualExpr.rand 8 #[8, 1]) = CertVal.bv (randV 8 [refBV E7 8, refBV E7 1]) := rfl
    rw [hu, ra7, rb7]
  rw [runBindings_step _ _ _ _ hv7]
  set E8 : SlotEnv := E7.push (CertVal.bv v7) with hE8
  have hs8 : E8.size = 10 := by rw [hE8, Array.size_push, hs7]
  have hag8 : ∀ j, j < E0.size → refBV E8 j = refBV E0 j :=
    fun j hj => by rw [hE8]; exact srcAgree_push _ (by omega) hag7 j hj
  clear ra7 rb7 hv7 hag7
  simp only [runBindings]
  have rout : refBV E8 9 = v7 := by
    rw [hE8, show (9 : Nat) = E7.size from (hs7).symm, refBV_push_self]
    rfl
  simp only [Array.map_singleton, rout]
  simp only [Array.map_empty, Array.mapIdx_empty]
  rfl
