import HDLProof.Theory.Channels

/-!
# Stuttering refinement of an implementation run to a channel spec

An implementation runs one cycle at a time. Each cycle maps to a list of spec events: an
empty list is a stutter, and several events are allowed (several commits in one cycle).
- `Exec` is one run as the hardware checker sees it: from cycle `R0` (after reset) on, the
  architectural state `arch n` and the events `evs n` of every cycle.
- `Exec.Refines` says every cycle's events are allowed and take `arch n` to `arch (n+1)`.
- `Exec.upTo n` lists the events of cycles before `n`. Refinement makes it a valid spec run from
  `arch R0` that ends in `arch n` (`upTo_valid`), so the run's channel streams are the spec's.

`PipelineRefinement` is the user-facing API: a machine, a projection to architectural state, an
invariant and a decoding of each cycle into spec events. `PipelineRefinement.exec_refines` turns
it into an `Exec.Refines` for every input sequence. With at most one event per cycle,
`step_refines` reads "`project s' = project s` or `project s' = step (project s) e`".
-/

namespace HDLProof.Theory

variable {S C O R : Type}

/-- A Mealy machine. -/
structure Mealy (St I Out : Type) where
  step : St → I → St
  out  : St → I → Out

/-- The state after the first `n` inputs. -/
def Mealy.trace {St I Out : Type} (M : Mealy St I Out) (s0 : St) (ins : Nat → I) : Nat → St
  | 0 => s0
  | n + 1 => M.step (M.trace s0 ins n) (ins n)

/-- One implementation run against a spec: the architectural state and the spec events of each
cycle from `R0` on. -/
structure Exec (S C R : Type) where
  R0   : Nat
  arch : Nat → S
  evs  : Nat → List (Ev C R)

namespace Exec

variable (X : Exec S C R)

/-- The events of cycles `R0 .. n-1`, in order. -/
def upTo : Nat → List (Ev C R)
  | 0 => []
  | n + 1 => upTo n ++ (if X.R0 ≤ n then X.evs n else [])

/-- Every cycle from `R0` on takes allowed events from `arch n` to `arch (n+1)`. -/
def Refines (sp : Spec S C O R) : Prop :=
  ∀ n, X.R0 ≤ n → sp.Valid (X.arch n) (X.evs n) ∧ X.arch (n + 1) = sp.run (X.arch n) (X.evs n)

variable {X}

theorem upTo_le : ∀ n, n ≤ X.R0 → X.upTo n = []
  | 0, _ => rfl
  | n + 1, h => by
    simp only [upTo, upTo_le n (Nat.le_of_succ_le h), List.nil_append]
    simp [show ¬ X.R0 ≤ n by omega]

theorem upTo_prefix (n m : Nat) (h : n ≤ m) : X.upTo n <+: X.upTo m := by
  induction m with
  | zero => rw [Nat.le_zero.mp h]; exact List.prefix_refl _
  | succ m ih =>
    rcases Nat.lt_or_ge n (m + 1) with hl | hl
    · exact List.IsPrefix.trans (ih (by omega)) (List.prefix_append _ _)
    · rw [show n = m + 1 by omega]; exact List.prefix_refl _

/-- The events so far are a valid spec run from `arch R0` that ends in `arch n`. -/
theorem upTo_valid {sp : Spec S C O R} (h : X.Refines sp) :
    ∀ k, sp.Valid (X.arch X.R0) (X.upTo (X.R0 + k)) ∧
      sp.run (X.arch X.R0) (X.upTo (X.R0 + k)) = X.arch (X.R0 + k)
  | 0 => by rw [Nat.add_zero, upTo_le _ (Nat.le_refl _)]; exact ⟨trivial, rfl⟩
  | k + 1 => by
    obtain ⟨hv, hr⟩ := upTo_valid h k
    obtain ⟨hv', hr'⟩ := h (X.R0 + k) (Nat.le_add_right _ _)
    rw [show X.R0 + (k + 1) = (X.R0 + k) + 1 by omega]
    simp only [upTo, show X.R0 ≤ X.R0 + k from Nat.le_add_right _ _, if_true]
    refine ⟨Spec.valid_append.mpr ⟨hv, ?_⟩, ?_⟩
    · rw [hr]; exact hv'
    · rw [Spec.run_append, hr, hr']

theorem upTo_valid' {sp : Spec S C O R} (h : X.Refines sp) (n : Nat) :
    sp.Valid (X.arch X.R0) (X.upTo n) := by
  rcases Nat.lt_or_ge n X.R0 with hn | hn
  · rw [upTo_le n (Nat.le_of_lt hn)]; trivial
  · have := (upTo_valid h (n - X.R0)).1
    rwa [show X.R0 + (n - X.R0) = n by omega] at this

end Exec

/-- The user-facing refinement API. A machine `rtl`, a projection of its state to the spec's
architectural state, an invariant, and the spec events each cycle performs. -/
structure PipelineRefinement (St I Out S C O R : Type) where
  rtl       : Mealy St I Out
  spec      : Spec S C O R
  project   : St → S
  decode    : St → I → List (Ev C R)
  invariant : St → Prop
  Initial   : St → Prop
  initial_correct     : ∀ s, Initial s → invariant s
  invariant_preserved : ∀ s i, invariant s → invariant (rtl.step s i)
  step_refines        : ∀ s i, invariant s →
    spec.Valid (project s) (decode s i) ∧ project (rtl.step s i) = spec.run (project s) (decode s i)

namespace PipelineRefinement

variable {St I Out : Type} (P : PipelineRefinement St I Out S C O R)

/-- The run of `P.rtl` from `s0` on inputs `ins`, as an `Exec`. -/
def exec (s0 : St) (ins : Nat → I) : Exec S C R where
  R0 := 0
  arch := fun n => P.project (P.rtl.trace s0 ins n)
  evs := fun n => P.decode (P.rtl.trace s0 ins n) (ins n)

theorem invariant_trace {s0 : St} (h0 : P.Initial s0) (ins : Nat → I) :
    ∀ n, P.invariant (P.rtl.trace s0 ins n)
  | 0 => P.initial_correct s0 h0
  | n + 1 => P.invariant_preserved _ _ (invariant_trace h0 ins n)

theorem exec_refines {s0 : St} (h0 : P.Initial s0) (ins : Nat → I) :
    (P.exec s0 ins).Refines P.spec :=
  fun n _ => P.step_refines _ _ (P.invariant_trace h0 ins n)

/-- With at most one event per cycle, a step either stutters or takes one spec step. -/
theorem step_or_stutter (s : St) (i : I) (hi : P.invariant s) (h1 : (P.decode s i).length ≤ 1) :
    P.project (P.rtl.step s i) = P.project s ∨
    ∃ c r, P.decode s i = [(c, r)] ∧ P.project (P.rtl.step s i) = P.spec.δ (P.project s) c r := by
  have hr := (P.step_refines s i hi).2
  match hd : P.decode s i, h1 with
  | [], _ => left; rw [hr, hd]; rfl
  | [(c, r)], _ => right; exact ⟨c, r, rfl, by rw [hr, hd]; rfl⟩

end PipelineRefinement

end HDLProof.Theory
