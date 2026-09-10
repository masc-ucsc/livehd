import LeanSemanticPrimitives.Compiler.CompileDesign
set_option maxRecDepth 1000000
set_option maxHeartbeats 0
open Compiler Compiler.Residual

def R2 : ResidualProgram :=
  { sources  := #[SourceDesc.input 0 8, SourceDesc.input 1 8]
    bindings := #[
    { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[0, 1] }
  ,     { ty := ValueType.bv 8, rhs := ResidualExpr.rand 8 #[2, 1] }
    ]
    outputs  := #[{ slot := 3, width := 8 }]
    flopUpdates := #[], memoryUpdates := #[] }

def fast2 (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let s0 := bv_resize 8 (i[0]?.getD (mk_bv 8 0))
  let s1 := bv_resize 8 (i[1]?.getD (mk_bv 8 0))
  let n0 := randV 8 [s0, s1]
  let n1 := randV 8 [n0, s1]
  { outputs := #[bv_resize 8 n1]
    nextState := { flops := #[], mems := #[] } }

theorem fast2_correct : ∀ i s, fast2 i s = denoteResidual R2 i s := by
  intro i s
  simp [fast2, denoteResidual, R2, runBindings, sourceEnvArr, denoteExpr, refBVs, refBV, denoteRef, sourceValue, CertVal.asBV]
