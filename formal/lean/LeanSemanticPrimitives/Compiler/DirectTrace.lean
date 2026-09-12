/-
# `DirectTrace` — multi-cycle direct execution

Direction 2, phase 4.

The multi-cycle semantics is ITERATION, not a second semantic definition: at
cycle `t+1` the state is the `nextState` returned at cycle `t`, and nothing else
about the cycle boundary is new.  So `refTrace` iterates `interpretDesign`,
`runDirect` iterates `directStepRaw`, and the trace theorem is an induction over
the input list whose only interesting step is `directStepRaw_correct`.

## Why the design is checked once and the state is not re-checked

`checkDesign` is O(nodes); running it every cycle would double the cost of a
trace for no information, because a certificate does not change between cycles.
`checkRuntime` IS re-run per cycle, because the input vector changes — but the
STATE shape never has to be re-established: `directStepRaw_sizes` proves each
step returns exactly `D.flops.size` flops and `D.memories.size` memories.

## Initial state

Not every design has a canonical initial state, so the API takes one.
`zeroState` is provided as STIMULUS — a convenience for designs that are reset
by their input trace — and is deliberately outside the semantic definitions.
-/
import LeanSemanticPrimitives.Compiler.DirectSemantics

namespace Compiler
namespace Direct

/-- A trace: one `RuntimeResult` per applied input, plus the state left over. -/
structure TraceResult where
  steps      : List RuntimeResult
  finalState : RuntimeState
deriving Inhabited

--------------------------------------------------------------------------------
-- The reference trace: iterate the specification
--------------------------------------------------------------------------------

def refTrace (D : DesignCert) : RuntimeState → List RuntimeInput → TraceResult
  | s, []      => { steps := [], finalState := s }
  | s, i :: is =>
      let r := interpretDesign D i s
      let t := refTrace D r.nextState is
      { steps := r :: t.steps, finalState := t.finalState }

--------------------------------------------------------------------------------
-- The direct trace
--------------------------------------------------------------------------------

/-- Unchecked iteration.  `runDirect` is the public entry point. -/
def runDirectRaw (D : DesignCert) : RuntimeState → List RuntimeInput → TraceResult
  | s, []      => { steps := [], finalState := s }
  | s, i :: is =>
      let r := directStepRaw D i s
      let t := runDirectRaw D r.nextState is
      { steps := r :: t.steps, finalState := t.finalState }

/-- Iteration with the per-cycle runtime shape check.  The certificate itself is
checked once, by `runDirect`. -/
def runDirectFrom (D : DesignCert) : RuntimeState → List RuntimeInput →
    Except SimError TraceResult
  | s, []      => .ok { steps := [], finalState := s }
  | s, i :: is =>
    match checkRuntime D i s with
    | .error e => .error e
    | .ok _ =>
      let r := directStepRaw D i s
      match runDirectFrom D r.nextState is with
      | .error e => .error e
      | .ok t    => .ok { steps := r :: t.steps, finalState := t.finalState }

/-- **The public multi-cycle API.** -/
def runDirect (D : DesignCert) (s : RuntimeState) (is : List RuntimeInput) :
    Except SimError TraceResult :=
  match checkDesign D with
  | .error e => .error e
  | .ok _    => runDirectFrom D s is

--------------------------------------------------------------------------------
-- The trace theorem
--------------------------------------------------------------------------------

theorem runDirectRaw_correct (D : DesignCert) (hdb : DesignCert.DepsBounded D) :
    ∀ (s : RuntimeState) (is : List RuntimeInput), runDirectRaw D s is = refTrace D s is := by
  intro s is
  induction is generalizing s with
  | nil => rfl
  | cons i is ih =>
      simp only [runDirectRaw, refTrace, directStepRaw_correct D hdb i s, ih]

/-- Every SUCCESSFUL checked trace is the reference trace. -/
theorem runDirectFrom_correct (D : DesignCert) (hdb : DesignCert.DepsBounded D) :
    ∀ (s : RuntimeState) (is : List RuntimeInput) (t : TraceResult),
      runDirectFrom D s is = .ok t → t = refTrace D s is := by
  intro s is
  induction is generalizing s with
  | nil =>
      intro t h
      simp only [runDirectFrom] at h
      injection h with h
      exact h.symm
  | cons i is ih =>
      intro t h
      simp only [runDirectFrom] at h
      cases hr : checkRuntime D i s with
      | error e => rw [hr] at h; exact absurd h (by simp)
      | ok _ =>
          rw [hr] at h
          cases ht : runDirectFrom D (directStepRaw D i s).nextState is with
          | error e => rw [ht] at h; exact absurd h (by simp)
          | ok t' =>
              rw [ht] at h
              injection h with h
              have hrec := ih (directStepRaw D i s).nextState t' ht
              have hstep := directStepRaw_correct D hdb i s
              rw [← h, hrec, hstep]
              simp only [refTrace]

