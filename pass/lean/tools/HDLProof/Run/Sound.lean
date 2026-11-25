/-
# A closed-loop run is the reference trace of its ticks

The host decides every cycle's inputs from the design's outputs, so the input sequence of a
closed-loop run is not known in advance. What is fixed is how each cycle uses the design:
`cycleStep ctx E` applies exactly one step of engine `E`, on the tick it records (`co.tick`), and
keeps its result.

* `LoopSpec E Inv Spec`: every step of `E` from a state with `Inv` keeps `Inv` and meets `Spec` on
  the runtime states they stand for (`E.view`); every peek keeps `Inv` and the runtime state.
  `cycleStep_spec`: after a cycle the state has `Inv`; an applied cycle meets `Spec` on its
  recorded tick, and a cycle that stops before its edge leaves the design's state as it was.
  Both tracks below are instances: the exact routes (`LoopSpec.ofSound`, `Spec` =
  `interpretDesign`'s step) and `fp` (`LoopSpec.ofAgree`, `Spec` = agreement below depth).

* `StepSound D step`: the stepper agrees with `interpretDesign` on every step it accepts.
  `irStep_sound` proves it for the direct simulator (route `ir`), `irStepStaged_sound` for its
  staged form (the one `mkStepper` builds); `refStep_sound` for `ref`.
  For route `vc` it is `compileDesign_correct` / `compileOpt_correct` (on the lake side).
* `cycleStep_sound`: on an engine sound against `interpretDesign` (`Engine.SoundWith`: ir, ref and
  every vc mode, `EngineSelector_*_sound`), an applied cycle's outputs and next state are
  `interpretDesign`'s on the recorded tick.
* `runCycles_sound`: for every host (every bind file, image, latency and stimulus) the run's
  outputs and final state are those of `refTrace` (iterated `interpretDesign`) on the recorded
  ticks. So the inputs a run writes (`inputs.stim`) replay it open-loop on any route.
* `compacted_outputs`: a run cut into segments, its state replaced by the restored checkpoint
  of itself after each segment (`cert_run compact:<K>`), prints the outputs of the run that is
  not cut, from power-on, from a restored checkpoint, or from any state whose words fit.
* Route `fp` (`fpStep_agree`, `mkStepper_fp_agree`): the residual's step, on the state converted
  to the residual's, gives `interpretDesign`'s outputs and a next state that agrees with
  `interpretDesign`'s on every flop and every memory word below its depth (`StateAgree`), not as
  memory functions everywhere (`EngineAgree`, `ofStep_engineAgree`). `runCycles_agree` /
  `runCycles_fp_outputs`: a run on it prints `refTrace`'s outputs on the recorded ticks and ends
  in a state that agrees with `refTrace`'s.

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

/-- What a loop needs of an engine (module header). -/
structure LoopSpec {σ : Type} (E : Engine σ) (Inv : σ → Prop)
    (Spec : RuntimeState → ClockEdges → RuntimeInput → Array BV → RuntimeState → Prop) : Prop where
  step : ∀ q, Inv q → ∀ e i o q', E.step e i q = .ok (o, q') →
    Inv q' ∧ Spec (E.view q) e i o (E.view q')
  peek : ∀ pk, E.peek? = some pk → ∀ q, Inv q → ∀ e i o q', pk e i q = .ok (o, q') →
    Inv q' ∧ E.view q' = E.view q

