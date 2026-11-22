/-
# A closed-loop run is the reference trace of its ticks

The host decides every cycle's inputs from the design's outputs, so the input sequence of a
closed-loop run is not known in advance. What is fixed is how each cycle uses the design:
`cycleStep` applies exactly one step, on the tick it records (`co.tick`), and keeps its result.

* `StepSound D step`: the stepper agrees with `interpretDesign` on every step it accepts.
  `irStep_sound` proves it for the direct simulator (route `ir`), `irStepStaged_sound` for its
  staged form (the one `mkStepper` builds); `refStep_sound` for `ref`.
  For route `vc` it is `compileDesign_correct` / `compileOpt_correct` (on the lake side).
* `cycleStep_applied`: an applied cycle's outputs and next state are `interpretDesign`'s on the
  recorded tick; `cycleStep_held`: a cycle that stops before its edge leaves the state as it was.
* `runCycles_sound`: for every host (every bind file, image, latency and stimulus) the run's
  outputs and final state are those of `refTrace` (iterated `interpretDesign`) on the recorded
  ticks. So the inputs a run writes (`inputs.stim`) replay it open-loop on any route.
* `compacted_outputs`: a run cut into segments, its state replaced by the restored checkpoint
  of itself after each segment (`cert_run compact:<K>`), prints the outputs of the run that is
  not cut, from power-on, from a restored checkpoint, or from any state whose words fit.

Mathlib-free; the axioms are audited at the end.
-/
import HDLProof.Run.Loop
import LeanSemanticPrimitives.Compiler.DirectTrace
import LeanSemanticPrimitives.Projection.CkptResume
import LeanSemanticPrimitives.Compiler.DirectStagedSound
import LeanSemanticPrimitives.Projection.AxiomGate

namespace HDLProof
namespace Run

open Compiler Compiler.Direct

/-- The stepper agrees with the reference semantics on every step it accepts. -/
def StepSound (D : DesignCert)
    (step : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult) : Prop :=
  ∀ e i s r, step e i s = .ok r → r = interpretDesign D e i s

theorem irStep_sound (D : DesignCert) (hd : Direct.checkDesign D = .ok ()) :
    StepSound D (irStep D) := by
  intro e i s r h
  unfold irStep at h
  split at h
  · cases h
  · cases h
    exact directStepRaw_correct D (checkDesign_sound hd).depsBounded e i s

/-- The staged `ir` stepper is sound: `Staged.step_eq` and `directStepRaw_correct`. -/
theorem irStepStaged_sound (D : DesignCert) (hd : Direct.checkDesign D = .ok ()) :
    StepSound D (irStepStaged (Staged.prepare D)) := by
  intro e i s r h
  unfold irStepStaged at h
  split at h
  · cases h
  · cases h
    have hdb := (checkDesign_sound hd).depsBounded
    rw [Staged.step_eq D hdb]
    exact directStepRaw_correct D hdb e i s

theorem refStep_sound (D : DesignCert) :
    StepSound D (fun e i s => .ok (interpretDesign D e i s)) := by
  intro e i s r h
  cases h; rfl

/-- The `ir` stepper `mkStepper` builds is sound. -/
theorem mkStepper_ir_sound {D : DesignCert} {st : Stepper} {opt : Bool}
    (h : mkStepper D .ir opt = .ok st) : StepSound D st.step := by
  simp only [mkStepper] at h
  cases hc : Direct.checkDesign D with
  | error e => rw [hc] at h; cases h
  | ok u =>
    rw [hc] at h
    cases h
    exact irStepStaged_sound D (by cases u; exact hc)

theorem mkStepper_ref_sound {D : DesignCert} {st : Stepper} {opt : Bool}
    (h : mkStepper D .ref opt = .ok st) : StepSound D st.step := by
  simp only [mkStepper] at h
  cases h
  exact refStep_sound D

/-! ## One cycle -/

theorem stepFinal_sound {ctx : Ctx} (hs : StepSound ctx.D ctx.step) {e : ClockEdges}
    {s : RuntimeState} {guess inputs : RuntimeInput} {rg r : RuntimeResult}
    (hg : rg = interpretDesign ctx.D e guess s) (h : stepFinal ctx e s guess rg inputs = .ok r) :
    r = interpretDesign ctx.D e inputs s := by
  unfold stepFinal at h
  by_cases heq : (inputs == guess) = true
  · simp only [heq, ↓reduceIte] at h
    cases h
    rw [beq_iff_eq.mp heq]; exact hg
  · simp only [heq, Bool.false_eq_true, ↓reduceIte] at h
    exact hs _ _ _ _ h

