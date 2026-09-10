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
  simp [fast, denoteResidual, R, runBindings, sourceEnvArr, denoteExpr,
    refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