/-- **One cycle**: the state after it has `Inv`; an applied cycle meets `Spec` on the tick it
records; a cycle that stops before its edge leaves the design's state as it was. -/
theorem cycleStep_spec {σ : Type} {ctx : Ctx} {E : Engine σ} {Inv : σ → Prop}
    {Spec : RuntimeState → ClockEdges → RuntimeInput → Array BV → RuntimeState → Prop}
    (hs : LoopSpec E Inv Spec) {st : LoopState σ} {co : CycleOut σ} (hq : Inv st.s)
    (h : cycleStep ctx E st = .ok co) :
    Inv co.next.s ∧
      ((co.applied = true ∧
          Spec (E.view st.s) co.tick.edges co.tick.input co.outs (E.view co.next.s)) ∨
        (co.applied = false ∧ E.view co.next.s = E.view st.s)) := by
  obtain ⟨q, host, k⟩ := st
  unfold cycleStep at h
  dsimp only at h hq ⊢
  generalize edgesAt ctx.D ctx.edges k = e at h
  generalize ctx.guess k = guess at h
  cases hp : E.peek? with
  | some pk =>
    rw [hp] at h
    dsimp only at h
    cases h1 : pk e guess q with
    | error err => rw [h1] at h; cases h
    | ok r1 =>
      obtain ⟨og, q1⟩ := r1
      rw [h1] at h
      dsimp only at h
      obtain ⟨hq1, hv1⟩ := hs.peek pk hp q hq e guess og q1 h1
      cases hpl : plan ctx host k guess og with
      | stop why hh =>
        rw [hpl] at h
        cases h
        exact ⟨hq1, Or.inr ⟨rfl, hv1⟩⟩
      | go inputs hh done events =>
        rw [hpl] at h
        dsimp only at h
        cases h2 : (if inputs == guess then (Except.ok (og, q1) : Except (String × σ) (Array BV × σ))
            else pk e inputs q1) with
        | error err => rw [h2] at h; cases h
        | ok r2 =>
          obtain ⟨o, q2⟩ := r2
          rw [h2] at h
          dsimp only at h
          have hq2v : Inv q2 ∧ E.view q2 = E.view q := by
            by_cases heq : (inputs == guess) = true
            · simp only [heq, ↓reduceIte, Except.ok.injEq, Prod.mk.injEq] at h2
              obtain ⟨-, rfl⟩ := h2
              exact ⟨hq1, hv1⟩
            · simp only [heq, Bool.false_eq_true, ↓reduceIte] at h2
              obtain ⟨hq2, hv2⟩ := hs.peek pk hp q1 hq1 e inputs o q2 h2
              exact ⟨hq2, hv2.trans hv1⟩
          by_cases hm : (o == og) = true
          · simp only [hm, ↓reduceIte] at h
            cases h3 : E.step e inputs q2 with
            | error err => rw [h3] at h; cases h
            | ok r3 =>
              obtain ⟨o', q3⟩ := r3
              rw [h3] at h
              cases h
              obtain ⟨hq3, hsp⟩ := hs.step q2 hq2v.1 e inputs o' q3 h3
              refine ⟨hq3, Or.inl ⟨rfl, ?_⟩⟩
              rw [← hq2v.2]
              exact hsp
          · simp only [hm, Bool.false_eq_true, ↓reduceIte] at h
            cases h
            exact ⟨hq2v.1, Or.inr ⟨rfl, hq2v.2⟩⟩
  | none =>
    rw [hp] at h
    dsimp only at h
    cases h1 : E.step e guess q with
    | error err => rw [h1] at h; cases h
    | ok r1 =>
      obtain ⟨og, qg⟩ := r1
      rw [h1] at h
      dsimp only at h
      obtain ⟨hqg, hspg⟩ := hs.step q hq e guess og qg h1
      cases hpl : plan ctx host k guess og with
      | stop why hh =>
        rw [hpl] at h
        cases h
        exact ⟨hq, Or.inr ⟨rfl, rfl⟩⟩
      | go inputs hh done events =>
        rw [hpl] at h
        dsimp only at h
        cases h2 : (if inputs == guess then (Except.ok (og, qg) : Except (String × σ) (Array BV × σ))
            else E.step e inputs q) with
        | error err => rw [h2] at h; cases h
        | ok r2 =>
          obtain ⟨o, q'⟩ := r2
          rw [h2] at h
          dsimp only at h
          have hq' : Inv q' ∧ Spec (E.view q) e inputs o (E.view q') := by
            by_cases heq : (inputs == guess) = true
            · simp only [heq, ↓reduceIte, Except.ok.injEq, Prod.mk.injEq] at h2
              obtain ⟨rfl, rfl⟩ := h2
              rw [beq_iff_eq.mp heq]
              exact ⟨hqg, hspg⟩
            · simp only [heq, Bool.false_eq_true, ↓reduceIte] at h2
              exact hs.step q hq e inputs o q' h2
          by_cases hm : (o == og) = true
          · simp only [hm, ↓reduceIte] at h
            cases h
            exact ⟨hq'.1, Or.inl ⟨rfl, hq'.2⟩⟩
          · simp only [hm, Bool.false_eq_true, ↓reduceIte] at h
            cases h
            exact ⟨hq, Or.inr ⟨rfl, rfl⟩⟩

/-- `interpretDesign`'s step on runtime states: the exact routes' `Spec`. -/
def SpecExact (D : DesignCert) (s : RuntimeState) (e : ClockEdges) (i : RuntimeInput) (o : Array BV)
    (s' : RuntimeState) : Prop :=
  interpretDesign D e i s = { outputs := o, nextState := s' }

theorem LoopSpec.ofSound {σ : Type} {E : Engine σ} {D : DesignCert} {Inv : σ → Prop}
    (h : E.SoundWith (interpretDesign D) Inv) : LoopSpec E Inv (SpecExact D) where
  step q hq e i o q' hs := by
    obtain ⟨hq', ho, hv⟩ := h.step q hq e i o q' hs
    exact ⟨hq', by unfold SpecExact; rw [ho, hv]⟩
  peek pk hp q hq e i o q' hs := by
    obtain ⟨hq', -, hv⟩ := h.peek pk hp q hq e i o q' hs
    exact ⟨hq', hv⟩

/-- On an engine sound against `interpretDesign`, an applied cycle is `interpretDesign` on the tick
it records, and a cycle that stops before its edge leaves the design's state as it was. -/
theorem cycleStep_sound {σ : Type} {ctx : Ctx} {E : Engine σ} {Inv : σ → Prop}
    (hs : E.SoundWith (interpretDesign ctx.D) Inv) {st : LoopState σ} {co : CycleOut σ}
    (hq : Inv st.s) (h : cycleStep ctx E st = .ok co) :
    Inv co.next.s ∧
      ((co.applied = true ∧ interpretDesign ctx.D co.tick.edges co.tick.input (E.view st.s) =
          { outputs := co.outs, nextState := E.view co.next.s }) ∨
        (co.applied = false ∧ E.view co.next.s = E.view st.s)) :=
  cycleStep_spec (LoopSpec.ofSound hs) hq h

/-! ## A run -/

/-- At most `n` cycles from `st`, until a status stops the run: the applied ticks, their
outputs, the final loop state and the status. The driver iterates `cycleStep` the same way. -/
def runCycles {σ : Type} (ctx : Ctx) (E : Engine σ) : Nat → LoopState σ →
    Except String (List Tick × List (Array BV) × LoopState σ × Option String)
  | 0, st => .ok ([], [], st, none)
  | n + 1, st =>
    match cycleStep ctx E st with
    | .error (e, _) => .error e
    | .ok co =>
      if co.applied then
        match co.status with
        | some why => .ok ([co.tick], [co.outs], co.next, some why)
        | none =>
          match runCycles ctx E n co.next with
          | .error e => .error e
          | .ok (ts, os, st', why) => .ok (co.tick :: ts, co.outs :: os, st', why)
      else .ok ([], [], co.next, co.status)

/-- **A closed-loop run is the reference trace of its recorded ticks**, for every host, on every
engine sound against `interpretDesign`. -/
theorem runCycles_sound {σ : Type} {ctx : Ctx} {E : Engine σ} {Inv : σ → Prop}
    (hs : E.SoundWith (interpretDesign ctx.D) Inv) :
    ∀ (n : Nat) (st : LoopState σ) (ts : List Tick) (os : List (Array BV)) (st' : LoopState σ)
      (why : Option String), Inv st.s → runCycles ctx E n st = .ok (ts, os, st', why) →
      (refTrace ctx.D (E.view st.s) ts).finalState = E.view st'.s ∧
      (refTrace ctx.D (E.view st.s) ts).steps.map (·.outputs) = os := by
  intro n
  induction n with
  | zero =>
    intro st ts os st' why _ h
    simp only [runCycles] at h
    cases h
    simp [refTrace]
  | succ n ih =>
    intro st ts os st' why hq h
    simp only [runCycles] at h
    cases hco : cycleStep ctx E st with
    | error e => rw [hco] at h; cases h
    | ok co =>
      rw [hco] at h
      simp only at h
      obtain ⟨hqn, hcases⟩ := cycleStep_sound hs hq hco
      by_cases ha : co.applied = true
      · have happ : interpretDesign ctx.D co.tick.edges co.tick.input (E.view st.s) =
            { outputs := co.outs, nextState := E.view co.next.s } := by
          rcases hcases with ⟨_, he⟩ | ⟨hf, _⟩
          · exact he
          · rw [ha] at hf; cases hf
        simp only [ha, ↓reduceIte] at h
        cases hst : co.status with
        | some w =>
          rw [hst] at h
          cases h
          simp [refTrace, happ]
        | none =>
          rw [hst] at h
          simp only at h
          cases hres : runCycles ctx E n co.next with
          | error e => rw [hres] at h; cases h
          | ok res =>
            obtain ⟨ts1, os1, st1, why1⟩ := res
            rw [hres] at h
            cases h
            obtain ⟨h1, h2⟩ := ih co.next _ _ _ _ hqn hres
            simp [refTrace, happ, h1, h2]
      · have hf : co.applied = false := by simpa using ha
        have hheld : E.view co.next.s = E.view st.s := by
          rcases hcases with ⟨ht, _⟩ | ⟨_, he⟩
          · rw [hf] at ht; cases ht
          · exact he
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

/-! ## Route `fp`: the outputs exact, the state agreeing below every memory's depth

The `fp` stepper runs the residual on the state converted to the residual's (`Ckpt.save`, then
`CkptStateM.toStateM`), which holds each memory as a finite table. So its next state agrees with
`interpretDesign`'s where a step can see it (flops at their width, memory words below their
depth: `StateAgree`), not as functions everywhere. That is all a run shows: `runCycles_agree`
gives the reference trace's outputs on the recorded ticks, and a final state that agrees with
the reference's (so its checkpoint, which reads only those words, is the reference's). -/

section
open Projection.CkptResume (StateAgree WordsFit MemAgree step_agree restore_save_agree wordsFit_step
  wordsFit_restore mems_size_step)
open Projection Projection.MemProjection Projection.HwMem

/-- A state the `fp` route converts exactly where a step can see it: one memory per declared
memory, every word below its memory's depth within its memory's width. -/
def Fits (D : DesignCert) (s : RuntimeState) : Prop := s.mems.size = D.memories.size ∧ WordsFit D s

/-- `r` is `interpretDesign`'s step on `s`, up to agreement below depth, and still fits. -/
def AgreeR (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState)
    (r : RuntimeResult) : Prop :=
  r.outputs = (interpretDesign D e i s).outputs ∧
  StateAgree D r.nextState (interpretDesign D e i s).nextState ∧ Fits D r.nextState

/-- The stepper's every accepted step, from a state that fits, is `AgreeR`. -/
def StepAgree (D : DesignCert)
    (step : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult) : Prop :=
  ∀ e i s r, Fits D s → step e i s = .ok r → AgreeR D e i s r

theorem StateAgree.refl' (D : DesignCert) (s : RuntimeState) : StateAgree D s s :=
  ⟨fun _ _ => rfl, fun _ _ => MemAgree.refl _ _⟩

theorem depth_le_of_depthError {D : DesignCert} (h : MemAddr.depthError D = none) :
    ∀ k (hk : k < D.memories.size), D.memories[k].depth ≤ 2 ^ D.memories[k].aw := by
  intro k hk
  unfold MemAddr.depthError at h
  split at h
  · cases h
  · rename_i hf
    have hno := Array.findIdx?_eq_none_iff.mp hf D.memories[k] (Array.getElem_mem hk)
    simp only [Bool.or_eq_false_iff, beq_eq_false_iff_ne, decide_eq_false_iff_not, Nat.not_lt] at hno
    exact hno.2

/-- The checkpoint of any state keeps each memory's depth and address width, so a certificate
whose depths fit their tables gives `toStateM_denote`'s side condition. -/
theorem save_depths {D : DesignCert} (h : MemAddr.depthError D = none) (hash cycle : Nat)
    (exts : Array Ckpt.Ext) (s : RuntimeState) :
    ∀ m ∈ (Ckpt.save D hash cycle exts s).mems, m.depth ≤ 2 ^ m.aw := by
  intro m hm
  obtain ⟨k, hk, rfl⟩ := Array.mem_iff_getElem.mp hm
  have hk' : k < D.memories.size := by simpa [Ckpt.save] using hk
  simp only [Ckpt.save, Array.getElem_mapIdx, Ckpt.saveMem]
  exact depth_le_of_depthError h k hk'

theorem runMemSimS_eq_of {sim : MemSimulator} {sh : Option (Program × Nat)}
    (hchk : Hw.checkResidual sim.prog = some sim.bound)
    (hsh : sh = ShareTails.shareSim sim ∨ sh = none) (e : ClockEdges) (i : RuntimeInput)
    (st : StateM) : ShareTails.runMemSimS sim sh e i st = runMemSim sim e i st := by
  rcases hsh with rfl | rfl
  · exact ShareTails.runMemSimS_eq hchk e i st
  · unfold ShareTails.runMemSimS ShareTails.stepEnc
    rw [MemRunFast.runMemSim_eq_enc]

/-- **The `fp` stepper agrees with `interpretDesign`.** For the residual of `D` (fused, plain
writes admitted, `runMemSimWF_correct`), with or without its tails shared. -/
theorem fpStep_agree {D : DesignCert} {R : Program} {sf wf : Nat} {sim : MemSimulator}
    {sh : Option (Program × Nat)}
    (hsem : Direct.DesignSemWF D) (haddr : MemAddr.addrError D = none)
    (hdepth : MemAddr.depthError D = none) (hroot : Ckpt.imgRootError D = none)
    (hword : Ckpt.wordError D = none)
    (hproj : MemFused.projectMemFDesignAt sf wf D = .ok R) (hsim : mkMemSimOfW D R = some sim)
    (hsh : sh = ShareTails.shareSim sim ∨ sh = none) :
    StepAgree D (fpStep D ⟨sim, sh⟩) := by
  intro e i s r hfit h
  obtain ⟨hd, hp, hchk0, -⟩ := mkMemSimOfW_parts hsim
  have hchk : Hw.checkResidual sim.prog = some sim.bound := by rw [hp]; exact hchk0
  have hw : MemFused.MemSimWFAtWF sf wf sim := MemFused.mkMemSimOfW_wfF hproj hsim
  unfold fpStep at h
  simp only at h
  split at h
  · rename_i hR
    have hR' : runtimeFullOKB sim.design i
        (CkptStateM.toStateM (Ckpt.save D 0 0 #[] s)) e = true := by rw [hd]; exact hR
    obtain ⟨r', hrun, hden, -⟩ := MemFused.runMemSimWF_correct hw hR'
    rw [runMemSimS_eq_of hchk hsh, hrun] at h
    simp only [Except.ok.injEq] at h
    subst h
    rw [hden, hd, CkptStateM.toStateM_denote _ (save_depths hdepth 0 0 #[] s)]
    have hag : StateAgree D (Ckpt.restore (Ckpt.save D 0 0 #[] s)) s :=
      restore_save_agree 0 0 #[] hfit.1 hfit.2
    obtain ⟨ho, hn⟩ := step_agree (i := i) hsem haddr hroot hag e
    refine ⟨ho, hn, mems_size_step e, ?_⟩
    exact wordsFit_step hsem hroot hword (wordsFit_restore (Ckpt.validate_save D 0 0 #[] s hroot hword)) e
  · cases h

theorem fpChecks_none {D : DesignCert} (h : fpChecks D = none) :
    Direct.DesignSemWF D ∧ MemAddr.addrError D = none ∧ MemAddr.depthError D = none ∧
      Ckpt.imgRootError D = none ∧ Ckpt.wordError D = none := by
  unfold fpChecks at h
  split at h
  · cases h
  · rename_i u hc
    have hsem := Direct.checkDesign_sound hc
    split at h <;> (try (rename_i h1 h2 h3 h4; exact ⟨hsem, h1, h2, h3, h4⟩)) <;> cases h

theorem fpResidual_ok {D : DesignCert} {R : Program} (h : fpResidual D = .ok R) :
    ∃ sf wf, MemFused.projectMemFDesignAt sf wf D = .ok R := by
  unfold fpResidual at h
  dsimp only at h
  split at h
  · rename_i R' hR
    cases h
    exact ⟨_, _, hR⟩
  · split at h
    · rename_i R' _ hR
      cases h
      exact ⟨_, _, hR⟩
    · cases h
  · cases h

/-- The `fp` stepper `mkStepper` builds agrees with `interpretDesign`. -/
theorem mkStepper_fp_agree {D : DesignCert} {st : Stepper} {opt : Bool}
    (h : mkStepper D .fp opt = .ok st) : StepAgree D st.step := by
  simp only [mkStepper] at h
  cases hc : fpChecks D with
  | some e => simp [hc] at h
  | none =>
    obtain ⟨hsem, haddr, hdepth, hroot, hword⟩ := fpChecks_none hc
    cases hr : fpResidual D with
    | error e => simp [hc, hr] at h
    | ok R =>
      obtain ⟨sf, wf, hproj⟩ := fpResidual_ok hr
      cases hs : mkMemSimOfW D R with
      | none => simp [hc, hr, hs] at h
      | some sim =>
        simp only [hc, hr, hs, Except.ok.injEq] at h
        subst h
        exact fpStep_agree hsem haddr hdepth hroot hword hproj hs
          (by cases opt <;> simp)

/-! ### One cycle and a run, up to agreement -/

/-- An engine that agrees with `interpretDesign` below depth (the `fp` route's form): a loaded or
reloaded state has `Inv` and stands for a runtime state that fits and agrees with the one given
(the encoded `fp` engine loads through a checkpoint, which keeps the words below each depth);
from a state with `Inv` whose runtime state fits, every step gives `interpretDesign`'s outputs and
a state with `Inv` that agrees with `interpretDesign`'s next state and fits; every peek gives the
outputs and a state with `Inv` standing for the same runtime state. -/
structure EngineAgree {σ : Type} (D : DesignCert) (E : Engine σ) (Inv : σ → Prop) : Prop where
  load   : ∀ s, Fits D s →
    Inv (E.load s) ∧ Fits D (E.view (E.load s)) ∧ StateAgree D (E.view (E.load s)) s
  reload : ∀ q s, Inv q → Fits D s →
    Inv (E.reload q s) ∧ Fits D (E.view (E.reload q s)) ∧ StateAgree D (E.view (E.reload q s)) s
  step   : ∀ q, Inv q → Fits D (E.view q) → ∀ e i o q', E.step e i q = .ok (o, q') →
    Inv q' ∧ AgreeR D e i (E.view q) { outputs := o, nextState := E.view q' }
  peek   : ∀ pk, E.peek? = some pk → ∀ q, Inv q → Fits D (E.view q) → ∀ e i o q',
    pk e i q = .ok (o, q') → Inv q' ∧ o = (interpretDesign D e i (E.view q)).outputs ∧
      E.view q' = E.view q

/-- A step on runtime states that agrees (`StepAgree`) is an engine that agrees. -/
theorem ofStep_engineAgree {D : DesignCert} {name : String}
    {f : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult}
    (h : StepAgree D f) : EngineAgree D (Engine.ofStep name f) (fun _ => True) where
  load s hs := ⟨trivial, hs, StateAgree.refl' D s⟩
  reload _ s _ hs := ⟨trivial, hs, StateAgree.refl' D s⟩
  step q _ hfit e i o q' hs := by
    simp only [Engine.ofStep] at hs
    split at hs
    · cases hs
    · rename_i r hr
      cases hs
      exact ⟨trivial, h e i q r hfit hr⟩
  peek pk hp := by cases hp

theorem LoopSpec.ofAgree {σ : Type} {E : Engine σ} {D : DesignCert} {Inv : σ → Prop}
    (h : EngineAgree D E Inv) :
    LoopSpec E (fun q => Inv q ∧ Fits D (E.view q))
      (fun s e i o s' => AgreeR D e i s { outputs := o, nextState := s' }) where
  step q hq e i o q' hs := by
    obtain ⟨hq', hag⟩ := h.step q hq.1 hq.2 e i o q' hs
    exact ⟨⟨hq', hag.2.2⟩, hag⟩
  peek pk hp q hq e i o q' hs := by
    obtain ⟨hq', -, hv⟩ := h.peek pk hp q hq.1 hq.2 e i o q' hs
    refine ⟨⟨hq', ?_⟩, hv⟩
    rw [hv]; exact hq.2

/-- **A closed-loop run on an engine that agrees is the reference trace of its ticks**: from a
state `t` that the run's start state agrees with, `refTrace` on the recorded ticks prints the
run's outputs and ends in a state the run's final state agrees with. -/
theorem runCycles_agree {σ : Type} {ctx : Ctx} {E : Engine σ} {Inv : σ → Prop}
    (hs : EngineAgree ctx.D E Inv) (hsem : Direct.DesignSemWF ctx.D)
    (haddr : MemAddr.addrError ctx.D = none) (hroot : Ckpt.imgRootError ctx.D = none) :
    ∀ (n : Nat) (st : LoopState σ) (t : RuntimeState) (ts : List Tick) (os : List (Array BV))
      (st' : LoopState σ) (why : Option String),
      Inv st.s → Fits ctx.D (E.view st.s) → StateAgree ctx.D (E.view st.s) t →
      runCycles ctx E n st = .ok (ts, os, st', why) →
      (refTrace ctx.D t ts).steps.map (·.outputs) = os ∧
      StateAgree ctx.D (E.view st'.s) (refTrace ctx.D t ts).finalState := by
  intro n
  induction n with
  | zero =>
    intro st t ts os st' why _ _ hag h
    simp only [runCycles, Except.ok.injEq, Prod.mk.injEq] at h
    obtain ⟨rfl, rfl, rfl, -⟩ := h
    exact ⟨rfl, hag⟩
  | succ n ih =>
    intro st t ts os st' why hq hfit hag h
    simp only [runCycles] at h
    cases hco : cycleStep ctx E st with
    | error e => rw [hco] at h; cases h
    | ok co =>
      rw [hco] at h
      simp only at h
      obtain ⟨⟨hqn, hfitn⟩, hcases⟩ := cycleStep_spec (LoopSpec.ofAgree hs) ⟨hq, hfit⟩ hco
      rcases hcases with ⟨ha, ho, hn, -⟩ | ⟨ha, hheld⟩
      · -- the applied step, carried from `E.view st.s` to `t`
        dsimp only at ho hn
        obtain ⟨ho', hn'⟩ := step_agree (i := co.tick.input) hsem haddr hroot hag co.tick.edges
        have hout : co.outs = (interpretDesign ctx.D co.tick.edges co.tick.input t).outputs := by
          rw [← ho', ← ho]
        have hnext : StateAgree ctx.D (E.view co.next.s)
            (interpretDesign ctx.D co.tick.edges co.tick.input t).nextState :=
          hn.trans hn'
        simp only [ha, ↓reduceIte] at h
        cases hst : co.status with
        | some w =>
          rw [hst] at h
          simp only [Except.ok.injEq, Prod.mk.injEq] at h
          obtain ⟨rfl, rfl, rfl, -⟩ := h
          simp only [Direct.refTrace, List.map_cons, List.map_nil]
          exact ⟨by rw [hout], hnext⟩
        | none =>
          rw [hst] at h
          simp only at h
          cases hres : runCycles ctx E n co.next with
          | error e => rw [hres] at h; cases h
          | ok res =>
            obtain ⟨ts1, os1, st1, why1⟩ := res
            rw [hres] at h
            simp only [Except.ok.injEq, Prod.mk.injEq] at h
            obtain ⟨rfl, rfl, rfl, -⟩ := h
            obtain ⟨h1, h2⟩ := ih co.next _ _ _ _ _ hqn hfitn hnext hres
            simp only [Direct.refTrace, List.map_cons]
            exact ⟨by rw [h1, hout], h2⟩
      · simp only [ha, Bool.false_eq_true, ↓reduceIte, Except.ok.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl, rfl, -⟩ := h
        exact ⟨rfl, by rw [hheld]; exact hag⟩

/-- …in particular on a step that agrees (`fp`'s, `mkStepper_fp_agree`), from the run's own start
state: the run prints `refTrace`'s outputs. -/
theorem runCycles_fp_outputs {ctx : Ctx}
    {step : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult}
    (hs : StepAgree ctx.D step) (hc : fpChecks ctx.D = none) {n : Nat}
    {st : LoopState RuntimeState} {ts : List Tick} {os : List (Array BV)}
    {st' : LoopState RuntimeState} {why : Option String} (hfit : Fits ctx.D st.s)
    (h : runCycles ctx (Engine.ofStep "fp" step) n st = .ok (ts, os, st', why)) :
    (refTrace ctx.D st.s ts).steps.map (·.outputs) = os ∧
    StateAgree ctx.D st'.s (refTrace ctx.D st.s ts).finalState := by
  obtain ⟨hsem, haddr, _, hroot, _⟩ := fpChecks_none hc
  exact runCycles_agree (ofStep_engineAgree hs) hsem haddr hroot n st st.s ts os st' why trivial
    hfit (StateAgree.refl' _ _) h

end

#audit_axioms HDLProof.Run.compacted_outputs
#audit_axioms HDLProof.Run.runCycles_sound
#audit_axioms HDLProof.Run.fpStep_agree
#audit_axioms HDLProof.Run.mkStepper_fp_agree
#audit_axioms HDLProof.Run.runCycles_agree
#audit_axioms HDLProof.Run.runCycles_fp_outputs
#audit_axioms HDLProof.Run.cycleStep_spec
#audit_axioms HDLProof.Run.mkStepper_ir_sound

end Run
end HDLProof