theorem cycleStep_cases {ctx : Ctx} (hs : StepSound ctx.D ctx.step) {st : LoopState}
    {co : CycleOut} (h : cycleStep ctx st = .ok co) :
    (co.applied = true ∧
      interpretDesign ctx.D co.tick.edges co.tick.input st.s =
        { outputs := co.outs, nextState := co.next.s }) ∨
    (co.applied = false ∧ co.next.s = st.s) := by
  unfold cycleStep at h
  dsimp only at h
  generalize edgesAt ctx.D ctx.edges st.cycle = e at h
  generalize ctx.guess st.cycle = guess at h
  cases hrg : ctx.step e guess st.s with
  | error err => rw [hrg] at h; cases h
  | ok rg =>
    rw [hrg] at h
    have hg := hs _ _ _ _ hrg
    simp only at h
    cases hp : plan ctx st guess rg.outputs with
    | stop why hh =>
      rw [hp] at h; cases h
      exact Or.inr ⟨rfl, rfl⟩
    | go inputs hh done events =>
      rw [hp] at h
      simp only at h
      cases hr : stepFinal ctx e st.s guess rg inputs with
      | error err => rw [hr] at h; cases h
      | ok r =>
        rw [hr] at h
        have hri := stepFinal_sound hs hg hr
        simp only at h
        by_cases hm : (r.outputs == rg.outputs) = true
        · simp only [hm, ↓reduceIte] at h
          cases h
          exact Or.inl ⟨rfl, by rw [← hri]⟩
        · simp only [hm, Bool.false_eq_true, ↓reduceIte] at h
          cases h
          exact Or.inr ⟨rfl, rfl⟩

/-- An applied cycle is `interpretDesign` on the tick it records. -/
theorem cycleStep_applied {ctx : Ctx} (hs : StepSound ctx.D ctx.step) {st : LoopState}
    {co : CycleOut} (h : cycleStep ctx st = .ok co) (ha : co.applied = true) :
    interpretDesign ctx.D co.tick.edges co.tick.input st.s =
      { outputs := co.outs, nextState := co.next.s } := by
  rcases cycleStep_cases hs h with ⟨_, he⟩ | ⟨hf, _⟩
  · exact he
  · rw [ha] at hf; cases hf

/-- A cycle that stops before its edge leaves the design's state as it was. -/
theorem cycleStep_held {ctx : Ctx} (hs : StepSound ctx.D ctx.step) {st : LoopState}
    {co : CycleOut} (h : cycleStep ctx st = .ok co) (ha : co.applied = false) :
    co.next.s = st.s := by
  rcases cycleStep_cases hs h with ⟨ht, _⟩ | ⟨_, he⟩
  · rw [ha] at ht; cases ht
  · exact he

/-! ## A run -/

/-- At most `n` cycles from `st`, until a status stops the run: the applied ticks, their
outputs, the final loop state and the status. The driver iterates `cycleStep` the same way. -/
def runCycles (ctx : Ctx) : Nat → LoopState →
    Except String (List Tick × List (Array BV) × LoopState × Option String)
  | 0, st => .ok ([], [], st, none)
  | n + 1, st =>
    match cycleStep ctx st with
    | .error e => .error e
    | .ok co =>
      if co.applied then
        match co.status with
        | some why => .ok ([co.tick], [co.outs], co.next, some why)
        | none =>
          match runCycles ctx n co.next with
          | .error e => .error e
          | .ok (ts, os, st', why) => .ok (co.tick :: ts, co.outs :: os, st', why)
      else .ok ([], [], co.next, co.status)

