/-
`Check.RefMapCompose` -- a checked refinement as a run of the channel spec, and the
composition of two.

From `runRefMapOf_sound` (the cycle facts) and `runSpecFactsOf_sound` (Moore, confluence):

- `execOf`: the implementation's run as a `Theory.Exec` of the channel spec `specOf`. Its
  architectural state is the map's state, its events are the slots that fire.
  `execOf_refines` is `Theory.Exec.Refines`.
- `implStream`: what the implementation shows on channel `x` up to cycle `n`, read from its
  REAL PORTS only: each fired slot's observed words and response words. `implStream_eq`: it is
  the spec run's channel stream.
- `ImplObeys`: the environment answers each fired slot from its channel's own observed history.
- **`refmap_compose`**: two implementations refining the same spec, from the same map state
  at `R0`, each answered by the same environment, show the same stream on every channel, on the
  common prefix. The spec appears in the hypotheses only; the conclusion is about the two
  designs' ports.
- `refmap_progress`: under justice of the declared environment requirements, a ranked channel
  that is allowed eventually gets an event.
Mathlib-free.
-/
import HDLProof.Check.SpecFacts

open Projection
namespace HDLProof.Check
open HDLProof.Sym HDLProof.Kit Std.Tactic.BVDecide Projection.MemProjection Projection.Hw
  Projection.HwMem Compiler

/-! ## Slots as events and observations -/

/-- The spec events of the slots that fire. -/
def evsL (C : Ports) : List SlotS → List (Nat × List Int)
  | [] => []
  | sl :: sls => (if sl.kind.bit C then [(sl.chan, respOf C sl)] else []) ++ evsL C sls

/-- What the slots that fire show: channel, observed words, response words. -/
def obsL (C : Ports) : List SlotS → List (Nat × (List Int × List Int))
  | [] => []
  | sl :: sls => (if sl.kind.bit C then [(sl.chan, (obsOf C sl, respOf C sl))] else []) ++ obsL C sls

section
variable {DS : DesignCert} {sp : RefMapSpec} {C : Ports}

