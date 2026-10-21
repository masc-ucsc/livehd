/-!
# Channels: a deterministic spec over channel events, and Kahn determinism

A specification is a deterministic labelled transducer whose steps are channel events.
In state `s`:
- `en s c` says whether an event on channel `c` is allowed;
- `out s c` is what that event presents (a bus request, a commit record);
- `δ s c r` is the next state when the event received `r` from the environment (an output-only
  channel receives a fixed value).

The spec is **Moore**: `out` depends on the state only. So an event's request is fixed before its
response exists.

The environment answers each channel from that channel's own request history (`Obeys`). Two runs
of the spec with the same environment may interleave the channels differently: that is what two
pipelines with different timing do. `chan_agree` says the order does not matter. Each channel's
stream of (request, response) pairs is the same in both runs on their common prefix, provided
the spec is `Confluent`: events on two different channels, when both are allowed, commute and
neither disables or changes the other. This is the diamond lemma of trace theory, used for Kahn
determinism.

Pure Lean (no Mathlib, no hardware). The hardware checkers prove `Confluent` for a spec
certificate one step at a time (`Check/SpecFacts`).
-/

namespace HDLProof.Theory

/-- Agreement on the common prefix: wherever both lists have an element, it is the same. -/
def PrefixAgree {α : Type} (xs ys : List α) : Prop :=
  ∀ (i : Nat) (a b : α), xs[i]? = some a → ys[i]? = some b → a = b

theorem PrefixAgree.nil_left {α : Type} (ys : List α) : PrefixAgree [] ys := by
  intro i a b h; simp at h

theorem PrefixAgree.nil_right {α : Type} (xs : List α) : PrefixAgree xs [] := by
  intro i a b _ h; simp at h

theorem PrefixAgree.symm {α : Type} {xs ys : List α} (h : PrefixAgree xs ys) : PrefixAgree ys xs :=
  fun i a b ha hb => (h i b a hb ha).symm

theorem PrefixAgree.append {α : Type} (p : List α) {xs ys : List α} (h : PrefixAgree xs ys) :
    PrefixAgree (p ++ xs) (p ++ ys) := by
  intro i a b ha hb
  by_cases hi : i < p.length
  · rw [List.getElem?_append_left hi] at ha hb
    rw [ha] at hb; exact Option.some.inj hb
  · have hle : p.length ≤ i := Nat.le_of_not_lt hi
    rw [List.getElem?_append_right hle] at ha hb
    exact h _ a b ha hb

theorem PrefixAgree.of_prefix {α : Type} {xs ys : List α} (h : xs <+: ys) : PrefixAgree xs ys := by
  intro i a b ha hb
  obtain ⟨t, rfl⟩ := h
  have hi : i < xs.length := by
    rcases Nat.lt_or_ge i xs.length with h | h
    · exact h
    · rw [List.getElem?_eq_none h] at ha; cases ha
  rw [List.getElem?_append_left hi, ha] at hb
  exact Option.some.inj hb

/-- A channel spec. `S` states, `C` channels, `O` what an event presents, `R` what it receives. -/
structure Spec (S C O R : Type) where
  en  : S → C → Bool
  out : S → C → O
  δ   : S → C → R → S

/-- One event: its channel and the response it received. -/
abbrev Ev (C R : Type) := C × R

/-- The environment: the response on channel `c` to that channel's request history (oldest
first, ending with the current request). -/
abbrev Env (C O R : Type) := C → List O → R

variable {S C O R : Type}

/-- The environment after it answered request `o` on channel `c`. -/
def Env.push [DecidableEq C] (E : Env C O R) (c : C) (o : O) : Env C O R :=
  fun x h => if x = c then E x (o :: h) else E x h

theorem Env.push_comm [DecidableEq C] (E : Env C O R) {c d : C} (hcd : c ≠ d) (o p : O) :
    (E.push c o).push d p = (E.push d p).push c o := by
  funext x h
  unfold Env.push
  by_cases hd : x = d
  · subst hd
    have : x ≠ c := fun e => hcd e.symm
    simp [this]
  · by_cases hc : x = c
    · subst hc; simp [hd]
    · simp [hd, hc]