/-- **A closed-loop run is the reference trace of its recorded ticks**, for every host. -/
theorem runCycles_sound {ctx : Ctx} (hs : StepSound ctx.D ctx.step) :
    ∀ (n : Nat) (st : LoopState) (ts : List Tick) (os : List (Array BV)) (st' : LoopState)
      (why : Option String), runCycles ctx n st = .ok (ts, os, st', why) →
      (refTrace ctx.D st.s ts).finalState = st'.s ∧
      (refTrace ctx.D st.s ts).steps.map (·.outputs) = os := by
  intro n
  induction n with
  | zero =>
    intro st ts os st' why h
    simp only [runCycles] at h
    cases h
    simp [refTrace]
  | succ n ih =>
    intro st ts os st' why h
    simp only [runCycles] at h
    cases hco : cycleStep ctx st with
    | error e => rw [hco] at h; cases h
    | ok co =>
      rw [hco] at h
      simp only at h
      by_cases ha : co.applied = true
      · have happ := cycleStep_applied hs hco ha
        simp only [ha, ↓reduceIte] at h
        cases hst : co.status with
        | some w =>
          rw [hst] at h
          cases h
          simp [refTrace, happ]
        | none =>
          rw [hst] at h
          simp only at h
          cases hres : runCycles ctx n co.next with
          | error e => rw [hres] at h; cases h
          | ok res =>
            obtain ⟨ts1, os1, st1, why1⟩ := res
            rw [hres] at h
            cases h
            obtain ⟨h1, h2⟩ := ih co.next _ _ _ _ hres
            simp [refTrace, happ, h1, h2]
      · have hf : co.applied = false := by simpa using ha
        have hheld := cycleStep_held hs hco hf
        simp only [ha, Bool.false_eq_true, ↓reduceIte] at h
        cases h
        exact ⟨hheld.symm, rfl⟩

/-! ## Compaction (`compact:<K>`) -/

section
open Projection.CkptResume (WordsFit resume_exact wordsFit_trace mems_size_trace wordsFit_restore)
variable {D : DesignCert}

/-- The outputs of a run on `a ++ b` are those on `a`, then those on `b` from where `a` ends. -/
theorem refTrace_append_outputs (D : DesignCert) :
    ∀ (s : RuntimeState) (a b : List Tick),
      (refTrace D s (a ++ b)).steps.map (·.outputs) =
        (refTrace D s a).steps.map (·.outputs) ++
          (refTrace D (refTrace D s a).finalState b).steps.map (·.outputs)
  | _, [], _ => rfl
  | s, t :: a, b => by
    simp only [List.cons_append, refTrace, List.map_cons]
    rw [refTrace_append_outputs D _ a b]

/-- `restore` reads neither the cycle nor the ext sections of a checkpoint. -/
theorem restore_save_cycle (hash k : Nat) (exts : Array Ckpt.Ext) (s : RuntimeState) :
    Ckpt.restore (Ckpt.save D hash k exts s) = Ckpt.restore (Ckpt.save D hash 0 #[] s) := rfl

/-- The outputs of a run in segments `segs` from `s`, its state replaced by its restored
checkpoint after each segment, as `cert_run compact:<K>` does every `K` cycles. -/
def compactedOutputs (D : DesignCert) (hash : Nat) : RuntimeState → List (List Tick) → List (Array BV)
  | _, [] => []
  | s, seg :: segs =>
    (refTrace D s seg).steps.map (·.outputs) ++
      compactedOutputs D hash (Ckpt.restore (Ckpt.save D hash 0 #[] (refTrace D s seg).finalState)) segs

/-- **Compaction is exact.** Cutting a run into segments and replacing its state by the
restored checkpoint of itself after each one changes none of its outputs. The start is any
state of the right shape whose memory words fit (`resetState`, `zeroState` and every restored
valid checkpoint are: `wordsFit_reset`, `wordsFit_zero`, `wordsFit_restore`). -/
theorem compacted_outputs (hsem : Direct.DesignSemWF D) (haddr : MemAddr.addrError D = none)
    (hroot : Ckpt.imgRootError D = none) (hword : Ckpt.wordError D = none) (hash : Nat) :
    ∀ (segs : List (List Tick)) (s : RuntimeState), s.mems.size = D.memories.size →
      WordsFit D s →
      compactedOutputs D hash s segs = (refTrace D s segs.flatten).steps.map (·.outputs)
  | [], s, _, _ => by simp [compactedOutputs, refTrace]
  | seg :: segs, s, hsz, hfit => by
    have hszf : (refTrace D s seg).finalState.mems.size = D.memories.size := mems_size_trace hsz seg
    have hfitf : WordsFit D (refTrace D s seg).finalState := wordsFit_trace hsem hroot hword seg s hfit
    have hv := Ckpt.validate_save D hash 0 #[] (refTrace D s seg).finalState hroot hword
    have hsz' : (Ckpt.restore (Ckpt.save D hash 0 #[] (refTrace D s seg).finalState)).mems.size =
        D.memories.size := by
      simp [Ckpt.restore, Ckpt.save]
    have ih := compacted_outputs hsem haddr hroot hword hash segs _ hsz' (wordsFit_restore hv)
    simp only [compactedOutputs, List.flatten_cons]
    rw [ih, resume_exact hsem haddr hroot hash 0 #[] hszf hfitf, refTrace_append_outputs]

end

#audit_axioms HDLProof.Run.compacted_outputs
#audit_axioms HDLProof.Run.runCycles_sound
#audit_axioms HDLProof.Run.cycleStep_applied
#audit_axioms HDLProof.Run.mkStepper_ir_sound

end Run
end HDLProof