/-- **`runDirect_correct`.**  Iterating the direct evaluator produces exactly the
trace obtained by iterating the reference one-cycle semantics.  As with
`directStep_correct`, the `.ok` premise is the only semantic hypothesis. -/
theorem runDirect_correct (D : DesignCert) (s : RuntimeState) (is : List RuntimeInput)
    (t : TraceResult) (h : runDirect D s is = .ok t) : t = refTrace D s is := by
  unfold runDirect at h
  cases hc : checkDesign D with
  | error e => rw [hc] at h; exact absurd h (by simp)
  | ok _ =>
      rw [hc] at h
      exact runDirectFrom_correct D (checkDesign_sound hc).depsBounded s is t h

/-- Each successful step of a trace is also a `directStep` result, so the
one-cycle theorem applies pointwise to a trace. -/
theorem runDirectFrom_step (D : DesignCert) (s : RuntimeState) (i : RuntimeInput)
    (is : List RuntimeInput) (t : TraceResult) (hd : checkDesign D = .ok ())
    (h : runDirectFrom D s (i :: is) = .ok t) :
    ∃ r ts, t.steps = r :: ts ∧ directStep D i s = .ok r := by
  simp only [runDirectFrom] at h
  cases hr : checkRuntime D i s with
  | error e => rw [hr] at h; exact absurd h (by simp)
  | ok u =>
      rw [hr] at h
      cases ht : runDirectFrom D (directStepRaw D i s).nextState is with
      | error e => rw [ht] at h; exact absurd h (by simp)
      | ok t' =>
          rw [ht] at h
          injection h with h
          refine ⟨directStepRaw D i s, t'.steps, by rw [← h], ?_⟩
          unfold directStep
          rw [hd, hr]

--------------------------------------------------------------------------------
-- Trace length and state shape
--------------------------------------------------------------------------------

theorem runDirectFrom_length (D : DesignCert) :
    ∀ (s : RuntimeState) (is : List RuntimeInput) (t : TraceResult),
      runDirectFrom D s is = .ok t → t.steps.length = is.length := by
  intro s is
  induction is generalizing s with
  | nil => intro t h; simp only [runDirectFrom] at h; injection h with h; rw [← h]; rfl
  | cons i is ih =>
      intro t h
      simp only [runDirectFrom] at h
      cases hr : checkRuntime D i s with
      | error e => rw [hr] at h; exact absurd h (by simp)
      | ok _ =>
          rw [hr] at h
          cases ht : runDirectFrom D (directStepRaw D i s).nextState is with
          | error e => rw [ht] at h; exact absurd h (by simp)
          | ok t' =>
              rw [ht] at h
              injection h with h
              rw [← h]
              simp only [List.length_cons, ih _ t' ht]

--------------------------------------------------------------------------------
-- Stimulus helpers (NOT part of the semantics)
--------------------------------------------------------------------------------

/-- An all-zero state of the shape the certificate declares.  A convenience for
designs whose input trace drives reset; a design without complete reset logic
must be given its initial state explicitly, which is why the semantic API takes
a `RuntimeState` rather than inventing one. -/
def zeroState (D : DesignCert) : RuntimeState :=
  { flops := D.flops.map fun f => mk_bv f.width 0
    mems  := D.memories.map fun m => (fun _ => mk_bv m.dw 0) }

theorem zeroState_flops (D : DesignCert) : (zeroState D).flops.size = D.flops.size := by
  simp [zeroState]

theorem zeroState_mems (D : DesignCert) : (zeroState D).mems.size = D.memories.size := by
  simp [zeroState]

/-- An all-zero primary-input vector wide enough for every ordinal the design
reads, at each ordinal's declared width. -/
def zeroInput (D : DesignCert) : RuntimeInput :=
  (Array.range (inputArity D)).map fun idx => mk_bv ((inputWidthOf D idx).getD 1) 0

theorem zeroInput_size (D : DesignCert) : (zeroInput D).size = inputArity D := by
  simp [zeroInput]

end Direct
end Compiler
