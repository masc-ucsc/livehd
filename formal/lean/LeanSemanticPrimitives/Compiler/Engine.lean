/-
# Engines: a route's step on a state of its own kind

A route keeps between steps whatever makes its next step fast: ir and ref keep the runtime
state itself; vc keeps memory tables and the last step's slot environment (`Compiler/VcEngine`).
`Engine σ` is a route as a driver's loop runs it:
- `load` builds the route's state standing for a runtime state (the reset state, a restored
  checkpoint);
- `step` is one clock step: the outputs before the edge and the route's state after it, or a
  refusal with the state it was given (so a loop that stops on a refusal still has its state
  without keeping a second reference to it during the step). It is a pure function, so a loop
  may step twice from one state (a closed loop's guess, then its final inputs);
- `view` is the runtime state a route state stands for: what a checkpoint writes, what another
  route is compared with, what compaction flattens;
- `reload q s` is the state for runtime state `s`, keeping what of `q` stays valid (by default
  nothing: `load s`; vc keeps its last environment, which any state may follow): compaction;
- `peek?`, when a route has one, gives the outputs before the edge without taking the edge, and
  a state standing for the same runtime state from which the next step is cheaper (vc: the
  environment for these inputs). A closed loop peeks with its guess, decides the final inputs,
  and takes one step.

`Engine.SoundWith E ref Inv`: loaded and reloaded states have `Inv` and stand for the runtime
state given; from a state with `Inv`, every step `E` takes gives `ref`'s outputs and a state with
`Inv` standing for `ref`'s next state, and every peek gives `ref`'s outputs and a state with `Inv`
standing for the same runtime state. `Engine.Sound E ref`: some `Inv` does. `ref` is `interpretDesign D` for ir and ref. For vc it is `denoteResidual R` here
and `interpretDesign D` on the Mathlib side (`CompileOpt`, through `compileOpt_correct`;
`Engine.Sound.of_ref_eq` moves a proof from one reference to an equal one).
`Engine.run_sound`: a whole run of a sound engine is `ref`'s run from the state it was loaded
with.

A driver's loop hands the state from one step to the next as the argument of a recursive
function, so each step holds the only reference to it and vc's step updates its environment and
tables in place. A `for` loop that kept the state in a `let mut` variable kept it shared during
the step (measured: vc's environment copied on every step, about 1.8 ms more per SoC cycle).

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.DirectStagedSound
import LeanSemanticPrimitives.Compiler.DirectCheck

namespace Compiler

/-- A route as a driver's loop runs it (see the module header). -/
structure Engine (σ : Type) where
  name   : String
  load   : RuntimeState → σ
  step   : ClockEdges → RuntimeInput → σ → Except (String × σ) (Array BV × σ)
  view   : σ → RuntimeState
  reload : σ → RuntimeState → σ := fun _ s => load s
  peek?  : Option (ClockEdges → RuntimeInput → σ → Except (String × σ) (Array BV × σ)) := none

/-- A reference step: a design's semantics (`interpretDesign D`), or a program's
(`denoteResidual R`). -/
abbrev RefStep := ClockEdges → RuntimeInput → RuntimeState → RuntimeResult

/-- `E` is `ref`'s step on the states it stands for, with invariant `Inv` (module header). -/
structure Engine.SoundWith {σ : Type} (E : Engine σ) (ref : RefStep) (Inv : σ → Prop) : Prop where
  load   : ∀ s, Inv (E.load s) ∧ E.view (E.load s) = s
  reload : ∀ q s, Inv q → Inv (E.reload q s) ∧ E.view (E.reload q s) = s
  step   : ∀ q, Inv q → ∀ e i o q', E.step e i q = .ok (o, q') →
    Inv q' ∧ o = (ref e i (E.view q)).outputs ∧ E.view q' = (ref e i (E.view q)).nextState
  peek   : ∀ pk, E.peek? = some pk → ∀ q, Inv q → ∀ e i o q', pk e i q = .ok (o, q') →
    Inv q' ∧ o = (ref e i (E.view q)).outputs ∧ E.view q' = E.view q

/-- From every loaded state, each step `E` takes is `ref`'s step on the state it stands for. -/
def Engine.Sound {σ : Type} (E : Engine σ) (ref : RefStep) : Prop :=
  ∃ Inv : σ → Prop, E.SoundWith ref Inv

