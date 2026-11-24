/-
# vc's engines

vc compiles a certificate to a residual program `R` once (`compileDesign`, then the optimizer)
and steps it. Its modes differ in what they keep between steps:
- `vcEngine` (the default, and the fastest): memories as tables (`ResidualFlat`) and the last
  step's slot environment; each step after the first is event-driven (`evEnvF`, which compiled
  code runs as `evEnvFW`, skipping chunks with nothing pending: `ResidualWork`);
- `vcEvFunEngine` (OLDER, SLOWER): the same event-driven step with memories as functions
  (`ResidualEv.evStep`);
- `vcScanEngine` (OLDER, SLOWER): the incremental step that visits every binding
  (`ResidualInc.incStep`);
- `vcFullEngine` (OLDER, SLOWEST): the whole `denoteResidual` every step; also the fallback for a
  program that is not well-ordered.
The older three are the steps vc ran before `vcEngine`, kept selectable (`mem:fun`, `inc:scan`,
`inc:off`) so earlier runs can be reproduced and compared; nothing chooses them by default.
Measured per cycle on SoC (cert_trace, 512 cycles, 2026-11-24): `vcEngine` 5.2 ms,
`vcEvFunEngine` 7.6 ms, `vcScanEngine` 16.8 ms, `vcFullEngine` 134 ms; every trace the same.

Each is proved sound against `denoteResidual R` (`Engine.Sound`), for a program in which every
binding reads only earlier slots (`wellOrdered`) where a mode needs that. The invariant of the
incremental modes is that the stored environment is SOME step's full environment (or that no
step ran yet): `evEnv_correct` and `incEnv_correct` need nothing more, so a loop may step twice
from one state. The same invariant lets them `reload` (compaction: new memories, the environment
kept) and `peek` (the environment for the given inputs and its outputs, no edge: the next step
starts from it). `CompileOpt` closes these against `interpretDesign`.

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.Engine
import LeanSemanticPrimitives.Compiler.ResidualWork

namespace Compiler

open ResidualInc ResidualFlat

/-- The outputs an environment gives (the first part of `stepOfEnvF`). -/
def outputsOfEnvF (R : ResidualProgram) (env : SlotEnvF) : Array BV :=
  R.outputs.map fun o => bv_resize o.width (refBVF env o.slot)

theorem stepOfEnvF_fst (R : ResidualProgram) (env : SlotEnvF) (e : ClockEdges) (s : RuntimeStateF) :
    (stepOfEnvF R env e s).1 = outputsOfEnvF R env := rfl

