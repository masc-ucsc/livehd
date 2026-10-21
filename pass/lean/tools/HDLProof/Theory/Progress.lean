import HDLProof.Theory.Compose

/-!
# Progress under fairness

Refinement alone says only that everything a run does is allowed: a run that stops forever
refines any spec. Progress rules that out with a rank per channel, the classical rule for
response under fairness (Lehmann–Pnueli–Stavi; Manna–Pnueli, helpful transitions).

A run waits either on its own work or on an environment requirement `j`, such as "a pending
request is accepted". For each requirement:
- `reqEn n j` says it is enabled at cycle `n`;
- `fires n j` says its helpful action happens at cycle `n`.

The environment promises **justice**: a requirement enabled from some cycle on eventually
fires.

`Progress.Sound` is what the checker proves, on every cycle where channel `c` is allowed and
gets no event:
- the rank of `c` does not rise;
- it falls when the cycle is helpful: internal work (`sel = none`), or the awaited requirement
  fires;
- if it stays, the run still waits on the same requirement and that requirement is still
  enabled, so the environment cannot dodge.

The theorems:
- `channel_progress`: under justice, an allowed channel eventually gets its event. The spec's
  persistence keeps it allowed meanwhile.
- `helpful_bound`: within any window where `c` waits, the number of helpful cycles is at most
  the rank at the start. This is the bounded form, independent of fairness.

The rank may be a lexicographic tuple. The hardware encodes it as one concatenated bit-vector,
compared unsigned.
-/

namespace HDLProof.Theory

variable {S C O R J : Type}

/-- What a run waits on, per cycle and channel, and the environment requirements' status. -/
structure Progress (C J : Type) where
  rank  : Nat → C → Nat
  sel   : Nat → C → Option J
  reqEn : Nat → J → Bool
  fires : Nat → J → Bool

namespace Progress

variable (G : Progress C J)

/-- The cycle is helpful for channel `c`: internal work, or the awaited requirement fires. -/
def helpful (n : Nat) (c : C) : Bool :=
  match G.sel n c with
  | none => true
  | some j => G.fires n j

/-- Channel `c`'s rank obligations, on every cycle where `c` is allowed and gets no event. -/
def SoundAt (sp : Spec S C O R) (X : Exec S C R) (c : C) : Prop :=
  ∀ n, X.R0 ≤ n → sp.en (X.arch n) c = true → Spec.Free c (X.evs n) →
    G.rank (n + 1) c ≤ G.rank n c ∧
    (G.helpful n c = true → G.rank (n + 1) c < G.rank n c) ∧
    (G.rank (n + 1) c = G.rank n c →
      G.sel (n + 1) c = G.sel n c ∧ ∀ j, G.sel n c = some j → G.reqEn n j = true)

/-- Every channel's rank obligations. -/
def Sound (sp : Spec S C O R) (X : Exec S C R) : Prop := ∀ c, G.SoundAt sp X c

/-- Justice: a requirement enabled from some cycle on eventually fires. -/
def Just : Prop :=
  ∀ j n, (∀ m, n ≤ m → G.reqEn m j = true) → ∃ m, n ≤ m ∧ G.fires m j = true

end Progress

variable {G : Progress C J}

/-- An allowed channel stays allowed along events on other channels. -/
theorem Spec.en_run {sp : Spec S C O R} {P : S → Prop} (hc : sp.Confluent P) {c : C} :
    ∀ (u : List (Ev C R)) (s : S), P s → sp.Valid s u → Spec.Free c u → sp.en s c = true →
      sp.en (sp.run s u) c = true
  | [], _, _, _, _, h => h
  | (d, p) :: u, s, hP, ⟨hd, hv⟩, hf, h => by
    have hdc : d ≠ c := hf (d, p) (List.mem_cons_self ..)
    exact Spec.en_run hc u _ (hc.pres s d p hP hd) hv
      (fun e he => hf e (List.mem_cons_of_mem _ he)) (hc.persist s d c p hP hdc hd h)

