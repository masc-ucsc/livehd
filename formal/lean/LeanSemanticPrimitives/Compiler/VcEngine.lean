/-
# vc's engines

vc compiles a certificate to a residual program `R` once (`compileDesign`, then the optimizer)
and steps it. Its modes differ in what they keep between steps:
- `vcEngine` (the default): memories as tables (`ResidualFlat`) and the last step's slot
  environment; each step after the first is event-driven (`evEnvF`, which compiled code runs as
  `evEnvFW`, skipping chunks with nothing pending: `ResidualWork`);
- `vcEvFunEngine`: the same event-driven step with memories as functions (`ResidualEv.evStep`);
- `vcScanEngine`: the incremental step that visits every binding (`ResidualInc.incStep`);
- `vcFullEngine`: the whole `denoteResidual` every step.

Each is proved sound against `denoteResidual R` (`Engine.Sound`), for a program in which every
binding reads only earlier slots (`wellOrdered`) where a mode needs that. The invariant of the
incremental modes is that the stored environment is SOME step's full environment (or that no
step ran yet): `evEnv_correct` and `incEnv_correct` need nothing more, so a loop may step twice
from one state. `CompileOpt` closes these against `interpretDesign`.

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.Engine
import LeanSemanticPrimitives.Compiler.ResidualWork

namespace Compiler

open ResidualInc ResidualFlat

/-- vc's default state: memories as tables, the last step's environment, whether a step ran. -/
structure VcState where
  tabs    : RuntimeStateF
  env     : SlotEnvF
  started : Bool