theorem Env.push_ne [DecidableEq C] (E : Env C O R) {c x : C} (h : x ≠ c) (o : O) (hs : List O) :
    E.push c o x hs = E x hs := by
  simp [Env.push, h]

namespace Spec

variable (sp : Spec S C O R)

/-- Each event is allowed in the state it starts from. -/
def Valid : S → List (Ev C R) → Prop
  | _, [] => True
  | s, (c, r) :: u => sp.en s c = true ∧ Valid (sp.δ s c r) u

/-- The state after a list of events. -/
def run : S → List (Ev C R) → S
  | s, [] => s
  | s, (c, r) :: u => run (sp.δ s c r) u

/-- The stream seen on channel `x`: the (request, response) of each `x` event, in order. -/
def chan [DecidableEq C] (x : C) : S → List (Ev C R) → List (O × R)
  | _, [] => []
  | s, (c, r) :: u => (if c = x then [(sp.out s c, r)] else []) ++ chan x (sp.δ s c r) u

/-- Every response is the environment's answer to its own channel's history. -/
def Obeys [DecidableEq C] : Env C O R → S → List (Ev C R) → Prop
  | _, _, [] => True
  | E, s, (c, r) :: u => r = E c [sp.out s c] ∧ Obeys (E.push c (sp.out s c)) (sp.δ s c r) u

/-- Two allowed events on different channels commute, and neither disables or changes the
other. `P` is a state invariant the spec keeps (e.g. "reachable", or a proved spec invariant). -/
structure Confluent (P : S → Prop) : Prop where
  pres    : ∀ s c r, P s → sp.en s c = true → P (sp.δ s c r)
  persist : ∀ s c d r, P s → c ≠ d → sp.en s c = true → sp.en s d = true →
              sp.en (sp.δ s c r) d = true
  outC    : ∀ s c d r, P s → c ≠ d → sp.en s c = true → sp.en s d = true →
              sp.out (sp.δ s c r) d = sp.out s d
  comm    : ∀ s c d r q, P s → c ≠ d → sp.en s c = true → sp.en s d = true →
              sp.δ (sp.δ s c r) d q = sp.δ (sp.δ s d q) c r

variable {sp}

theorem valid_append {s : S} {u v : List (Ev C R)} :
    sp.Valid s (u ++ v) ↔ sp.Valid s u ∧ sp.Valid (sp.run s u) v := by
  induction u generalizing s with
  | nil => simp [Valid, run]
  | cons e u ih =>
    obtain ⟨c, r⟩ := e
    simp only [List.cons_append, Valid, run, ih, and_assoc]

theorem run_append (s : S) (u v : List (Ev C R)) : sp.run s (u ++ v) = sp.run (sp.run s u) v := by
  induction u generalizing s with
  | nil => rfl
  | cons e u ih => obtain ⟨c, r⟩ := e; exact ih _

/-- No event of the list is on channel `c`. -/
def Free (c : C) (w : List (Ev C R)) : Prop := ∀ e ∈ w, e.1 ≠ c

theorem chan_free [DecidableEq C] {c : C} :
    ∀ (w : List (Ev C R)) (s : S), Free c w → sp.chan c s w = []
  | [], _, _ => rfl
  | (d, p) :: w, s, h => by
    have hd : d ≠ c := h (d, p) (List.mem_cons_self ..)
    simp only [chan, hd, if_false, List.nil_append]
    exact chan_free w _ (fun e he => h e (List.mem_cons_of_mem _ he))

