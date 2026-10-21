import HDLProof.Theory.Stutter

/-!
# Composing refinements

- **Horizontal (`compose`).** Two implementation runs refine the same confluent spec from the
  same architectural state, and each is answered by the same environment from its own request
  history. Then every channel's stream agrees on the common prefix, at any two cycle counts. This
  is the equivalence theorem: matching inputs (the environment), matching initial states, a
  deterministic spec.
- **Vertical (`Exec.map_refines`).** If a spec `sp1` refines a spec `sp2` event by event, every run
  refining `sp1` refines `sp2` through the composed map. Long refinement gaps can be split into
  chains.

`Spec.envAfter` and `envAfter_apply` restate obedience as "each response is `E c` of the
channel's requests so far", the form a hardware statement uses.
-/

namespace HDLProof.Theory

variable {S C O R : Type}

namespace Spec

variable (sp : Spec S C O R)

/-- The environment after it answered the events of `u`. -/
def envAfter [DecidableEq C] : Env C O R → S → List (Ev C R) → Env C O R
  | E, _, [] => E
  | E, s, (c, r) :: u => envAfter (E.push c (sp.out s c)) (sp.δ s c r) u

variable {sp}

theorem obeys_append [DecidableEq C] {u v : List (Ev C R)} :
    ∀ {E : Env C O R} {s : S},
      sp.Obeys E s (u ++ v) ↔ sp.Obeys E s u ∧ sp.Obeys (sp.envAfter E s u) (sp.run s u) v := by
  induction u with
  | nil => intro E s; simp [Obeys, envAfter, run]
  | cons e u ih =>
    intro E s
    obtain ⟨c, r⟩ := e
    simp only [List.cons_append, Obeys, envAfter, run, ih, and_assoc]

/-- The environment after `u` answers channel `x` with the requests `u` made on `x` in front. -/
theorem envAfter_apply [DecidableEq C] :
    ∀ (u : List (Ev C R)) (E : Env C O R) (s : S) (x : C) (h : List O),
      sp.envAfter E s u x h = E x ((sp.chan x s u).map Prod.fst ++ h)
  | [], _, _, _, _ => rfl
  | (c, r) :: u, E, s, x, h => by
    simp only [envAfter, chan]
    rw [envAfter_apply u]
    by_cases hc : c = x
    · subst hc; simp [Env.push]
    · have hxc : x ≠ c := fun e => hc e.symm
      simp [Env.push, hc, hxc]

theorem pres_run {P : S → Prop} (pres : ∀ s c r, P s → sp.en s c = true → P (sp.δ s c r)) :
    ∀ (u : List (Ev C R)) (s : S), P s → sp.Valid s u → P (sp.run s u)
  | [], _, h, _ => h
  | (c, r) :: u, s, h, ⟨he, hv⟩ => pres_run pres u _ (pres s c r h he) hv

end Spec

namespace Exec

/-- The run obeys `E`: every response so far is `E`'s answer to its channel's own history. -/
def Obeys [DecidableEq C] (sp : Spec S C O R) (X : Exec S C R) (E : Env C O R) : Prop :=
  ∀ n, sp.Obeys E (X.arch X.R0) (X.upTo n)

/-- Channel `x`'s stream over cycles `R0 .. n-1`. -/
def stream [DecidableEq C] (sp : Spec S C O R) (X : Exec S C R) (x : C) (n : Nat) : List (O × R) :=
  sp.chan x (X.arch X.R0) (X.upTo n)

/-- Obedience cycle by cycle implies obedience of the whole run. -/
theorem obeys_of_cycles [DecidableEq C] {sp : Spec S C O R} {X : Exec S C R} {E : Env C O R}
    (hX : X.Refines sp)
    (h : ∀ n, X.R0 ≤ n →
      sp.Obeys (sp.envAfter E (X.arch X.R0) (X.upTo n)) (X.arch n) (X.evs n)) :
    X.Obeys sp E := by
  intro n
  induction n with
  | zero => exact trivial
  | succ n ih =>
    simp only [upTo]
    rw [Spec.obeys_append]
    refine ⟨ih, ?_⟩
    by_cases hn : X.R0 ≤ n
    · simp only [hn, if_true]
      have hr : sp.run (X.arch X.R0) (X.upTo n) = X.arch n := by
        have := (upTo_valid hX (n - X.R0)).2
        rwa [show X.R0 + (n - X.R0) = n by omega] at this
      rw [hr]; exact h n hn
    · simp only [hn, if_false]; exact trivial

end Exec

