/-
# The optimized compiled program is `interpretDesign`

`compileDesign_correct` ties vc's compiled program to the reference semantics;
`ResidualOpt.optimize_correct` says the optimizer never changes what that program computes.
Together: running the OPTIMIZED program is `interpretDesign`, on every step, from every state
and input, with the same hypothesis as `compileDesign_correct` alone.

`runIncOpt_correct` adds the incremental step (`ResidualInc`): a whole run of the optimized
program, each step after the first reusing the last step's unchanged values, is
`interpretDesign`'s run. `runEvOpt_correct` says the same of the event-driven step
(`ResidualEv`), which visits only the bindings a changed slot's readers reach.

Mathlib side, because `compileDesign_correct` is.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.ResidualOptSound
import LeanSemanticPrimitives.Compiler.ResidualInc
import LeanSemanticPrimitives.Compiler.ResidualEv

namespace Compiler

theorem compileOpt_correct (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) :
    ∀ (e : ClockEdges) (inp : RuntimeInput) (st : RuntimeState),
      denoteResidual (ResidualOpt.optimize R) e inp st = interpretDesign D e inp st := by
  intro e inp st
  rw [ResidualOpt.optimize_correct]
  exact compileDesign_correct D R hc e inp st

/-- Steps of `interpretDesign`, each from the state the one before left. -/
def runInterp (D : DesignCert) : RuntimeState → List (ClockEdges × RuntimeInput) →
    List RuntimeResult
  | _, [] => []
  | s, (e, i) :: rest =>
    let r := interpretDesign D e i s
    r :: runInterp D r.nextState rest

/-- **vc's incremental run is `interpretDesign`'s run**: the optimized program, its first step
full and every later step incremental (`ResidualInc.runInc`, reusing the last step's value of
every binding whose operands kept theirs), gives `interpretDesign`'s result on every step, when
the optimized program reads only earlier slots (`wellOrdered`, which the driver checks once). -/
theorem runIncOpt_correct (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) (hw : ResidualInc.wellOrdered (ResidualOpt.optimize R) = true) :
    ∀ (s : RuntimeState) (ticks : List (ClockEdges × RuntimeInput)),
      ResidualInc.runInc (ResidualOpt.optimize R) s ticks = runInterp D s ticks := by
  intro s ticks
  rw [ResidualInc.runInc_eq_runFull hw]
  induction ticks generalizing s with
  | nil => rfl
  | cons t rest ih =>
    obtain ⟨e, i⟩ := t
    simp only [ResidualInc.runFull, runInterp, compileOpt_correct D R hc e i s]
    rw [ih]

/-- **vc's event-driven run is `interpretDesign`'s run.** As `runIncOpt_correct`, with each step
after the first visiting only the bindings the changed slots' readers reach
(`ResidualInc.runEv`, with the program's fanout table). -/
theorem runEvOpt_correct (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) (hw : ResidualInc.wellOrdered (ResidualOpt.optimize R) = true) :
    ∀ (s : RuntimeState) (ticks : List (ClockEdges × RuntimeInput)),
      ResidualInc.runEv (ResidualOpt.optimize R) s ticks = runInterp D s ticks := by
  intro s ticks
  rw [ResidualInc.runEv_eq_runFull hw, ← ResidualInc.runInc_eq_runFull hw]
  exact runIncOpt_correct D R hc hw s ticks

end Compiler

#print axioms Compiler.compileOpt_correct
#print axioms Compiler.runIncOpt_correct
#print axioms Compiler.runEvOpt_correct