/-- An allowed event on `c` can be put in front of a run with no `c` event: the run stays
valid and obeyed, every other channel's stream is unchanged, and the end state is the same. -/
theorem insert_front [DecidableEq C] {P : S → Prop} (hc : sp.Confluent P) {c : C} :
    ∀ (v : List (Ev C R)) (s : S) (E : Env C O R) (r : R), P s → sp.en s c = true → Free c v →
      sp.Valid s v → sp.Obeys E s v →
      sp.Valid (sp.δ s c r) v ∧ sp.Obeys (E.push c (sp.out s c)) (sp.δ s c r) v ∧
      (∀ x, x ≠ c → sp.chan x (sp.δ s c r) v = sp.chan x s v)
  | [], _, _, _, _, _, _, _, _ => ⟨trivial, trivial, fun _ _ => rfl⟩
  | (d, p) :: v, s, E, r, hP, hen, hfree, hval, hob => by
    have hdc : d ≠ c := hfree (d, p) (List.mem_cons_self ..)
    have hcd : c ≠ d := fun e => hdc e.symm
    obtain ⟨hend, hval'⟩ := hval
    obtain ⟨hp, hob'⟩ := hob
    have hP' : P (sp.δ s d p) := hc.pres s d p hP hend
    have hen' : sp.en (sp.δ s d p) c = true := hc.persist s d c p hP hdc hend hen
    have hfree' : Free c v := fun e he => hfree e (List.mem_cons_of_mem _ he)
    obtain ⟨ihv, iho, ihc⟩ := insert_front hc v (sp.δ s d p) (E.push d (sp.out s d)) r hP' hen' hfree' hval' hob'
    have hcomm : sp.δ (sp.δ s c r) d p = sp.δ (sp.δ s d p) c r := hc.comm s c d r p hP hcd hen hend
    have hout_d : sp.out (sp.δ s c r) d = sp.out s d := hc.outC s c d r hP hcd hen hend
    have hout_c : sp.out (sp.δ s d p) c = sp.out s c := hc.outC s d c p hP hdc hend hen
    refine ⟨⟨hc.persist s c d r hP hcd hen hend, ?_⟩, ⟨?_, ?_⟩, ?_⟩
    · rw [hcomm]; exact ihv
    · rw [hout_d, Env.push_ne E hdc]; exact hp
    · rw [hout_d, hcomm, Env.push_comm E hcd, ← hout_c]; exact iho
    · intro x hx
      simp only [chan]
      rw [hout_d, hcomm, ihc x hx]

/-- The first `c` event of a run can be moved to the front when `c` is allowed at the start:
the run stays valid and obeyed, keeps every channel's stream and its end state, and the moved
event presents the same request it presented at its old position. -/
theorem move_front [DecidableEq C] {P : S → Prop} (hc : sp.Confluent P) {c : C} (q : R)
    (z : List (Ev C R)) :
    ∀ (w : List (Ev C R)) (s : S) (E : Env C O R), P s → sp.en s c = true → Free c w →
      sp.Valid s (w ++ (c, q) :: z) → sp.Obeys E s (w ++ (c, q) :: z) →
      sp.out (sp.run s w) c = sp.out s c ∧
      sp.Valid s ((c, q) :: (w ++ z)) ∧ sp.Obeys E s ((c, q) :: (w ++ z)) ∧
      (∀ x, sp.chan x s ((c, q) :: (w ++ z)) = sp.chan x s (w ++ (c, q) :: z))
  | [], s, E, _, _, _, hval, hob => ⟨rfl, hval, hob, fun _ => rfl⟩
  | (d, p) :: w, s, E, hP, hen, hfree, hval, hob => by
    have hdc : d ≠ c := hfree (d, p) (List.mem_cons_self ..)
    have hcd : c ≠ d := fun e => hdc e.symm
    obtain ⟨hend, hval'⟩ := hval
    obtain ⟨hp, hob'⟩ := hob
    have hP' : P (sp.δ s d p) := hc.pres s d p hP hend
    have hen' : sp.en (sp.δ s d p) c = true := hc.persist s d c p hP hdc hend hen
    have hfree' : Free c w := fun e he => hfree e (List.mem_cons_of_mem _ he)
    obtain ⟨ihout, ⟨ihen, ihv⟩, ⟨ihq, iho⟩, ihc⟩ :=
      move_front hc q z w (sp.δ s d p) (E.push d (sp.out s d)) hP' hen' hfree' hval' hob'
    have hcomm : sp.δ (sp.δ s c q) d p = sp.δ (sp.δ s d p) c q := hc.comm s c d q p hP hcd hen hend
    have hout_d : sp.out (sp.δ s c q) d = sp.out s d := hc.outC s c d q hP hcd hen hend
    have hout_c : sp.out (sp.δ s d p) c = sp.out s c := hc.outC s d c p hP hdc hend hen
    refine ⟨?_, ⟨hen, hc.persist s c d q hP hcd hen hend, ?_⟩, ⟨?_, ?_, ?_⟩, ?_⟩
    · show sp.out (sp.run (sp.δ s d p) w) c = sp.out s c
      rw [ihout, hout_c]
    · rw [hcomm]; exact ihv
    · rw [ihq, hout_c, Env.push_ne E hcd]
    · rw [hout_d, Env.push_ne E hdc]; exact hp
    · rw [hout_d, hcomm, Env.push_comm E hcd, ← hout_c]; exact iho
    · intro x
      have := ihc x
      simp only [List.cons_append, chan] at this ⊢
      rw [hout_d, hcomm, ← hout_c]
      by_cases hx : c = x
      · subst hx
        simp only [hdc, if_false, List.nil_append, if_true]
        simp only [if_true] at this
        rw [← this]
      · by_cases hdx : d = x
        · subst hdx
          simp only [hx, if_false, List.nil_append, if_true]
          simp only [hx, if_false, List.nil_append] at this
          rw [this]
        · simp only [hx, hdx, if_false, List.nil_append]
          simp only [hx, if_false, List.nil_append] at this
          exact this

