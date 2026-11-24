/-
# The optimized compiled program is `interpretDesign`

`compileDesign_correct` ties vc's compiled program to the reference semantics;
`ResidualOpt.optimize_correct` says the optimizer never changes what that program computes.
Together: running the OPTIMIZED program is `interpretDesign`, on every step, from every state
and input, with the same hypothesis as `compileDesign_correct` alone.

`runIncOpt_correct` adds the incremental step (`ResidualInc`): a whole run of the optimized
program, each step after the first reusing the last step's unchanged values, is
`interpretDesign`'s run. `runEvOpt_correct` says the same of the event-driven step
(`ResidualEv`), which visits only the bindings a changed slot's readers reach, and
`runEvFlatOpt_correct` of that step with memories as tables (`ResidualFlat`). The
`*_interp` theorems say each of vc's engines (`Compiler/VcEngine`) is `interpretDesign`'s, step for
step (`Engine.Sound`), so a loop over any engine (`Engine.run_sound`) runs `interpretDesign`.

Mathlib side, because `compileDesign_correct` is.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.ResidualOptSound
import LeanSemanticPrimitives.Compiler.ResidualInc
import LeanSemanticPrimitives.Compiler.ResidualEv
import LeanSemanticPrimitives.Compiler.ResidualFlat
import LeanSemanticPrimitives.Compiler.VcEngine

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

/-- **vc's event-driven run with memories as tables is `interpretDesign`'s run** from the state
the tables stand for (`ResidualFlat.absS`; `absS_ofState`: tables built from a state stand for it). -/
theorem runEvFlatOpt_correct (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) (hw : ResidualInc.wellOrdered (ResidualOpt.optimize R) = true) :
    ∀ (s : ResidualFlat.RuntimeStateF) (ticks : List (ClockEdges × RuntimeInput)),
      (ResidualFlat.runEvF (ResidualOpt.optimize R) s ticks).map ResidualFlat.absRes
        = runInterp D (ResidualFlat.absS s) ticks := by
  intro s ticks
  rw [ResidualFlat.runEvF_correct hw, ← ResidualInc.runInc_eq_runFull hw]
  exact runIncOpt_correct D R hc hw _ ticks

/-! ## vc's engines are `interpretDesign`'s -/

/-- vc's default engine on the optimized program. -/
theorem vcEngine_sound_interp (D : DesignCert) (R : ResidualProgram) (hc : compileDesign D = .ok R)
    (hw : ResidualInc.wellOrdered (ResidualOpt.optimize R) = true) (depth : Nat → Nat) :
    (vcEngine (ResidualOpt.optimize R) (ResidualInc.fanout (ResidualOpt.optimize R)) depth).Sound
      (interpretDesign D) :=
  (vcEngine_sound hw depth).of_ref_eq (compileOpt_correct D R hc)

/-- vc's default engine on the unoptimized program (`opt:off`). -/
theorem vcEngine_sound_interp_noopt (D : DesignCert) (R : ResidualProgram) (hc : compileDesign D = .ok R)
    (hw : ResidualInc.wellOrdered R = true) (depth : Nat → Nat) :
    (vcEngine R (ResidualInc.fanout R) depth).Sound (interpretDesign D) :=
  (vcEngine_sound hw depth).of_ref_eq (compileDesign_correct D R hc)

/-- The event-driven engine with memories as functions, on the optimized program. -/
theorem vcEvFunEngine_sound_interp (D : DesignCert) (R : ResidualProgram) (hc : compileDesign D = .ok R)
    (hw : ResidualInc.wellOrdered (ResidualOpt.optimize R) = true) :
    (vcEvFunEngine (ResidualOpt.optimize R) (ResidualInc.fanout (ResidualOpt.optimize R))).Sound
      (interpretDesign D) :=
  (vcEvFunEngine_sound hw).of_ref_eq (compileOpt_correct D R hc)

/-- The scan engine, on the optimized program. -/
theorem vcScanEngine_sound_interp (D : DesignCert) (R : ResidualProgram) (hc : compileDesign D = .ok R)
    (hw : ResidualInc.wellOrdered (ResidualOpt.optimize R) = true) :
    (vcScanEngine (ResidualOpt.optimize R)).Sound (interpretDesign D) :=
  (vcScanEngine_sound hw).of_ref_eq (compileOpt_correct D R hc)

/-- The full-step engine, on the optimized program (no well-ordering needed). -/
theorem vcFullEngine_sound_interp (D : DesignCert) (R : ResidualProgram) (hc : compileDesign D = .ok R) :
    (vcFullEngine (ResidualOpt.optimize R)).Sound (interpretDesign D) :=
  (vcFullEngine_sound _).of_ref_eq (compileOpt_correct D R hc)

end Compiler

#print axioms Compiler.compileOpt_correct
#print axioms Compiler.runIncOpt_correct
#print axioms Compiler.runEvOpt_correct
#print axioms Compiler.runEvFlatOpt_correct
#print axioms Compiler.vcEngine_sound_interp
