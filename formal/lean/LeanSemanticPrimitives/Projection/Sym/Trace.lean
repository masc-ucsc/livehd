/-
`Sym.Trace` -- the concrete runs the unbounded rules talk about (M3 S3).

`resM D e i st` is THE memory-route result of one step (chosen; unique by
`Eval_det`), and `resM_residual` says the projection's residual evaluates to
it. `traceM` iterates it from an initial `StateM` with every clock firing,
and `traceM_denote` ties the trace to the shared reference semantics:
`denote (traceM n)` is the `interpretDesign` state after `n` cycles and
`outM n` its outputs at cycle `n`.
-/
import LeanSemanticPrimitives.Projection.Sym.Step

namespace Projection.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-- The edge vector with every clock firing. -/
def edgesAll (D : DesignCert) : ClockEdges := allEdges D.clocks.size

/-- One step's result, when the step is admissible. -/
noncomputable def resM (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (st : StateM) :
    StateM.ResultM :=
  if h : admitsB D = true ∧ runtimeFullOKB D i st e = true then
    Classical.choose (admits_run_full h.1 h.2)
  else default

theorem resM_spec {D : DesignCert} {e : ClockEdges} {i : RuntimeInput} {st : StateM}
    (hA : admitsB D = true) (hR : runtimeFullOKB D i st e = true) :
    Eval hwMemP [] (.call hwMemP.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
      (StateM.encResultM (resM D e i st))
    ∧ (resM D e i st).denote = interpretDesign D e i (StateM.denote st)
    ∧ stateFullOKB D (resM D e i st).nextState = true := by
  have h : admitsB D = true ∧ runtimeFullOKB D i st e = true := ⟨hA, hR⟩
  unfold resM
  rw [dif_pos h]
  exact Classical.choose_spec (admits_run_full h.1 h.2)

/-- The projection's residual evaluates to `resM`. -/
theorem resM_residual {sf wf : Nat} {D : DesignCert} {R : Program}
    {e : ClockEdges} {i : RuntimeInput} {st : StateM}
    (hproj : projectMemDesignAt sf wf D = .ok R) (hA : admitsB D = true)
    (hR : runtimeFullOKB D i st e = true) :
    Eval R [] (.call R.entry ([encEdges e, encInput i, StateM.encStateM st].map Term.lit))
      (StateM.encResultM (resM D e i st)) := by
  have hev := (resM_spec hA hR).1
  have hiff : ∀ v, Eval hwMemP [] (.call hwMemP.entry
      ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
        ↔ v = StateM.encResultM (resM D e i st) :=
    fun v => ⟨fun hv => Surface.Eval_det hv hev, fun hv => hv ▸ hev⟩
  exact (specializeFour_correct_of hwMemAP_entry hwMemAP_erases hiff hproj _).mpr rfl

/-- `symStep` against ANY value the residual evaluates to. -/
theorem symStep_eval {R : Program} {e : ClockEdges} {i : RuntimeInput} {stM : StateM} {v : Val}
    (hev : Eval R [] (.call R.entry ([encEdges e, encInput i, StateM.encStateM stM].map Term.lit)) v)
    {σ : Asg} {fuel : Nat} {st0 stS stF : SymSt} {sE sI sS sres : SVal}
    (hsym : symStep R fuel st0 sE sI sS = .ok (sres, stS))
    (hc : stF.Consistent σ) (hx : stS.Extends stF)
    (rE : SRel σ sE (encEdges e)) (rI : SRel σ sI (encInput i))
    (rS : SRel σ sS (StateM.encStateM stM)) : SRel σ sres v := by
  unfold symStep at hsym
  split at hsym
  · rename_i fd hfd
    split at hsym
    · cases hev with
      | call hl hf ha hb =>
        have hvs := EvalList_lits_inj _ _ hl
        subst hvs
        rw [hfd] at hf
        cases hf
        exact symEval_sound hc _ _ _ _ _ _ hsym hx _ _ (.cons rE (.cons rI (.cons rS .nil))) hb
    · cases hsym
  · cases hsym

/-! ## The trace -/

/-- The state after `n` cycles, every clock firing, inputs `ins`. -/
noncomputable def traceM (D : DesignCert) (s0 : StateM) (ins : Nat → RuntimeInput) : Nat → StateM
  | 0 => s0
  | n + 1 => (resM D (edgesAll D) (ins n) (traceM D s0 ins n)).nextState

/-- The outputs of cycle `n`. -/
noncomputable def outM (D : DesignCert) (s0 : StateM) (ins : Nat → RuntimeInput) (n : Nat) : Array BV :=
  (resM D (edgesAll D) (ins n) (traceM D s0 ins n)).outputs

/-- The shared reference semantics, iterated. -/
def refState (D : DesignCert) (r0 : RuntimeState) (ins : Nat → RuntimeInput) : Nat → RuntimeState
  | 0 => r0
  | n + 1 => (interpretDesign D (edgesAll D) (ins n) (refState D r0 ins n)).nextState

def refOut (D : DesignCert) (r0 : RuntimeState) (ins : Nat → RuntimeInput) (n : Nat) : Array BV :=
  (interpretDesign D (edgesAll D) (ins n) (refState D r0 ins n)).outputs

/-- While every step is admissible, the finite-memory trace DENOTES the
reference trace, and its outputs are the reference outputs. -/
theorem traceM_denote {D : DesignCert} {s0 : StateM} {ins : Nat → RuntimeInput}
    (hA : admitsB D = true)
    (hR : ∀ n, runtimeFullOKB D (ins n) (traceM D s0 ins n) (edgesAll D) = true) :
    ∀ n, StateM.denote (traceM D s0 ins n) = refState D (StateM.denote s0) ins n
      ∧ outM D s0 ins n = refOut D (StateM.denote s0) ins n := by
  intro n
  induction n with
  | zero =>
      have hd := (resM_spec hA (hR 0)).2.1
      refine ⟨rfl, ?_⟩
      have := congrArg RuntimeResult.outputs hd
      simpa [outM, refOut, refState, traceM, StateM.ResultM.denote] using this
  | succ m ih =>
      have hd := (resM_spec hA (hR m)).2.1
      have hn : StateM.denote (traceM D s0 ins (m + 1)) = refState D (StateM.denote s0) ins (m + 1) := by
        have := congrArg RuntimeResult.nextState hd
        simp only [StateM.ResultM.denote] at this
        simp only [traceM, refState, this, ih.1]
      refine ⟨hn, ?_⟩
      have hd' := (resM_spec hA (hR (m + 1))).2.1
      have := congrArg RuntimeResult.outputs hd'
      simp only [StateM.ResultM.denote] at this
      simp only [outM, refOut, this, hn]

end Projection.Sym