/-- Split a run at its first `c` event, or find that it has none. -/
theorem split_first [DecidableEq C] (c : C) :
    ∀ (v : List (Ev C R)), Free c v ∨ ∃ w q z, Free c w ∧ v = w ++ (c, q) :: z
  | [] => Or.inl (fun _ h => by cases h)
  | (d, p) :: v => by
    by_cases hd : d = c
    · subst hd; exact Or.inr ⟨[], p, v, (fun _ h => by cases h), rfl⟩
    · rcases split_first c v with h | ⟨w, q, z, hw, rfl⟩
      · exact Or.inl (fun e he => by
          rcases List.mem_cons.mp he with rfl | he
          · exact hd
          · exact h e he)
      · refine Or.inr ⟨(d, p) :: w, q, z, ?_, rfl⟩
        intro e he
        rcases List.mem_cons.mp he with rfl | he
        · exact hd
        · exact hw e he

/-- **Kahn determinism.** Two runs of a confluent spec from the same state, each obeying the same
environment, present the same stream on every channel, on their common prefix. -/
theorem chan_agree [DecidableEq C] {P : S → Prop} (hc : sp.Confluent P) :
    ∀ (n : Nat) (s : S) (E : Env C O R) (u v : List (Ev C R)), u.length + v.length ≤ n → P s →
      sp.Valid s u → sp.Valid s v → sp.Obeys E s u → sp.Obeys E s v →
      ∀ x, PrefixAgree (sp.chan x s u) (sp.chan x s v)
  | _, _, _, [], _, _, _, _, _, _, _, x => PrefixAgree.nil_left _
  | _, _, _, _ :: _, [], _, _, _, _, _, _, x => PrefixAgree.nil_right _
  | 0, _, _, _ :: _, _ :: _, hn, _, _, _, _, _, _ => by simp only [List.length_cons] at hn; omega
  | n + 1, s, E, (c, r) :: u, (d, q) :: v, hn, hP, hu, hv, hou, hov, x => by
    obtain ⟨henc, hu'⟩ := hu
    obtain ⟨hr, hou'⟩ := hou
    have hP1 : P (sp.δ s c r) := hc.pres s c r hP henc
    -- the same first event on both sides: recurse on the tails
    have same_head : ∀ (v' : List (Ev C R)), u.length + v'.length ≤ n →
        sp.Valid (sp.δ s c r) v' → sp.Obeys (E.push c (sp.out s c)) (sp.δ s c r) v' →
        PrefixAgree (sp.chan x s ((c, r) :: u)) (sp.chan x s ((c, r) :: v')) := by
      intro v' hlen hv' hov'
      simp only [chan]
      exact PrefixAgree.append _
        (chan_agree hc n _ _ u v' hlen hP1 hu' hv' hou' hov' x)
    by_cases hcd : d = c
    · subst hcd
      obtain ⟨hend, hv'⟩ := hv
      obtain ⟨hq, hov'⟩ := hov
      have : q = r := by rw [hq, hr]
      subst this
      exact same_head v (by simp at hn; omega) hv' hov'
    · rcases split_first c ((d, q) :: v) with hfree | ⟨w, q', z, hw, hsplit⟩
      · -- v has no `c` event: `c`'s stream on that side is empty; for other channels,
        -- put `u`'s first event in front of `v` too
        by_cases hx : x = c
        · subst hx; rw [chan_free _ s hfree]; exact PrefixAgree.nil_right _
        · obtain ⟨hv1, hov1, hch⟩ := insert_front hc ((d, q) :: v) s E r hP henc hfree hv hov
          have key := chan_agree hc n _ _ u ((d, q) :: v) (by simp at hn ⊢; omega) hP1 hu' hv1
            hou' hov1 x
          have hl : sp.chan x s ((c, r) :: u) = sp.chan x (sp.δ s c r) u := by
            simp only [chan, show c ≠ x from fun e => hx e.symm, if_false, List.nil_append]
          rw [hl, ← hch x hx]
          exact key
      · -- move `v`'s first `c` event to the front; it received the same response as `u`'s
        rw [hsplit] at hv hov ⊢
        obtain ⟨hout, hmv, hmo, hmc⟩ := move_front hc q' z w s E hP henc hw hv hov
        rw [← hmc x]
        obtain ⟨_, hmv'⟩ := hmv
        obtain ⟨hq', hmo'⟩ := hmo
        have : q' = r := by rw [hq', hr]
        subst this
        have hlen := congrArg List.length hsplit
        simp only [List.length_cons, List.length_append] at hlen hn
        exact same_head (w ++ z) (by simp only [List.length_append]; omega) hmv' hmo'

end Spec

namespace Spec

variable {sp : Spec S C O R}

/-- A run's events, each with the request it presented. -/
def annot (sp : Spec S C O R) : S → List (Ev C R) → List (C × (O × R))
  | _, [] => []
  | s, (c, r) :: u => (c, (sp.out s c, r)) :: annot sp (sp.δ s c r) u

/-- The stream of channel `x` in an annotated event list. -/
def chanOf [DecidableEq C] (x : C) (l : List (C × (O × R))) : List (O × R) :=
  l.filterMap fun e => if e.1 = x then some e.2 else none

theorem chan_eq_annot [DecidableEq C] (x : C) :
    ∀ (s : S) (u : List (Ev C R)), sp.chan x s u = chanOf x (sp.annot s u)
  | _, [] => rfl
  | s, (c, r) :: u => by
      simp only [chan, annot, chanOf, List.filterMap_cons]
      rw [chan_eq_annot x _ u]
      by_cases h : c = x <;> simp [h, chanOf]

theorem annot_append (s : S) (u v : List (Ev C R)) :
    sp.annot s (u ++ v) = sp.annot s u ++ sp.annot (sp.run s u) v := by
  induction u generalizing s with
  | nil => rfl
  | cons e u ih => obtain ⟨c, r⟩ := e; simp [annot, run, ih]

/-- Every response is the environment's answer to its channel's history, over annotated events. -/
def ObeysA [DecidableEq C] : Env C O R → List (C × (O × R)) → Prop
  | _, [] => True
  | E, (c, (o, r)) :: l => r = E c [o] ∧ ObeysA (E.push c o) l

theorem obeys_iff_annot [DecidableEq C] :
    ∀ (E : Env C O R) (s : S) (u : List (Ev C R)), sp.Obeys E s u ↔ ObeysA E (sp.annot s u)
  | _, _, [] => Iff.rfl
  | E, s, (c, r) :: u => by
      simp only [Obeys, annot, ObeysA]
      rw [obeys_iff_annot _ _ u]

end Spec

end HDLProof.Theory