/-- A proof against one reference is a proof against an equal one. -/
theorem Engine.SoundWith.of_ref_eq {σ : Type} {E : Engine σ} {ref ref' : RefStep} {Inv : σ → Prop}
    (h : E.SoundWith ref Inv) (heq : ∀ e i s, ref e i s = ref' e i s) : E.SoundWith ref' Inv where
  load := h.load
  reload := h.reload
  step q hq e i o q' hs := by rw [← heq]; exact h.step q hq e i o q' hs
  peek pk hp q hq e i o q' hs := by rw [← heq]; exact h.peek pk hp q hq e i o q' hs

theorem Engine.Sound.of_ref_eq {σ : Type} {E : Engine σ} {ref ref' : RefStep}
    (h : E.Sound ref) (heq : ∀ e i s, ref e i s = ref' e i s) : E.Sound ref' := by
  obtain ⟨Inv, h⟩ := h
  exact ⟨Inv, h.of_ref_eq heq⟩

/-- `ref`'s outputs, step after step, from `s`. -/
def refOutputs (ref : RefStep) : RuntimeState → List (ClockEdges × RuntimeInput) → List (Array BV)
  | _, [] => []
  | s, (e, i) :: rest => (ref e i s).outputs :: refOutputs ref (ref e i s).nextState rest

/-- The engine's outputs, step after step, from `q`; the first refusal stops the run. -/
def Engine.run {σ : Type} (E : Engine σ) : σ → List (ClockEdges × RuntimeInput) →
    Except String (List (Array BV))
  | _, [] => .ok []
  | q, (e, i) :: rest =>
    match E.step e i q with
    | .error (err, _) => .error err
    | .ok (o, q') =>
      match E.run q' rest with
      | .error err => .error err
      | .ok os => .ok (o :: os)

theorem Engine.run_inv {σ : Type} {E : Engine σ} {ref : RefStep} {Inv : σ → Prop}
    (hstep : ∀ q, Inv q → ∀ e i o q', E.step e i q = .ok (o, q') →
      Inv q' ∧ o = (ref e i (E.view q)).outputs ∧ E.view q' = (ref e i (E.view q)).nextState) :
    ∀ (ticks : List (ClockEdges × RuntimeInput)) (q : σ) (os : List (Array BV)), Inv q →
      E.run q ticks = .ok os → os = refOutputs ref (E.view q) ticks
  | [], q, os, _, h => by simp only [Engine.run] at h; cases h; rfl
  | (e, i) :: rest, q, os, hq, h => by
    simp only [Engine.run] at h
    split at h
    · cases h
    · rename_i o q' hs
      split at h
      · cases h
      · rename_i os' hr
        cases h
        obtain ⟨hq', ho, hv⟩ := hstep q hq e i o q' hs
        simp only [refOutputs]
        rw [ho, Engine.run_inv hstep rest q' os' hq' hr, hv]

/-- **A run of a sound engine is the reference's run** from the state it was loaded with. -/
theorem Engine.run_sound {σ : Type} {E : Engine σ} {ref : RefStep} (h : E.Sound ref)
    (s : RuntimeState) (ticks : List (ClockEdges × RuntimeInput)) (os : List (Array BV))
    (hr : E.run (E.load s) ticks = .ok os) : os = refOutputs ref s ticks := by
  obtain ⟨Inv, h⟩ := h
  have := Engine.run_inv h.step ticks (E.load s) os (h.load s).1 hr
  rwa [(h.load s).2] at this

/-! ## Replays: a whole run's results, or its last state

A replay (`lgraph-sim`'s trace, `ckpt_reach`'s witness) needs the run's states, not only its
outputs. `Engine.trace` gives each step's result (the outputs and the runtime state the next
route state stands for: `view` after every step, which costs nothing for an engine whose state
is the runtime state); `Engine.runFinal` only the last state. -/

/-- `ref`'s results, step after step, from `s`, and the state after the last step. -/
def refRun (ref : RefStep) : RuntimeState → List (ClockEdges × RuntimeInput) →
    List RuntimeResult × RuntimeState
  | s, [] => ([], s)
  | s, (e, i) :: rest =>
    let r := ref e i s
    let t := refRun ref r.nextState rest
    (r :: t.1, t.2)

/-- The engine's result at every step from `q` (its outputs, and the view of its state after the
step) and its state after the last step; the first refusal stops the run. -/
def Engine.trace {σ : Type} (E : Engine σ) : σ → List (ClockEdges × RuntimeInput) →
    Except String (List RuntimeResult × σ)
  | q, [] => .ok ([], q)
  | q, (e, i) :: rest =>
    match E.step e i q with
    | .error (err, _) => .error err
    | .ok (o, q') =>
      match E.trace q' rest with
      | .error err => .error err
      | .ok (rs, qf) => .ok ({ outputs := o, nextState := E.view q' } :: rs, qf)

/-- The engine's state after the steps from `q`; the first refusal stops the run. -/
def Engine.runFinal {σ : Type} (E : Engine σ) : σ → List (ClockEdges × RuntimeInput) → Except String σ
  | q, [] => .ok q
  | q, (e, i) :: rest =>
    match E.step e i q with
    | .error (err, _) => .error err
    | .ok (_, q') => E.runFinal q' rest

theorem Engine.trace_inv {σ : Type} {E : Engine σ} {ref : RefStep} {Inv : σ → Prop}
    (hstep : ∀ q, Inv q → ∀ e i o q', E.step e i q = .ok (o, q') →
      Inv q' ∧ o = (ref e i (E.view q)).outputs ∧ E.view q' = (ref e i (E.view q)).nextState) :
    ∀ (ticks : List (ClockEdges × RuntimeInput)) (q : σ) (rs : List RuntimeResult) (qf : σ), Inv q →
      E.trace q ticks = .ok (rs, qf) →
      rs = (refRun ref (E.view q) ticks).1 ∧ E.view qf = (refRun ref (E.view q) ticks).2
  | [], q, rs, qf, _, h => by simp only [Engine.trace] at h; cases h; exact ⟨rfl, rfl⟩
  | (e, i) :: rest, q, rs, qf, hq, h => by
    simp only [Engine.trace] at h
    split at h
    · cases h
    · rename_i o q' hs
      split at h
      · cases h
      · rename_i rs' qf' hr
        cases h
        obtain ⟨hq', ho, hv⟩ := hstep q hq e i o q' hs
        obtain ⟨hrs, hvf⟩ := Engine.trace_inv hstep rest q' rs' qf hq' hr
        rw [hv] at hrs hvf
        refine ⟨?_, ?_⟩
        · simp only [refRun]
          rw [hrs, ho, hv]
        · simp only [refRun]
          exact hvf

theorem Engine.runFinal_inv {σ : Type} {E : Engine σ} {ref : RefStep} {Inv : σ → Prop}
    (hstep : ∀ q, Inv q → ∀ e i o q', E.step e i q = .ok (o, q') →
      Inv q' ∧ o = (ref e i (E.view q)).outputs ∧ E.view q' = (ref e i (E.view q)).nextState) :
    ∀ (ticks : List (ClockEdges × RuntimeInput)) (q qf : σ), Inv q →
      E.runFinal q ticks = .ok qf → E.view qf = (refRun ref (E.view q) ticks).2
  | [], q, qf, _, h => by simp only [Engine.runFinal] at h; cases h; rfl
  | (e, i) :: rest, q, qf, hq, h => by
    simp only [Engine.runFinal] at h
    split at h
    · cases h
    · rename_i o q' hs
      obtain ⟨hq', _, hv⟩ := hstep q hq e i o q' hs
      have := Engine.runFinal_inv hstep rest q' qf hq' h
      rw [hv] at this
      simp only [refRun]
      exact this

/-- **A replay of a sound engine is the reference's run** from the state it was loaded with: its
results step after step and its last state. -/
theorem Engine.trace_sound {σ : Type} {E : Engine σ} {ref : RefStep} (h : E.Sound ref)
    (s : RuntimeState) (ticks : List (ClockEdges × RuntimeInput)) (rs : List RuntimeResult) (qf : σ)
    (hr : E.trace (E.load s) ticks = .ok (rs, qf)) :
    rs = (refRun ref s ticks).1 ∧ E.view qf = (refRun ref s ticks).2 := by
  obtain ⟨Inv, h⟩ := h
  have := Engine.trace_inv h.step ticks (E.load s) rs qf (h.load s).1 hr
  rwa [(h.load s).2] at this

/-- **The last state of a sound engine's run is the reference's.** -/
theorem Engine.runFinal_sound {σ : Type} {E : Engine σ} {ref : RefStep} (h : E.Sound ref)
    (s : RuntimeState) (ticks : List (ClockEdges × RuntimeInput)) (qf : σ)
    (hr : E.runFinal (E.load s) ticks = .ok qf) : E.view qf = (refRun ref s ticks).2 := by
  obtain ⟨Inv, h⟩ := h
  have := Engine.runFinal_inv h.step ticks (E.load s) qf (h.load s).1 hr
  rwa [(h.load s).2] at this

/-! ## Engines whose state is the runtime state -/

/-- The engine of a step on runtime states. -/
def Engine.ofStep (name : String)
    (f : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult) :
    Engine RuntimeState where
  name := name
  load s := s
  step e i s :=
    match f e i s with
    | .error err => .error (err, s)
    | .ok r => .ok (r.outputs, r.nextState)
  view s := s

/-- Such an engine is sound when every result its step gives is the reference's. -/
theorem Engine.ofStep_sound {name : String}
    {f : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult} {ref : RefStep}
    (h : ∀ e i s r, f e i s = .ok r → r = ref e i s) : (Engine.ofStep name f).Sound ref := by
  refine ⟨fun _ => True, fun s => ⟨trivial, rfl⟩, fun _ s _ => ⟨trivial, rfl⟩, ?_, ?_⟩
  · intro q _ e i o q' hs
    simp only [Engine.ofStep] at hs
    split at hs
    · cases hs
    · rename_i r hr
      cases hs
      rw [h e i q r hr]
      exact ⟨trivial, rfl, rfl⟩
  · intro pk hp
    cases hp

/-- The `ir` engine: the staged direct step (`Compiler/DirectStaged`), the design prepared once,
each step after the runtime check. -/
def irEngine (D : DesignCert) : Engine RuntimeState :=
  let P := Staged.prepare D
  Engine.ofStep "ir" fun e i s =>
    match Direct.checkRuntime D e i s with
    | .error err => .error err.render
    | .ok _ => .ok (Staged.step P e i s)

/-- The `ir` engine on the unstaged direct step (`directStepRaw`), as `runDirect` iterates it. -/
def irRawEngine (D : DesignCert) : Engine RuntimeState :=
  Engine.ofStep "ir" fun e i s =>
    match Direct.checkRuntime D e i s with
    | .error err => .error err.render
    | .ok _ => .ok (Direct.directStepRaw D e i s)

/-- The `ref` engine: `interpretDesign` itself. -/
def refEngine (D : DesignCert) : Engine RuntimeState :=
  Engine.ofStep "ref" fun e i s => .ok (interpretDesign D e i s)

/-- **The staged `ir` engine is sound** for a certificate the direct check accepts. -/
theorem irEngine_sound {D : DesignCert} (hd : Direct.checkDesign D = .ok ()) :
    (irEngine D).Sound (interpretDesign D) := by
  apply Engine.ofStep_sound
  intro e i s r h
  split at h
  · cases h
  · cases h
    have hdb := (Direct.checkDesign_sound hd).depsBounded
    rw [Staged.step_eq D hdb]
    exact Direct.directStepRaw_correct D hdb e i s

theorem irRawEngine_sound {D : DesignCert} (hd : Direct.checkDesign D = .ok ()) :
    (irRawEngine D).Sound (interpretDesign D) := by
  apply Engine.ofStep_sound
  intro e i s r h
  split at h
  · cases h
  · cases h
    exact Direct.directStepRaw_correct D (Direct.checkDesign_sound hd).depsBounded e i s

theorem refEngine_sound (D : DesignCert) : (refEngine D).Sound (interpretDesign D) := by
  apply Engine.ofStep_sound
  intro e i s r h
  cases h; rfl

end Compiler

#print axioms Compiler.Engine.run_sound
#print axioms Compiler.Engine.trace_sound
#print axioms Compiler.Engine.runFinal_sound
#print axioms Compiler.irEngine_sound