/-- A cycle that satisfies `CycleOK` is a valid spec run that shows what the slots show. -/
theorem cycle_run (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    (hSF : SpecFactsOK DS sp) :
    ∀ (sls : List SlotS) (σ σ' : StateM), SpecNorm sp σ → CycleOK DS sp C σ sls σ' →
      (specOf DS sp).Valid σ (evsL C sls) ∧ σ' = (specOf DS sp).run σ (evsL C sls)
      ∧ (specOf DS sp).annot σ (evsL C sls) = obsL C sls
  | [], σ, σ', _, h => by
      simp only [CycleOK] at h
      subst h
      exact ⟨trivial, rfl, rfl⟩
  | sl :: sls, σ, σ', hσ, h => by
      simp only [CycleOK] at h
      by_cases hk : sl.kind.bit C = true
      · simp only [hk, if_true] at h
        obtain ⟨ch, hch, hal, hob, hrest⟩ := h
        obtain ⟨mal, mob⟩ := hSF.moore σ sl.chan ch (respOf C sl) hσ hch
        obtain ⟨ih1, ih2, ih3⟩ := cycle_run hAS hside hSF sls _ σ'
          (specStep_norm hAS hside hσ ch (respOf C sl)) hrest
        simp only [evsL, obsL, hk, if_true, List.singleton_append]
        refine ⟨⟨?_, ?_⟩, ?_, ?_⟩
        · simp only [specOf, hch]; rw [← mal]; exact hal
        · simp only [specOf, hch]; exact ih1
        · simp only [Theory.Spec.run, specOf, hch]; exact ih2
        · simp only [Theory.Spec.annot, specOf, hch]
          rw [← mob, ← hob]
          simp only [specOf, hch] at ih3
          rw [ih3]
      · have hk' : sl.kind.bit C = false := by simpa using hk
        simp only [hk', Bool.false_eq_true, if_false] at h
        obtain ⟨ih1, ih2, ih3⟩ := cycle_run hAS hside hSF sls σ σ' hσ h
        simp only [evsL, obsL, hk', Bool.false_eq_true, if_false, List.nil_append]
        exact ⟨ih1, ih2, ih3⟩

end

/-! ## The run -/

/-- The implementation's run as a run of the channel spec. -/
noncomputable def execOf (DI : DesignCert) (sp : RefMapSpec) (s0 : StateM)
    (ins : Nat → RuntimeInput) : Theory.Exec StateM Nat (List Int) where
  R0 := sp.R0
  arch n := alphaSt sp (outM DI s0 ins n)
  evs n := evsL (portsAt DI s0 ins n) sp.slots

/-- What the slots that fired show, over cycles `R0 .. n-1`. -/
noncomputable def implUpTo (DI : DesignCert) (sp : RefMapSpec) (s0 : StateM)
    (ins : Nat → RuntimeInput) : Nat → List (Nat × (List Int × List Int))
  | 0 => []
  | n + 1 => implUpTo DI sp s0 ins n
      ++ (if sp.R0 ≤ n then obsL (portsAt DI s0 ins n) sp.slots else [])

/-- Channel `x` as the implementation shows it up to cycle `n`: each event's observed words
and response words, read from its ports. -/
noncomputable def implStream (DI : DesignCert) (sp : RefMapSpec) (s0 : StateM)
    (ins : Nat → RuntimeInput) (x n : Nat) : List (List Int × List Int) :=
  Theory.Spec.chanOf x (implUpTo DI sp s0 ins n)

/-- The environment `E` answers every fired slot from its channel's observed history. -/
def ImplObeys (DI : DesignCert) (sp : RefMapSpec) (s0 : StateM) (ins : Nat → RuntimeInput)
    (E : Theory.Env Nat (List Int) (List Int)) : Prop :=
  ∀ n, Theory.Spec.ObeysA E (implUpTo DI sp s0 ins n)

section
variable {DI DS : DesignCert} {RI RS : Program} {sp : RefMapSpec} {s0 : StateM}
  {pfIB pfB : ByteArray} {pfIS pfS : List (List ByteArray)} {ins : Nat → RuntimeInput}

/-- The facts of every cycle from `R0` on, when every cycle meets the assumptions. -/
theorem cycles_of (hF : RefMapFacts DI DS sp s0 ins) (hg : ∀ n, Good DI sp s0 ins n) :
    ∀ k, ActiveAt sp (outM DI s0 ins (sp.R0 + k)) ∧ SpecNorm sp (alphaSt sp (outM DI s0 ins (sp.R0 + k)))
  | 0 => by have := hF.base (hg _); exact ⟨this.2.1, this.2.2⟩
  | k + 1 => by
      obtain ⟨ha, _⟩ := cycles_of hF hg k
      have := hF.step (sp.R0 + k) (by omega) (hg _) ha
      rw [show sp.R0 + (k + 1) = sp.R0 + k + 1 by omega]
      exact ⟨this.2.1, this.2.2.2.2⟩

theorem execOf_refines (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    (hSF : SpecFactsOK DS sp) (hF : RefMapFacts DI DS sp s0 ins) (hg : ∀ n, Good DI sp s0 ins n) :
    (execOf DI sp s0 ins).Refines (specOf DS sp)
    ∧ ∀ n, sp.R0 ≤ n → (specOf DS sp).annot (alphaSt sp (outM DI s0 ins n))
        (evsL (portsAt DI s0 ins n) sp.slots) = obsL (portsAt DI s0 ins n) sp.slots := by
  have key : ∀ n, sp.R0 ≤ n → _ := fun n hn => by
    have hc := cycles_of hF hg (n - sp.R0)
    rw [show sp.R0 + (n - sp.R0) = n by omega] at hc
    have hs := hF.step n hn (hg _) hc.1
    exact cycle_run hAS hside hSF sp.slots _ _ hc.2 hs.1
  refine ⟨fun n hn => ⟨(key n hn).1, (key n hn).2.1⟩, fun n hn => (key n hn).2.2⟩

/-- The implementation's streams are the spec run's. -/
theorem implUpTo_annot (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    (hSF : SpecFactsOK DS sp) (hF : RefMapFacts DI DS sp s0 ins) (hg : ∀ n, Good DI sp s0 ins n) :
    ∀ n, implUpTo DI sp s0 ins n
      = (specOf DS sp).annot ((execOf DI sp s0 ins).arch (execOf DI sp s0 ins).R0)
          ((execOf DI sp s0 ins).upTo n) := by
  obtain ⟨hX, hann⟩ := execOf_refines hAS hside hSF hF hg
  intro n
  induction n with
  | zero => rfl
  | succ n ih =>
    simp only [implUpTo, Theory.Exec.upTo, Theory.Spec.annot_append]
    rw [ih]
    congr 1
    by_cases hn : sp.R0 ≤ n
    · have hn' : (execOf DI sp s0 ins).R0 ≤ n := hn
      simp only [hn, hn', if_true]
      have hr := (Theory.Exec.upTo_valid hX (n - sp.R0)).2
      rw [show (execOf DI sp s0 ins).R0 + (n - sp.R0) = n by show sp.R0 + (n - sp.R0) = n; omega] at hr
      rw [hr]
      exact (hann n hn).symm
    · have hn' : ¬ (execOf DI sp s0 ins).R0 ≤ n := hn
      simp only [hn, hn', if_false]
      rfl

theorem implStream_eq (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    (hSF : SpecFactsOK DS sp) (hF : RefMapFacts DI DS sp s0 ins) (hg : ∀ n, Good DI sp s0 ins n)
    (x n : Nat) :
    implStream DI sp s0 ins x n = (execOf DI sp s0 ins).stream (specOf DS sp) x n := by
  unfold implStream Theory.Exec.stream
  rw [implUpTo_annot hAS hside hSF hF hg n, ← Theory.Spec.chan_eq_annot]

theorem implObeys_exec (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    (hSF : SpecFactsOK DS sp) (hF : RefMapFacts DI DS sp s0 ins) (hg : ∀ n, Good DI sp s0 ins n)
    {E : Theory.Env Nat (List Int) (List Int)} (hE : ImplObeys DI sp s0 ins E) :
    (execOf DI sp s0 ins).Obeys (specOf DS sp) E := by
  intro n
  rw [Theory.Spec.obeys_iff_annot, ← implUpTo_annot hAS hside hSF hF hg n]
  exact hE n

end

/-! ## The same spec for two implementations -/

/-- A channel's spec side (its rank is the implementation's). -/
def ChanS.spec (ch : ChanS) : ChanS := { ch with rank := none }

/-- Two refinement specs name the same spec: channels, input widths, layout. -/
def SameSpec (a b : RefMapSpec) : Prop :=
  a.chans.map ChanS.spec = b.chans.map ChanS.spec ∧ a.specInW = b.specInW ∧ a.LS = b.LS

theorem specIn_spec (inW : List Nat) (ch : ChanS) (rs : List Int) :
    specIn inW ch.spec rs = specIn inW ch rs := rfl

theorem chans_spec {a b : RefMapSpec} (h : a.chans.map ChanS.spec = b.chans.map ChanS.spec) (c : Nat) :
    (a.chans[c]?).map ChanS.spec = (b.chans[c]?).map ChanS.spec := by
  have := congrArg (·[c]?) h
  simpa [List.getElem?_map] using this

theorem specOuts_same {DS : DesignCert} {a b : RefMapSpec} (h : SameSpec a b) {cha chb : ChanS}
    (hc : cha.spec = chb.spec) (σ : StateM) (rs : List Int) :
    specOuts DS a σ cha rs = specOuts DS b σ chb rs := by
  unfold specOuts specRes
  rw [h.2.1, ← specIn_spec, hc, specIn_spec]

theorem specStep_same {DS : DesignCert} {a b : RefMapSpec} (h : SameSpec a b) {cha chb : ChanS}
    (hc : cha.spec = chb.spec) (σ : StateM) (rs : List Int) :
    specStep DS a σ cha rs = specStep DS b σ chb rs := by
  unfold specStep specRes
  rw [h.2.1, h.2.2, ← specIn_spec, hc, specIn_spec]

theorem specOf_same {DS : DesignCert} {a b : RefMapSpec} (h : SameSpec a b) : specOf DS a = specOf DS b := by
  have hall : ∀ (c : Nat), (∃ (cha chb : ChanS), a.chans[c]? = some cha ∧ b.chans[c]? = some chb ∧ cha.spec = chb.spec)
      ∨ (a.chans[c]? = none ∧ b.chans[c]? = none) := by
    intro c
    have := chans_spec h.1 c
    cases ha : a.chans[c]? with
    | none =>
      cases hb : b.chans[c]? with
      | none => exact Or.inr ⟨rfl, rfl⟩
      | some _ => rw [ha, hb] at this; cases this
    | some cha =>
      cases hb : b.chans[c]? with
      | none => rw [ha, hb] at this; cases this
      | some chb => rw [ha, hb] at this; exact Or.inl ⟨cha, chb, rfl, rfl, Option.some.inj this⟩
  unfold specOf
  congr 1
  · funext σ c
    rcases hall c with ⟨cha, chb, ha, hb, hc⟩ | ⟨ha, hb⟩
    · simp only [ha, hb]
      rw [specOuts_same h hc]
      have : cha.allow = chb.allow := (by have h' := congrArg ChanS.allow hc; simpa [ChanS.spec] using h')
      rw [this]
    · simp only [ha, hb]
  · funext σ c
    rcases hall c with ⟨cha, chb, ha, hb, hc⟩ | ⟨ha, hb⟩
    · simp only [ha, hb]
      rw [specOuts_same h hc]
      have : cha.obsOut = chb.obsOut := (by have h' := congrArg ChanS.obsOut hc; simpa [ChanS.spec] using h')
      rw [this]
    · simp only [ha, hb]
  · funext σ c rs
    rcases hall c with ⟨cha, chb, ha, hb, hc⟩ | ⟨ha, hb⟩
    · simp only [ha, hb]; exact specStep_same h hc σ rs
    · simp only [ha, hb]

theorem specNorm_same {a b : RefMapSpec} (h : SameSpec a b) (σ : StateM) :
    SpecNorm a σ ↔ SpecNorm b σ := by
  unfold SpecNorm SpecSt; rw [h.2.2]

theorem specSide_same {DS : DesignCert} {a b : RefMapSpec} (h : SameSpec a b) :
    specSideOK DS a = specSideOK DS b := by
  unfold specSideOK; rw [h.2.1, h.2.2]

theorem specFacts_same {DS : DesignCert} {a b : RefMapSpec} (h : SameSpec a b)
    (hS : SpecFactsOK DS a) : SpecFactsOK DS b := by
  have hP : SpecNorm a = SpecNorm b := funext fun σ => propext (specNorm_same h σ)
  refine ⟨fun σ c chb rs hσ hchb => ?_, ?_⟩
  · have := chans_spec h.1 c
    cases ha : a.chans[c]? with
    | none => rw [ha, hchb] at this; cases this
    | some cha =>
      rw [ha, hchb] at this
      have hc : cha.spec = chb.spec := Option.some.inj this
      obtain ⟨m1, m2⟩ := hS.moore σ c cha rs ((specNorm_same h σ).mpr hσ) ha
      rw [specOuts_same h hc, specOuts_same h hc] at m1 m2
      have hal : cha.allow = chb.allow := (by have h' := congrArg ChanS.allow hc; simpa [ChanS.spec] using h')
      have hob : cha.obsOut = chb.obsOut := (by have h' := congrArg ChanS.obsOut hc; simpa [ChanS.spec] using h')
      rw [hal] at m1
      rw [hob] at m2
      exact ⟨m1, m2⟩
  · rw [← specOf_same h, ← hP]; exact hS.conf

/-! ## The composition -/

/-- The composition from the checks' facts (`runRefMapOf_sound`, `runSpecFactsOf_sound`): the
residuals the checks ran on do not appear, so each certificate may be specialized on its own. -/
theorem compose_of_facts {DIa DIb DS : DesignCert} {spA spB : RefMapSpec} {s0A s0B : StateM}
    {insA insB : Nat → RuntimeInput} (hAS : admitsWB DS = true) (hsideA : specSideOK DS spA = true)
    (hSFa : SpecFactsOK DS spA) (hsame : SameSpec spA spB)
    (hFA : RefMapFacts DIa DS spA s0A insA) (hFB : RefMapFacts DIb DS spB s0B insB)
    (hgA : ∀ n, Good DIa spA s0A insA n) (hgB : ∀ n, Good DIb spB s0B insB n)
    (hstart : alphaSt spA (outM DIa s0A insA spA.R0) = alphaSt spB (outM DIb s0B insB spB.R0))
    {E : Theory.Env Nat (List Int) (List Int)}
    (hEA : ImplObeys DIa spA s0A insA E) (hEB : ImplObeys DIb spB s0B insB E) :
    ∀ nA nB x, Theory.PrefixAgree (implStream DIa spA s0A insA x nA)
      (implStream DIb spB s0B insB x nB) := by
  have hSFb := specFacts_same hsame hSFa
  have hsideB : specSideOK DS spB = true := by rw [← specSide_same hsame]; exact hsideA
  obtain ⟨hXA, -⟩ := execOf_refines hAS hsideA hSFa hFA hgA
  obtain ⟨hXB, -⟩ := execOf_refines hAS hsideB hSFb hFB hgB
  have hOA := implObeys_exec hAS hsideA hSFa hFA hgA hEA
  have hOB := implObeys_exec hAS hsideB hSFb hFB hgB hEB
  rw [← specOf_same hsame] at hXB hOB
  intro nA nB x
  rw [implStream_eq hAS hsideA hSFa hFA hgA, implStream_eq hAS hsideB hSFb hFB hgB,
    ← specOf_same hsame]
  exact Theory.compose hSFa.conf hXA hXB (cycles_of hFA hgA 0).2 hstart hOA hOB nA nB x

/-- **Two implementations refining one spec show the same streams.** Both checked against the
same spec (`SameSpec`, spec facts checked once), both runs meeting their assumptions, both
starting from the same map state at `R0`, each answered by the same environment from its own
history. Then on every channel the observed and response words agree on the common prefix, at
any two cycle counts. -/
theorem refmap_compose {DIa DIb DS : DesignCert} {RIa RIb RS : Program} {spA spB : RefMapSpec}
    {s0A s0B : StateM} {pfIBa pfBa pfIBb pfBb : ByteArray}
    {pfISa pfSa pfISb pfSb : List (List ByteArray)} {pfF : List ByteArray}
    (hrIa : MemResidual DIa RIa) (hAIa : admitsWB DIa = true)
    (hrIb : MemResidual DIb RIb) (hAIb : admitsWB DIb = true)
    (hrS : MemResidual DS RS) (hAS : admitsWB DS = true)
    (hA : runRefMapOf DIa DS RIa RS spA s0A pfIBa pfISa pfBa pfSa = true)
    (hB : runRefMapOf DIb DS RIb RS spB s0B pfIBb pfISb pfBb pfSb = true)
    (hF : runSpecFactsOf DS RS spA pfF = true) (hsame : SameSpec spA spB)
    {insA insB : Nat → RuntimeInput}
    (hinsA : ∀ n, (insA n).toList.map BV.width = spA.inv.inW)
    (hinsB : ∀ n, (insB n).toList.map BV.width = spB.inv.inW)
    (hgA : ∀ n, Good DIa spA s0A insA n) (hgB : ∀ n, Good DIb spB s0B insB n)
    (hstart : alphaSt spA (outM DIa s0A insA spA.R0) = alphaSt spB (outM DIb s0B insB spB.R0))
    {E : Theory.Env Nat (List Int) (List Int)}
    (hEA : ImplObeys DIa spA s0A insA E) (hEB : ImplObeys DIb spB s0B insB E) :
    ∀ nA nB x, Theory.PrefixAgree (implStream DIa spA s0A insA x nA)
      (implStream DIb spB s0B insB x nB) := by
  exact compose_of_facts hAS (sf_parts hF).1 (runSpecFactsOf_sound hrS hAS hF) hsame
    (runRefMapOf_sound hrIa hAIa hrS hAS hA hinsA) (runRefMapOf_sound hrIb hAIb hrS hAS hB hinsB)
    hgA hgB hstart hEA hEB

/-! ## Progress -/

/-- The implementation's progress data: each ranked channel's rank and selector words, and
the environment requirements' formulas, read from its ports. -/
noncomputable def progOf (DI : DesignCert) (sp : RefMapSpec) (s0 : StateM)
    (ins : Nat → RuntimeInput) : Theory.Progress Nat Nat where
  rank n c := match sp.chans[c]? with
    | some ch => match ch.rank with
      | some (ro, _) => wordAt (outM DI s0 ins n) ro
      | none => 0
    | none => 0
  sel n c := match sp.chans[c]? with
    | some ch => match ch.rank with
      | some (_, so) => selOf (outM DI s0 ins n) so
      | none => none
    | none => none
  reqEn n j := match sp.reqs[j]? with
    | some r => r.en.bit (portsAt DI s0 ins n)
    | none => false
  fires n j := match sp.reqs[j]? with
    | some r => r.fires.bit (portsAt DI s0 ins n)
    | none => false

theorem firesChan_of_free {C : Ports} {c : Nat} :
    ∀ (sls : List SlotS), Theory.Spec.Free c (evsL C sls) →
      (sls.any fun sl => sl.chan == c && sl.kind.bit C) = false
  | [], _ => rfl
  | sl :: sls, h => by
      simp only [List.any_cons]
      have hrest : Theory.Spec.Free c (evsL C sls) := fun e he => h e (by
        simp only [evsL, List.mem_append]; exact Or.inr he)
      rw [firesChan_of_free sls hrest]
      by_cases hk : sl.kind.bit C = true
      · have : sl.chan ≠ c := h (sl.chan, respOf C sl) (by simp [evsL, hk])
        simp [this]
      · simp [hk]

section
variable {DI DS : DesignCert} {RI RS : Program} {sp : RefMapSpec} {s0 : StateM}
  {pfIB pfB : ByteArray} {pfIS pfS : List (List ByteArray)} {ins : Nat → RuntimeInput}

/-- A ranked channel's rank obligations hold on the run. -/
theorem progOf_sound (hF : RefMapFacts DI DS sp s0 ins) (hg : ∀ n, Good DI sp s0 ins n)
    {c : Nat} {ch : ChanS} {ro so : Nat} (hch : sp.chans[c]? = some ch) (hrank : ch.rank = some (ro, so)) :
    (progOf DI sp s0 ins).SoundAt (specOf DS sp) (execOf DI sp s0 ins) c := by
  intro n hn hen hfree
  have hn0 : sp.R0 ≤ n := hn
  have hc := cycles_of hF hg (n - sp.R0)
  rw [show sp.R0 + (n - sp.R0) = n by omega] at hc
  have hr := (hF.step n hn0 (hg _) hc.1).2.2.1
  have hen' : propBit (specOuts DS sp (alphaSt sp (outM DI s0 ins n)) ch []) ch.allow = true := by
    simpa [specOf, execOf, hch] using hen
  have hnf : firesChan sp (portsAt DI s0 ins n) c = false := firesChan_of_free sp.slots hfree
  obtain ⟨h1, h2, h3⟩ := hr c ch ro so hch hrank hen' hnf
  simp only [progOf, hch, hrank, Theory.Progress.helpful]
  refine ⟨h1, fun hh => h2 ?_, fun he => ?_⟩
  · unfold helpfulB
    show (match selOf (outM DI s0 ins n) so with
      | none => true
      | some j => match sp.reqs[j]? with
        | some r => r.fires.bit (portsAt DI s0 ins n)
        | none => false) = true
    cases hs : selOf (outM DI s0 ins n) so with
    | none => rfl
    | some j =>
      rw [hs] at hh
      dsimp only at hh ⊢
      cases hr : sp.reqs[j]? with
      | none => rw [hr] at hh; exact hh
      | some r => rw [hr] at hh; dsimp only at hh ⊢; exact hh
  obtain ⟨hs, hreq⟩ := h3 he
  refine ⟨hs, fun j hj => ?_⟩
  obtain ⟨r, hr', hrb⟩ := hreq j hj
  simp [hr', hrb]

/-- The progress from the checks' facts. -/
theorem progress_of_facts (hAS : admitsWB DS = true) (hside : specSideOK DS sp = true)
    (hSFo : SpecFactsOK DS sp) (hF : RefMapFacts DI DS sp s0 ins) (hg : ∀ n, Good DI sp s0 ins n)
    (hJ : (progOf DI sp s0 ins).Just) {c : Nat} {ch : ChanS} {r : Nat × Nat}
    (hch : sp.chans[c]? = some ch) (hrank : ch.rank = some r) :
    ∀ n, sp.R0 ≤ n → (specOf DS sp).en (alphaSt sp (outM DI s0 ins n)) c = true →
      ∃ m, n ≤ m ∧ ∃ sl ∈ sp.slots, sl.chan = c ∧ sl.kind.bit (portsAt DI s0 ins m) = true := by
  obtain ⟨hX, -⟩ := execOf_refines hAS hside hSFo hF hg
  obtain ⟨ro, so⟩ := r
  intro n hn hen
  obtain ⟨m, hm, e, he, hec⟩ := Theory.channel_progress hSFo.conf hX (cycles_of hF hg 0).2
    (progOf_sound hF hg hch hrank) hJ n hn hen
  refine ⟨m, hm, ?_⟩
  -- an event of channel c in the cycle's list comes from a slot of c that fires
  have : ∀ (sls : List SlotS), e ∈ evsL (portsAt DI s0 ins m) sls →
      ∃ sl ∈ sls, sl.chan = e.1 ∧ sl.kind.bit (portsAt DI s0 ins m) = true := by
    intro sls
    induction sls with
    | nil => intro h; simp [evsL] at h
    | cons sl sls ih =>
      intro h
      simp only [evsL, List.mem_append] at h
      rcases h with h | h
      · by_cases hk : sl.kind.bit (portsAt DI s0 ins m) = true
        · simp only [hk, if_true, List.mem_singleton] at h
          exact ⟨sl, List.mem_cons_self .., by rw [h], hk⟩
        · simp [hk] at h
      · obtain ⟨sl', hsl', h1, h2⟩ := ih h
        exact ⟨sl', List.mem_cons_of_mem _ hsl', h1, h2⟩
  obtain ⟨sl, hsl, h1, h2⟩ := this sp.slots he
  exact ⟨sl, hsl, h1.trans hec, h2⟩

/-- **Progress.** Under justice of the environment requirements, a ranked channel allowed at a
cycle from `R0` on gets, at that cycle or later, a slot of its own that fires. -/
theorem refmap_progress (hrI : MemResidual DI RI) (hAI : admitsWB DI = true)
    (hrS : MemResidual DS RS) (hAS : admitsWB DS = true) {pfF : List ByteArray}
    (hchk : runRefMapOf DI DS RI RS sp s0 pfIB pfIS pfB pfS = true)
    (hSF : runSpecFactsOf DS RS sp pfF = true)
    (hins : ∀ n, (ins n).toList.map BV.width = sp.inv.inW) (hg : ∀ n, Good DI sp s0 ins n)
    (hJ : (progOf DI sp s0 ins).Just) {c : Nat} {ch : ChanS} {r : Nat × Nat}
    (hch : sp.chans[c]? = some ch) (hrank : ch.rank = some r) :
    ∀ n, sp.R0 ≤ n → (specOf DS sp).en (alphaSt sp (outM DI s0 ins n)) c = true →
      ∃ m, n ≤ m ∧ ∃ sl ∈ sp.slots, sl.chan = c ∧ sl.kind.bit (portsAt DI s0 ins m) = true := by
  exact progress_of_facts hAS (sf_parts hSF).1 (runSpecFactsOf_sound hrS hAS hSF)
    (runRefMapOf_sound hrI hAI hrS hAS hchk hins) hg hJ hch hrank

end

end HDLProof.Check