/-- vc's default engine: memories as tables over each memory's `depth` words, the first step full,
each later one event-driven with the program's fanout table `fan`. -/
def vcEngine (R : ResidualProgram) (fan : Array (Array Nat)) (depth : Nat → Nat) : Engine VcState where
  name := "vc"
  load s := { tabs := ofState depth s, env := #[], started := false }
  -- `q` is taken apart first, so the old state is gone before the environment and the tables
  -- are updated: they stay unshared and are updated in place
  step e i q :=
    match q with
    | ⟨tabs, env0, started⟩ =>
      let env := if started then evEnvF R fan env0 i tabs else fullEnvF R i tabs
      let r := stepOfEnvF R env e tabs
      .ok (r.1, { tabs := r.2, env, started := true })
  view q := absS q.tabs

/-- **vc's default engine is `denoteResidual`**, step for step, for a well-ordered program with its
fanout table. -/
theorem vcEngine_sound {R : ResidualProgram} (hw : wellOrdered R = true) (depth : Nat → Nat) :
    (vcEngine R (fanout R) depth).Sound (denoteResidual R) := by
  refine ⟨fun q => q.started = false ∨ ∃ i' s', absEnv q.env = fullEnv R i' s', ?_, ?_⟩
  · intro s
    exact ⟨Or.inl rfl, absS_ofState depth s⟩
  · intro q hq e i o q' hs
    obtain ⟨tabs, env0, started⟩ := q
    simp only at hq
    simp only [vcEngine] at hs
    cases hs
    have henv : absEnv (if started then evEnvF R (fanout R) env0 i tabs else fullEnvF R i tabs)
        = fullEnv R i (absS tabs) := by
      cases hst : started
      · simp only [Bool.false_eq_true, ite_false]
        exact fullEnvF_abs R i tabs
      · simp only [ite_true]
        rcases hq with h | ⟨i', s', h⟩
        · rw [hst] at h; cases h
        · rw [evEnvF_abs, h, evEnv_correct hw (fanout_ok R)]
    have hres := stepOfEnvF_abs R (if started then evEnvF R (fanout R) env0 i tabs
      else fullEnvF R i tabs) e tabs
    rw [henv, ← denoteResidual_eq_stepOfEnv] at hres
    refine ⟨Or.inr ⟨i, absS tabs, henv⟩, ?_, ?_⟩
    · show _ = (denoteResidual R e i (absS tabs)).outputs
      rw [← hres]; rfl
    · show absS _ = (denoteResidual R e i (absS tabs)).nextState
      rw [← hres]; rfl

/-- The state of the modes that keep memories as functions. -/
structure VcFunState where
  s       : RuntimeState
  env     : SlotEnv
  started : Bool

/-- The event-driven step with memories as functions (`evStep`). -/
def vcEvFunEngine (R : ResidualProgram) (fan : Array (Array Nat)) : Engine VcFunState where
  name := "vc"
  load s := { s, env := #[], started := false }
  step e i q :=
    match q with
    | ⟨s, env0, started⟩ =>
      let env := if started then evEnv R fan env0 i s else fullEnv R i s
      let r := stepOfEnv R env e s
      .ok (r.outputs, { s := r.nextState, env, started := true })
  view q := q.s

/-- The incremental step that visits every binding (`incStep`). -/
def vcScanEngine (R : ResidualProgram) : Engine VcFunState where
  name := "vc"
  load s := { s, env := #[], started := false }
  step e i q :=
    match q with
    | ⟨s, env0, started⟩ =>
      let env := if started then incEnv R env0 i s else fullEnv R i s
      let r := stepOfEnv R env e s
      .ok (r.outputs, { s := r.nextState, env, started := true })
  view q := q.s

/-- The whole `denoteResidual` every step. -/
def vcFullEngine (R : ResidualProgram) : Engine RuntimeState :=
  Engine.ofStep "vc" fun e i s => .ok (denoteResidual R e i s)

/-- The two function-memory modes share one proof: the environment each step makes is the full
environment of that step. -/
theorem vcFun_sound {R : ResidualProgram} (E : Engine VcFunState)
    (hE : ∀ s, E.load s = { s, env := #[], started := false })
    (hv : ∀ q, E.view q = q.s)
    (hstep : ∀ q e i o q', E.step e i q = .ok (o, q') →
      (q.started = true → ∃ i' s', q.env = fullEnv R i' s') →
      q'.env = fullEnv R i q.s ∧ q'.started = true ∧
      o = (stepOfEnv R q'.env e q.s).outputs ∧ q'.s = (stepOfEnv R q'.env e q.s).nextState) :
    E.Sound (denoteResidual R) := by
  refine ⟨fun q => q.started = false ∨ ∃ i' s', q.env = fullEnv R i' s', ?_, ?_⟩
  · intro s
    rw [hE, hv]
    exact ⟨Or.inl rfl, rfl⟩
  · intro q hq e i o q' hs
    have hpre : q.started = true → ∃ i' s', q.env = fullEnv R i' s' := by
      intro hst
      rcases hq with h | h
      · rw [hst] at h; cases h
      · exact h
    obtain ⟨henv, hst', ho, hs'⟩ := hstep q e i o q' hs hpre
    rw [hv, hv]
    refine ⟨Or.inr ⟨i, q.s, henv⟩, ?_, ?_⟩
    · rw [ho, henv, ← denoteResidual_eq_stepOfEnv]
    · rw [hs', henv, ← denoteResidual_eq_stepOfEnv]

theorem vcEvFunEngine_sound {R : ResidualProgram} (hw : wellOrdered R = true) :
    (vcEvFunEngine R (fanout R)).Sound (denoteResidual R) := by
  apply vcFun_sound (vcEvFunEngine R (fanout R)) (fun _ => rfl) (fun _ => rfl)
  intro q e i o q' hs hpre
  obtain ⟨s, env0, started⟩ := q
  simp only [vcEvFunEngine] at hs
  cases hs
  refine ⟨?_, rfl, rfl, rfl⟩
  show (if started = true then evEnv R (fanout R) env0 i s else fullEnv R i s) = fullEnv R i s
  cases hst : started
  · rfl
  · obtain ⟨i', s', h⟩ := hpre hst
    simp only [ite_true]
    rw [show env0 = fullEnv R i' s' from h, evEnv_correct hw (fanout_ok R)]

theorem vcScanEngine_sound {R : ResidualProgram} (hw : wellOrdered R = true) :
    (vcScanEngine R).Sound (denoteResidual R) := by
  apply vcFun_sound (vcScanEngine R) (fun _ => rfl) (fun _ => rfl)
  intro q e i o q' hs hpre
  obtain ⟨s, env0, started⟩ := q
  simp only [vcScanEngine] at hs
  cases hs
  refine ⟨?_, rfl, rfl, rfl⟩
  show (if started = true then incEnv R env0 i s else fullEnv R i s) = fullEnv R i s
  cases hst : started
  · rfl
  · obtain ⟨i', s', h⟩ := hpre hst
    simp only [ite_true]
    rw [show env0 = fullEnv R i' s' from h, incEnv_correct hw]

theorem vcFullEngine_sound (R : ResidualProgram) : (vcFullEngine R).Sound (denoteResidual R) := by
  apply Engine.ofStep_sound
  intro e i s r h
  cases h; rfl

end Compiler

#print axioms Compiler.vcEngine_sound
#print axioms Compiler.vcEvFunEngine_sound
#print axioms Compiler.vcScanEngine_sound