/-- **Horizontal composition.** Two runs refining the same confluent spec, from the same
architectural state and answered by the same environment, agree on every channel's stream on
the common prefix, at any two cycle counts. -/
theorem compose [DecidableEq C] {sp : Spec S C O R} {P : S → Prop} (hc : sp.Confluent P)
    {X Y : Exec S C R} (hX : X.Refines sp) (hY : Y.Refines sp)
    (hP : P (X.arch X.R0)) (h0 : X.arch X.R0 = Y.arch Y.R0)
    {E : Env C O R} (hEX : X.Obeys sp E) (hEY : Y.Obeys sp E) :
    ∀ nX nY x, PrefixAgree (X.stream sp x nX) (Y.stream sp x nY) := by
  intro nX nY x
  unfold Exec.stream
  have hvY := Exec.upTo_valid' hY nY
  have hoY := hEY nY
  rw [← h0] at hvY hoY ⊢
  exact Spec.chan_agree hc _ _ E (X.upTo nX) (Y.upTo nY) (Nat.le_refl _) hP
    (Exec.upTo_valid' hX nX) hvY (hEX nX) hoY x

/-- A spec refines another event by event: each `sp1` event is a list of `sp2` events, and the
map `π` takes `sp1`'s state change to `sp2`'s. -/
structure SpecRefines {S₁ C₁ O₁ R₁ S₂ C₂ O₂ R₂ : Type}
    (sp1 : Spec S₁ C₁ O₁ R₁) (sp2 : Spec S₂ C₂ O₂ R₂) (P1 : S₁ → Prop) where
  π    : S₁ → S₂
  dec  : S₁ → Ev C₁ R₁ → List (Ev C₂ R₂)
  pres : ∀ s c r, P1 s → sp1.en s c = true → P1 (sp1.δ s c r)
  step : ∀ s c r, P1 s → sp1.en s c = true →
    sp2.Valid (π s) (dec s (c, r)) ∧ π (sp1.δ s c r) = sp2.run (π s) (dec s (c, r))

namespace SpecRefines

variable {S₁ C₁ O₁ R₁ S₂ C₂ O₂ R₂ : Type} {sp1 : Spec S₁ C₁ O₁ R₁} {sp2 : Spec S₂ C₂ O₂ R₂}
  {P1 : S₁ → Prop} (h : SpecRefines sp1 sp2 P1)

/-- The `sp2` events of a list of `sp1` events. -/
def decList : S₁ → List (Ev C₁ R₁) → List (Ev C₂ R₂)
  | _, [] => []
  | s, (c, r) :: u => h.dec s (c, r) ++ decList (sp1.δ s c r) u

theorem run_map : ∀ (u : List (Ev C₁ R₁)) (s : S₁), P1 s → sp1.Valid s u →
    sp2.Valid (h.π s) (h.decList s u) ∧ h.π (sp1.run s u) = sp2.run (h.π s) (h.decList s u)
  | [], _, _, _ => ⟨trivial, rfl⟩
  | (c, r) :: u, s, hP, ⟨he, hv⟩ => by
    obtain ⟨hv1, hr1⟩ := h.step s c r hP he
    obtain ⟨hv2, hr2⟩ := run_map u _ (h.pres s c r hP he) hv
    refine ⟨Spec.valid_append.mpr ⟨hv1, ?_⟩, ?_⟩
    · rw [← hr1]; exact hv2
    · simp only [decList, Spec.run, Spec.run_append]; rw [hr2, hr1]

end SpecRefines

/-- The run seen through a spec refinement. -/
def Exec.map {S₁ C₁ O₁ R₁ S₂ C₂ O₂ R₂ : Type} {sp1 : Spec S₁ C₁ O₁ R₁} {sp2 : Spec S₂ C₂ O₂ R₂}
    {P1 : S₁ → Prop} (h : SpecRefines sp1 sp2 P1) (X : Exec S₁ C₁ R₁) : Exec S₂ C₂ R₂ where
  R0 := X.R0
  arch := fun n => h.π (X.arch n)
  evs := fun n => h.decList (X.arch n) (X.evs n)

/-- **Vertical composition.** A run refining `sp1` from a state where `P1` holds refines `sp2`. -/
theorem Exec.map_refines {S₁ C₁ O₁ R₁ S₂ C₂ O₂ R₂ : Type} {sp1 : Spec S₁ C₁ O₁ R₁}
    {sp2 : Spec S₂ C₂ O₂ R₂} {P1 : S₁ → Prop} (h : SpecRefines sp1 sp2 P1) {X : Exec S₁ C₁ R₁}
    (hX : X.Refines sp1) (hP : P1 (X.arch X.R0)) : (X.map h).Refines sp2 := by
  have hPn : ∀ n, X.R0 ≤ n → P1 (X.arch n) := by
    intro n hn
    have := Spec.pres_run h.pres (X.upTo n) _ hP (Exec.upTo_valid' hX n)
    have hr := (Exec.upTo_valid hX (n - X.R0)).2
    rw [show X.R0 + (n - X.R0) = n by omega] at hr
    rwa [hr] at this
  intro n hn
  obtain ⟨hv, hr⟩ := hX n hn
  have := h.run_map (X.evs n) (X.arch n) (hPn n hn) hv
  refine ⟨this.1, ?_⟩
  show h.π (X.arch (n + 1)) = _
  rw [hr]; exact this.2

end HDLProof.Theory
