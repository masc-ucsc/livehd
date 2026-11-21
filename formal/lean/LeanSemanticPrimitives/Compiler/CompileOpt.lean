/-
# The optimized compiled program is `interpretDesign`

`compileDesign_correct` ties vc's compiled program to the reference semantics;
`ResidualOpt.optimize_correct` says the optimizer never changes what that program computes.
Together: running the OPTIMIZED program is `interpretDesign`, on every step, from every state
and input, with the same hypothesis as `compileDesign_correct` alone.

Mathlib side, because `compileDesign_correct` is.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.ResidualOptSound

namespace Compiler

theorem compileOpt_correct (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) :
    ∀ (e : ClockEdges) (inp : RuntimeInput) (st : RuntimeState),
      denoteResidual (ResidualOpt.optimize R) e inp st = interpretDesign D e inp st := by
  intro e inp st
  rw [ResidualOpt.optimize_correct]
  exact compileDesign_correct D R hc e inp st

end Compiler

#print axioms Compiler.compileOpt_correct