/-- The outputs an environment gives (`stepOfEnv`'s outputs). -/
def outputsOfEnv (R : ResidualProgram) (env : SlotEnv) : Array BV :=
  R.outputs.map fun o => bv_resize o.width (refBV env o.slot)

theorem stepOfEnv_outputs (R : ResidualProgram) (env : SlotEnv) (e : ClockEdges) (s : RuntimeState) :
    (stepOfEnv R env e s).outputs = outputsOfEnv R env := rfl

/-- vc's default state: memories as tables, the last step's environment, whether a step ran. -/
structure VcState where
  tabs    : RuntimeStateF
  env     : SlotEnvF
  started : Bool

/-- The environment for inputs `i` on tables `tabs`: full before any step, then event-driven from
the last one. -/
@[inline] def vcEnvF (R : ResidualProgram) (fan : Array (Array Nat)) (env0 : SlotEnvF) (started : Bool)
    (i : RuntimeInput) (tabs : RuntimeStateF) : SlotEnvF :=
  if started then evEnvF R fan env0 i tabs else fullEnvF R i tabs

/-- vc's default engine: memories as tables over each memory's `depth` words, the first step full,
each later one event-driven with the program's fanout table `fan`. Each operation takes `q` apart
first, so the old state is gone before the environment and the tables are updated: they stay
unshared and are updated in place. -/
def vcEngine (R : ResidualProgram) (fan : Array (Array Nat)) (depth : Nat → Nat) : Engine VcState where
  name := "vc"
  load s := { tabs := ofState depth s, env := #[], started := false }
  step e i q :=
    match q with
    | ⟨tabs, env0, started⟩ =>
      let env := vcEnvF R fan env0 started i tabs
      let r := stepOfEnvF R env e tabs
      .ok (r.1, { tabs := r.2, env, started := true })
  view q := absS q.tabs
  reload q s :=
    match q with
    | ⟨_, env, started⟩ => { tabs := ofState depth s, env, started }
  peek? := some fun _ i q =>
    match q with
    | ⟨tabs, env0, started⟩ =>
      let env := vcEnvF R fan env0 started i tabs
      .ok (outputsOfEnvF R env, { tabs, env, started := true })

/-- vc's invariant on tables: no step ran, or the environment is some step's full one. -/
def VcInv (R : ResidualProgram) (q : VcState) : Prop :=
  q.started = false ∨ ∃ i' s', absEnv q.env = fullEnv R i' s'

theorem vcEnvF_abs {R : ResidualProgram} (hw : wellOrdered R = true) {env0 : SlotEnvF}
    {started : Bool} (h : started = false ∨ ∃ i' s', absEnv env0 = fullEnv R i' s')
    (i : RuntimeInput) (tabs : RuntimeStateF) :
    absEnv (vcEnvF R (fanout R) env0 started i tabs) = fullEnv R i (absS tabs) := by
  unfold vcEnvF
  cases hst : started
  · simp only [Bool.false_eq_true, ite_false]
    exact fullEnvF_abs R i tabs
  · simp only [ite_true]
    rcases h with h | ⟨i', s', h⟩
    · rw [hst] at h; cases h
    · rw [evEnvF_abs, h, evEnv_correct hw (fanout_ok R)]

/-- A table step from step `i`'s full environment is `denoteResidual`'s step. -/
theorem denote_of_envF {R : ResidualProgram} {env : SlotEnvF} {i : RuntimeInput} {tabs : RuntimeStateF}
    (h : absEnv env = fullEnv R i (absS tabs)) (e : ClockEdges) :
    denoteResidual R e i (absS tabs) = absRes (stepOfEnvF R env e tabs) := by
  rw [denoteResidual_eq_stepOfEnv, ← h, stepOfEnvF_abs]

/-- **vc's default engine is `denoteResidual`**, step for step, for a well-ordered program with its
fanout table. -/
theorem vcEngine_sound {R : ResidualProgram} (hw : wellOrdered R = true) (depth : Nat → Nat) :
    (vcEngine R (fanout R) depth).Sound (denoteResidual R) := by
  refine ⟨VcInv R, ⟨?_, ?_, ?_, ?_⟩⟩
  · intro s
    exact ⟨Or.inl rfl, absS_ofState depth s⟩
  · intro q s hq
    obtain ⟨tabs, env, started⟩ := q
    exact ⟨hq, absS_ofState depth s⟩
  · intro q hq e i o q' hs
    obtain ⟨tabs, env0, started⟩ := q
    simp only [vcEngine, Except.ok.injEq, Prod.mk.injEq] at hs
    obtain ⟨rfl, rfl⟩ := hs
    have henv := vcEnvF_abs hw hq i tabs
    have hres := denote_of_envF henv e
    refine ⟨Or.inr ⟨i, absS tabs, henv⟩, ?_, ?_⟩
    · show _ = (denoteResidual R e i (absS tabs)).outputs
      rw [hres]; rfl
    · show absS _ = (denoteResidual R e i (absS tabs)).nextState
      rw [hres]; rfl
  · intro pk hp q hq e i o q' hs
    simp only [vcEngine, Option.some.injEq] at hp
    subst hp
    obtain ⟨tabs, env0, started⟩ := q
    simp only [Except.ok.injEq, Prod.mk.injEq] at hs
    obtain ⟨rfl, rfl⟩ := hs
    have henv := vcEnvF_abs hw hq i tabs
    have hres := denote_of_envF henv e
    refine ⟨Or.inr ⟨i, absS tabs, henv⟩, ?_, rfl⟩
    show _ = (denoteResidual R e i (absS tabs)).outputs
    rw [hres]; rfl

/-- The state of the modes that keep memories as functions. -/
structure VcFunState where
  s       : RuntimeState
  env     : SlotEnv
  started : Bool

/-- OLDER, SLOWER than `vcEngine` (module header): the modes with memories as functions, a memory
write wrapping the function, so a read walks back through the writes since the last compaction.
`next` makes each step's environment from the last one (`evEnv R fan`, event-driven; `incEnv R`,
visiting every binding). -/
def vcFunEngine (R : ResidualProgram) (next : SlotEnv → RuntimeInput → RuntimeState → SlotEnv) :
    Engine VcFunState where
  name := "vc"
  load s := { s, env := #[], started := false }
  step e i q :=
    match q with
    | ⟨s, env0, started⟩ =>
      let env := if started then next env0 i s else fullEnv R i s
      let r := stepOfEnv R env e s
      .ok (r.outputs, { s := r.nextState, env, started := true })
  view q := q.s
  reload q s :=
    match q with
    | ⟨_, env, started⟩ => { s, env, started }
  peek? := some fun _ i q =>
    match q with
    | ⟨s, env0, started⟩ =>
      let env := if started then next env0 i s else fullEnv R i s
      .ok (outputsOfEnv R env, { s, env, started := true })

/-- OLDER, SLOWER than `vcEngine` (about 1.5x per cycle on SoC): the event-driven step with
memories as functions (`evStep`). Selected by `mem:fun`. -/
def vcEvFunEngine (R : ResidualProgram) (fan : Array (Array Nat)) : Engine VcFunState :=
  vcFunEngine R (evEnv R fan)

/-- OLDER, SLOWER than `vcEngine` (about 3x per cycle on SoC): the incremental step that visits
every binding (`incStep`). Selected by `inc:scan`. -/
def vcScanEngine (R : ResidualProgram) : Engine VcFunState :=
  vcFunEngine R (incEnv R)

/-- OLDER, SLOWEST (about 25x `vcEngine`'s time per cycle on SoC): the whole `denoteResidual`
every step, which `cert_run` ran before engines. Selected by `inc:off`; `EngineSelector` also falls back
to it for a program that is not well-ordered (the incremental modes need that). -/
def vcFullEngine (R : ResidualProgram) : Engine RuntimeState :=
  Engine.ofStep "vc" fun e i s => .ok (denoteResidual R e i s)

/-- A function-memory mode is sound when its `next` turns any step's full environment into the
full environment of the inputs and state given. -/
theorem vcFunEngine_sound {R : ResidualProgram} {next : SlotEnv → RuntimeInput → RuntimeState → SlotEnv}
    (hnext : ∀ i' s' i s, next (fullEnv R i' s') i s = fullEnv R i s) :
    (vcFunEngine R next).Sound (denoteResidual R) := by
  have henv : ∀ {env0 : SlotEnv} {started : Bool}, (started = false ∨ ∃ i' s', env0 = fullEnv R i' s') →
      ∀ i s, (if started then next env0 i s else fullEnv R i s) = fullEnv R i s := by
    intro env0 started h i s
    cases hst : started
    · rfl
    · rcases h with h | ⟨i', s', h⟩
      · rw [hst] at h; cases h
      · simp only [ite_true]; rw [h, hnext]
  refine ⟨fun q => q.started = false ∨ ∃ i' s', q.env = fullEnv R i' s', ⟨?_, ?_, ?_, ?_⟩⟩
  · intro s
    exact ⟨Or.inl rfl, rfl⟩
  · intro q s hq
    obtain ⟨s0, env, started⟩ := q
    exact ⟨hq, rfl⟩
  · intro q hq e i o q' hs
    obtain ⟨s, env0, started⟩ := q
    simp only [vcFunEngine, Except.ok.injEq, Prod.mk.injEq] at hs
    obtain ⟨rfl, rfl⟩ := hs
    have h := henv hq i s
    refine ⟨Or.inr ⟨i, s, h⟩, ?_, ?_⟩
    · show (stepOfEnv R _ e s).outputs = (denoteResidual R e i s).outputs
      rw [h, ← denoteResidual_eq_stepOfEnv]
    · show (stepOfEnv R _ e s).nextState = (denoteResidual R e i s).nextState
      rw [h, ← denoteResidual_eq_stepOfEnv]
  · intro pk hp q hq e i o q' hs
    simp only [vcFunEngine, Option.some.injEq] at hp
    subst hp
    obtain ⟨s, env0, started⟩ := q
    simp only [Except.ok.injEq, Prod.mk.injEq] at hs
    obtain ⟨rfl, rfl⟩ := hs
    have h := henv hq i s
    refine ⟨Or.inr ⟨i, s, h⟩, ?_, rfl⟩
    show outputsOfEnv R _ = (denoteResidual R e i s).outputs
    rw [h, denoteResidual_eq_stepOfEnv]; rfl

theorem vcEvFunEngine_sound {R : ResidualProgram} (hw : wellOrdered R = true) :
    (vcEvFunEngine R (fanout R)).Sound (denoteResidual R) :=
  vcFunEngine_sound (evEnv_correct hw (fanout_ok R))

theorem vcScanEngine_sound {R : ResidualProgram} (hw : wellOrdered R = true) :
    (vcScanEngine R).Sound (denoteResidual R) :=
  vcFunEngine_sound (incEnv_correct hw)

theorem vcFullEngine_sound (R : ResidualProgram) : (vcFullEngine R).Sound (denoteResidual R) := by
  apply Engine.ofStep_sound
  intro e i s r h
  cases h; rfl

end Compiler

#print axioms Compiler.vcEngine_sound
#print axioms Compiler.vcEvFunEngine_sound
#print axioms Compiler.vcScanEngine_sound