/-- `P` holds at every architectural state of a refining run that starts in `P`. -/
theorem Exec.pres_arch {sp : Spec S C O R} {P : S → Prop}
    (pres : ∀ s c r, P s → sp.en s c = true → P (sp.δ s c r)) {X : Exec S C R}
    (hX : X.Refines sp) (hP : P (X.arch X.R0)) : ∀ n, X.R0 ≤ n → P (X.arch n) := by
  intro n hn
  have := Spec.pres_run pres (X.upTo n) _ hP (Exec.upTo_valid' hX n)
  have hr := (Exec.upTo_valid hX (n - X.R0)).2
  rw [show X.R0 + (n - X.R0) = n by omega] at hr
  rwa [hr] at this

/-- **Channel progress.** Under justice, a channel allowed at some cycle gets its event later. -/
theorem channel_progress [DecidableEq C] {sp : Spec S C O R} {P : S → Prop} (hc : sp.Confluent P)
    {X : Exec S C R} (hX : X.Refines sp) (hP : P (X.arch X.R0)) {c : C}
    (hS : G.SoundAt sp X c) (hJ : G.Just) :
    ∀ n, X.R0 ≤ n → sp.en (X.arch n) c = true → ∃ m, n ≤ m ∧ ∃ e ∈ X.evs m, e.1 = c := by
  intro n hn hen
  apply Classical.byContradiction
  intro hno
  -- from `n` on, `c` gets no event and stays allowed
  have hfree : ∀ m, n ≤ m → Spec.Free c (X.evs m) := by
    intro m hm e he hec
    exact hno ⟨m, hm, e, he, hec⟩
  have hPn := Exec.pres_arch hc.pres hX hP
  have hen_all : ∀ m, n ≤ m → sp.en (X.arch m) c = true := by
    intro m hm
    induction m with
    | zero => rw [Nat.le_zero.mp hm] at hen; exact hen
    | succ m ih =>
      rcases Nat.lt_or_ge n (m + 1) with h | h
      · have ihm := ih (by omega)
        rw [(hX m (by omega)).2]
        exact Spec.en_run hc (X.evs m) _ (hPn m (by omega)) (hX m (by omega)).1
          (hfree m (by omega)) ihm
      · rw [show m + 1 = n by omega]; exact hen
  have hob : ∀ m, n ≤ m → _ := fun m hm => hS m (by omega) (hen_all m hm) (hfree m hm)
  -- the rank cannot fall forever; once it stays, the awaited requirement never fires
  -- once the rank stops falling, the awaited requirement never fires: contradiction
  have stuck : ∀ m, n ≤ m → (∀ m', m ≤ m' → G.rank m c ≤ G.rank m' c) → False := by
    intro m hm hdrop'
    have hconst : ∀ m', m ≤ m' → G.rank m' c = G.rank m c := by
      intro m' hm'
      induction m' with
      | zero => rw [Nat.le_zero.mp hm']
      | succ m' ih' =>
        rcases Nat.lt_or_ge m (m' + 1) with h | h
        · have e1 := ih' (by omega)
          have le := (hob m' (by omega)).1
          have ge := hdrop' (m' + 1) (by omega)
          omega
        · rw [show m' + 1 = m by omega]
    have hstay : ∀ m', m ≤ m' → G.rank (m' + 1) c = G.rank m' c := fun m' hm' => by
      rw [hconst m' hm', hconst (m' + 1) (by omega)]
    have hsel : ∀ m', m ≤ m' → G.sel m' c = G.sel m c := by
      intro m' hm'
      induction m' with
      | zero => rw [Nat.le_zero.mp hm']
      | succ m' ih' =>
        rcases Nat.lt_or_ge m (m' + 1) with h | h
        · rw [((hob m' (by omega)).2.2 (hstay m' (by omega))).1, ih' (by omega)]
        · rw [show m' + 1 = m by omega]
    have hnohelp : ∀ m', m ≤ m' → G.helpful m' c = false := by
      intro m' hm'
      cases hh : G.helpful m' c
      · rfl
      · have := (hob m' (by omega)).2.1 hh
        have := hstay m' hm'
        omega
    cases hs : G.sel m c with
    | none =>
      have := hnohelp m (Nat.le_refl _)
      simp [Progress.helpful, hs] at this
    | some j =>
      have hreq : ∀ m', m ≤ m' → G.reqEn m' j = true := fun m' hm' =>
        (hob m' (by omega)).2.2 (hstay m' hm') |>.2 j (by rw [hsel m' hm', hs])
      obtain ⟨m', hm', hf⟩ := hJ j m hreq
      have := hnohelp m' hm'
      simp [Progress.helpful, hsel m' hm', hs, hf] at this
  -- the rank cannot fall forever
  have key : ∀ k, ∀ m, n ≤ m → G.rank m c ≤ k → False := by
    intro k
    induction k with
    | zero =>
      intro m hm hk
      exact stuck m hm (fun m' _ => by omega)
    | succ k ih =>
      intro m hm hk
      by_cases hdrop : ∃ m', m ≤ m' ∧ G.rank m' c < G.rank m c
      · obtain ⟨m', hm', hlt⟩ := hdrop
        exact ih m' (by omega) (by omega)
      · exact stuck m hm (fun m' hm' => Nat.le_of_not_lt (fun h => hdrop ⟨m', hm', h⟩))
  exact key (G.rank n c) n (Nat.le_refl _) (Nat.le_refl _)

/-- The number of helpful cycles among `n .. n+L-1`. -/
def Progress.helpfulCount (G : Progress C J) (c : C) (n : Nat) : Nat → Nat
  | 0 => 0
  | L + 1 => G.helpfulCount c n L + (if G.helpful (n + L) c then 1 else 0)

/-- **Bounded stuttering.** While `c` waits for its event, the helpful cycles plus the
remaining rank never exceed the rank at the start of the wait. -/
theorem helpful_bound {sp : Spec S C O R} {X : Exec S C R} {c : C} (hS : G.SoundAt sp X c) (n : Nat)
    (hn : X.R0 ≤ n) :
    ∀ L, (∀ i, i < L → sp.en (X.arch (n + i)) c = true ∧ Spec.Free c (X.evs (n + i))) →
      G.helpfulCount c n L + G.rank (n + L) c ≤ G.rank n c
  | 0, _ => by simp [Progress.helpfulCount]
  | L + 1, h => by
    have ih := helpful_bound hS n hn L (fun i hi => h i (by omega))
    obtain ⟨hen, hf⟩ := h L (by omega)
    obtain ⟨hle, hlt, _⟩ := hS (n + L) (by omega) hen hf
    simp only [Progress.helpfulCount, show n + (L + 1) = n + L + 1 by omega]
    by_cases hh : G.helpful (n + L) c = true
    · have := hlt hh; simp only [hh, if_true]; omega
    · simp only [Bool.not_eq_true] at hh; simp only [hh, Bool.false_eq_true, if_false]; omega

end HDLProof.Theory
