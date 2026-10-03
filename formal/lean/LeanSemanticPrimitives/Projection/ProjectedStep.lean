/-
# From a checked residual to a `StepCorrect` simulator

Phase 5, second increment.  Increment 1 produced a bundle and a bound.  What is
missing is the bridge from `evalFuel` (what runs) to `Eval` (what
`projectDesign_correct` talks about), in BOTH directions, and the runtime
checking that `StepCorrect` forces.

## Why a runtime check is unavoidable

`StepCorrect step D` quantifies over EVERY `e, i, s`.  `projectDesign_correct`
does not: it needs `Compiler.RuntimeWF D i s` and `RuntimeSized D e i s`,
because `I_hw`'s slot reads are unchecked and `interpretDesign` is total
exactly where they are not.

So `stepOf` must DECIDE those conditions and REFUSE before evaluating anything.
Refusing is not incorrect -- `StepCorrect` is stated on success only -- and it
is what makes the contract's universal quantification honest.

What is NOT rechecked per cycle: `SupportedByProjection`, which is static and
comes from `SimWF` once; and the residual itself, which `mkSim` already
checked.  And `Step`'s design argument is ignored outright.

## The two halves

  runProjected_correct   on `.ok r`, `r` IS the reference answer
  runProjected_success   …and for a valid run it DOES return `.ok`, never
                         `typeError` and never `undecodableResult`

The second needs the converse of increment 1's bound lemma: not merely "never
out of fuel", but "reaches the value the denotation says it reaches".  That is
`fragB_evalFuel` below, by recursion on the `Eval` DERIVATION -- which is what
makes the `caseT` case work, since `findAlt`'s result is not a syntactic
subterm but the sub-derivation is.
-/

import LeanSemanticPrimitives.Projection.ResidualFragment

namespace Projection
namespace Hw

open Compiler

/-! ## The runtime check -/

/-- Exactly `Compiler.RuntimeWF` and `RuntimeSized`, decided.

`RuntimeSized.flopsSized` is an inequality and needs no separate test: the
EXACT state shape `RuntimeWF` demands implies it. -/
def runtimeOK (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : Bool :=
  decide (s.flops.size = D.flops.size) &&
  decide (s.mems.size = D.memories.size) &&
  decide (D.clocks.size ≤ e.size) &&
  D.sources.toList.all (fun sd => decide (SourceInputBound sd i.size)) &&
  D.sources.toList.all (fun sd => decide (SourceFlopBound sd s.flops.size))

theorem runtimeOK_sound {D : DesignCert} {e : ClockEdges} {i : RuntimeInput}
    {s : RuntimeState} (h : runtimeOK D e i s = true) :
    Compiler.RuntimeWF D i s ∧ RuntimeSized D e i s := by
  simp only [runtimeOK, Bool.and_eq_true, decide_eq_true_eq, List.all_eq_true] at h
  obtain ⟨⟨⟨⟨hfl, hme⟩, hed⟩, hin⟩, hst⟩ := h
  refine ⟨⟨hfl, hme⟩, ⟨hed, ?_, ?_, by omega⟩⟩
  · intro sd hsd; exact hin sd hsd
  · intro sd hsd; exact hst sd hsd

/-! ## The success half

`evalFuel` reaches the denotation's value at the checked bound.  Proved by
recursion on the `Eval` derivation rather than on the term: the `caseT` case
continues into `a.body`, which `findAlt` produced and which is therefore not a
syntactic subterm of `.caseT s alts`, while the sub-derivation IS a subterm of
the derivation. -/

mutual

theorem fragB_evalFuel {P : Program} :
    ∀ (ρ : Env) (t : Term) (v : Val) (d n : Nat),
      Eval P ρ t v → fragB d t = true → ρ.length = d → height t ≤ n →
      evalFuel n P ρ t = .value v
  | _, _, _, _, n, .lit, _, _, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m => simp [evalFuel]
  | _, _, _, _, n, .var hv, _, _, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m => simp [evalFuel, hv]
  | ρ, _, _, d, n, .letIn (e := e) (b := b) (v₁ := v₁) he hb, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          rw [fragB_evalFuel ρ e v₁ d m he hf.1 hρ (by omega)]
          exact fragB_evalFuel (v₁ :: ρ) b _ (d + 1) m hb hf.2 (by simp [hρ]) (by omega)
  | ρ, _, _, d, n, .iteT (c := c) (a := a) hc ha, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          rw [fragB_evalFuel ρ c _ d m hc hf.1.1 hρ (by omega)]
          exact fragB_evalFuel ρ a _ d m ha hf.1.2 hρ (by omega)
  | ρ, _, _, d, n, .iteF (c := c) (b := b) hc hb, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          rw [fragB_evalFuel ρ c _ d m hc hf.1.1 hρ (by omega)]
          exact fragB_evalFuel ρ b _ d m hb hf.2 hρ (by omega)
  | ρ, _, _, d, n, .prim (ts := ts) (vs := vs) hts hp, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          rw [fragListB_evalFuel ρ ts vs d m hts hf.2 hρ (by omega)]
          simp [hp]
  | ρ, _, _, d, n, .ctorT (ts := ts) (vs := vs) hts, hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB] at hf
          simp only [height] at hn
          simp only [evalFuel]
          rw [fragListB_evalFuel ρ ts vs d m hts hf hρ (by omega)]
  | ρ, _, _, d, n, .caseT (s := s) (alts := alts) (a := a) (vs := vs) hs hfa har hb,
      hf, hρ, hn => by
      cases n with
      | zero   => exact absurd hn (by simp [height])
      | succ m =>
          simp only [fragB, Bool.and_eq_true] at hf
          simp only [height] at hn
          simp only [evalFuel]
          rw [fragB_evalFuel ρ s _ d m hs hf.1 hρ (by omega)]
          simp only [hfa, if_pos har]
          refine fragB_evalFuel (vs ++ ρ) a.body _ (d + a.arity) m hb
            (fragAltsB_find alts _ a hf.2 hfa) ?_ ?_
          · simp only [List.length_append, hρ, har]; omega
          · have := heightAlts_find alts _ a hfa
            omega
  | _, _, _, _, _, .call .., hf, _, _ => by simp [fragB] at hf

theorem fragListB_evalFuel {P : Program} :
    ∀ (ρ : Env) (ts : List Term) (vs : List Val) (d n : Nat),
      EvalList P ρ ts vs → fragListB d ts = true → ρ.length = d →
      heightList ts ≤ n → evalFuelList n P ρ ts = .inl vs
  | _, _, _, _, _, .nil, _, _, _ => by simp [evalFuelList]
  | ρ, _, _, d, n, .cons (t := t) (v := v) (ts := ts) (vs := vs) ht hts, hf, hρ, hn => by
      simp only [fragListB, Bool.and_eq_true] at hf
      simp only [heightList] at hn
      simp only [evalFuelList]
      rw [fragB_evalFuel ρ t v d n ht hf.1 hρ (by omega)]
      rw [fragListB_evalFuel ρ ts vs d n hts hf.2 hρ (by omega)]

end

/-- …and at the entry-call form, which is what `runProjected` evaluates. -/
theorem checkResidual_complete {R : Program} {b : Nat} (h : checkResidual R = some b)
    {a₀ a₁ a₂ v : Val}
    (hev : Eval R [] (.call R.entry [.lit a₀, .lit a₁, .lit a₂]) v) :
    evalFuel b R [] (.call R.entry [.lit a₀, .lit a₁, .lit a₂]) = .value v := by
  obtain ⟨fd, hfd, ha, hfr, rfl⟩ := checkResidual_entry h
  obtain ⟨k, hk⟩ : ∃ k, height fd.body = k + 1 :=
    ⟨height fd.body - 1, by have := height_pos fd.body; omega⟩
  cases hev with
  | call hts hfn har hb =>
      rename_i vs fdx
      rw [hfd] at hfn
      cases hfn
      have hvs : vs = [a₀, a₁, a₂] := EvalList_lits_inj [a₀, a₁, a₂] vs hts
      subst hvs
      rw [hk]
      simp only [evalFuel, evalFuelList, hfd, ha]
      have h3 : (3 : Nat) = [a₀, a₁, a₂].length := rfl
      simp only [if_pos h3]
      exact fragB_evalFuel [a₀, a₁, a₂] fd.body _ 3 (k + 1) hb hfr rfl (by omega)

/-! ## `runProjected`, both halves -/

/-- **Partial correctness.**  Whatever `runProjected` returns is the reference
answer.  This half needs nothing from the success half. -/
theorem runProjected_correct {sim : ProjectedSimulator} (hsim : SimSound sim)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState} {r : RuntimeResult}
    (hrt : runtimeOK sim.design e i s = true)
    (hok : runProjected sim e i s = .ok r) :
    r = interpretDesign sim.design e i s := by
  obtain ⟨hwf, hrs⟩ := runtimeOK_sound hrt
  unfold runProjected at hok
  cases hr : evalFuel sim.bound sim.prog []
      (.call sim.prog.entry
        [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) with
  | outOfFuel     => simp [hr] at hok
  | typeError msg => simp [hr] at hok
  | value v       =>
      simp only [hr] at hok
      cases hd : decResult v with
      | none    => simp [hd] at hok
      | some r' =>
          simp only [hd] at hok
          simp only [Except.ok.injEq] at hok
          subst hok
          have hev := evalFuel_sound _ _ _ _ _ hr
          have hrel := (hsim.agree hwf hrs v).mp hev
          simp only [ResultRel, hd] at hrel
          exact Option.some.inj hrel

/-- **Success.**  For a valid run it DOES answer, and the answer is the
reference one -- so neither `typeError` nor `undecodableResult` can fire, and
by increment 1 neither can `boundExceeded`. -/
theorem runProjected_success {sim : ProjectedSimulator} (hsim : SimSound sim)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState}
    (hrt : runtimeOK sim.design e i s = true) :
    runProjected sim e i s = .ok (interpretDesign sim.design e i s) := by
  obtain ⟨hwf, hrs⟩ := runtimeOK_sound hrt
  have hmf : MemFree (interpretDesign sim.design e i s).nextState :=
    interpretDesign_memFree hsim.sup.memFree e i s
  have hrel : ResultRel (encResult (interpretDesign sim.design e i s))
      (interpretDesign sim.design e i s) := ResultRel_encResult hmf
  have hev := (hsim.agree hwf hrs _).mpr hrel
  have hfuel := checkResidual_complete hsim.chk hev
  simp only [ResultRel] at hrel
  unfold runProjected
  simp only [hfuel, hrel]

/-! ## The contract -/

/-- One cycle of an already-specialized simulator.

The `Step` design argument is IGNORED -- the simulator was specialized for
`sim.design` and comparing a whole `DesignCert` every cycle would be O(|D|) of
pure overhead.  `StepCorrect` below is therefore stated for `sim.design`, which
is the only design this simulator claims anything about.

What is NOT ignored is the runtime shape: it is decided here, and a refusal
happens BEFORE the residual is evaluated. -/
def stepOf (sim : ProjectedSimulator) : Step SimError :=
  fun _ e i s =>
    if runtimeOK sim.design e i s then runProjected sim e i s
    else .error .runtimeShape

/-- **The first thing on this branch to satisfy `StepCorrect`.** -/
theorem stepOf_correct {sim : ProjectedSimulator} (hsim : SimSound sim) :
    StepCorrect (stepOf sim) sim.design := by
  intro e i s r hstep
  unfold stepOf at hstep
  split at hstep
  · rename_i hrt
    exact runProjected_correct hsim hrt hstep
  · simp at hstep

/-- …and it answers whenever the runtime shape is right. -/
theorem stepOf_succeeds {sim : ProjectedSimulator} (hsim : SimSound sim)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState}
    (hrt : runtimeOK sim.design e i s = true) :
    stepOf sim sim.design e i s = .ok (interpretDesign sim.design e i s) := by
  unfold stepOf
  rw [if_pos hrt]
  exact runProjected_success hsim hrt

/-! ## Traces

`stepTrace` and `stepTrace_correct` already exist in `SimulatorContract.lean`
and are generic over `Step`.  Nothing here defines a second trace runner; this
is one instantiation. -/

theorem stepTrace_projected {sim : ProjectedSimulator} (hsim : SimSound sim)
    (s : RuntimeState) (is : List Stim) (rs : List RuntimeResult)
    (h : stepTrace (stepOf sim) sim.design s is = .ok rs) :
    rs = refTrace sim.design s is :=
  stepTrace_correct (stepOf_correct hsim) s is rs h

/-! ## Fixture acceptance, at theorem level

Conditional on `SimSound sim`, which is exactly the residual being correct
for its design -- weaker than `SimWF`, and satisfiable by any specializer /
interpreter pair with a proved composition theorem, not only `projectDesign`.

That hypothesis cannot currently be discharged for a CONCRETE fixture: it
contains `projectDesign D = .ok R`, and the previous commit measured that the
specializer does not reduce in the kernel at any nontrivial fuel.  So these are
the strongest statements available, and the `#guard`s in `ResidualFragment` --
which run the compiled evaluator -- remain what certifies the concrete case.
The gap is the same one, in the same place; it has not grown. -/

namespace FixtureStep
open Projection.Acceptance

theorem tiny_runtimeOK : runtimeOK tinyD (allEdges tinyD) tinyIn tinySt = true := by decide

theorem seq_runtimeOK (d en rst q : Int) :
    runtimeOK seqD (allEdges seqD) (seqIn d en rst) (seqSt q) = true := by
  -- an array's SIZE does not depend on its elements, so the check holds at
  -- every stimulus and every stored value
  have h1 : (seqSt q).flops.size = 1 := rfl
  have h2 : (seqSt q).mems.size = 0 := rfl
  have h3 : (seqIn d en rst).size = 3 := rfl
  simp only [runtimeOK, h1, h2, h3]
  decide

/-- One cycle, as a theorem rather than a `#guard`. -/
theorem tiny_one_cycle {sim : ProjectedSimulator} (hsim : SimSound sim)
    (hd : sim.design = tinyD) :
    stepOf sim tinyD (allEdges tinyD) tinyIn tinySt
      = .ok (interpretDesign tinyD (allEdges tinyD) tinyIn tinySt) := by
  have h := stepOf_succeeds hsim
    (show runtimeOK sim.design (allEdges sim.design) tinyIn tinySt = true by
      rw [hd]; exact tiny_runtimeOK)
  rw [hd] at h
  exact h

theorem seq_one_cycle {sim : ProjectedSimulator} (hsim : SimSound sim)
    (hd : sim.design = seqD) (d en rst q : Int) :
    stepOf sim seqD (allEdges seqD) (seqIn d en rst) (seqSt q)
      = .ok (interpretDesign seqD (allEdges seqD) (seqIn d en rst) (seqSt q)) := by
  have h := stepOf_succeeds hsim
    (show runtimeOK sim.design (allEdges sim.design) (seqIn d en rst) (seqSt q) = true by
      rw [hd]; exact seq_runtimeOK d en rst q)
  rw [hd] at h
  exact h

/-- Two cycles, through the SHARED `stepTrace`/`refTrace`, with the second
cycle starting from the first one's `nextState` -- which is the part a
one-cycle theorem does not say. -/
theorem tiny_two_cycles {sim : ProjectedSimulator} (hsim : SimSound sim)
    (hd : sim.design = tinyD) (rs : List RuntimeResult)
    (h : stepTrace (stepOf sim) tinyD tinySt
            [(allEdges tinyD, tinyIn), (allEdges tinyD, tinyIn)] = .ok rs) :
    rs = refTrace tinyD tinySt [(allEdges tinyD, tinyIn), (allEdges tinyD, tinyIn)] := by
  have h' := stepTrace_projected hsim tinySt
    [(allEdges tinyD, tinyIn), (allEdges tinyD, tinyIn)] rs (by rw [hd]; exact h)
  rw [hd] at h'
  exact h'

theorem seq_two_cycles {sim : ProjectedSimulator} (hsim : SimSound sim)
    (hd : sim.design = seqD) (q : Int) (rs : List RuntimeResult)
    (h : stepTrace (stepOf sim) seqD (seqSt q)
            [(allEdges seqD, seqIn 5 1 0), (allEdges seqD, seqIn 3 0 0)] = .ok rs) :
    rs = refTrace seqD (seqSt q)
            [(allEdges seqD, seqIn 5 1 0), (allEdges seqD, seqIn 3 0 0)] := by
  have h' := stepTrace_projected hsim (seqSt q)
    [(allEdges seqD, seqIn 5 1 0), (allEdges seqD, seqIn 3 0 0)] rs (by rw [hd]; exact h)
  rw [hd] at h'
  exact h'

/-! ### Executable regressions

The theorems above are conditional on `SimSound`.  These are not: they run the
compiled evaluator on a bundle `mkSim` actually built, and fail the build if
the executable path stops agreeing. -/

def stepAgrees (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : Bool :=
  match mkSim D with
  | none     => false
  | some sim =>
      match stepOf sim D e i s with
      | .ok r    => encResult r == encResult (interpretDesign D e i s)
      | .error _ => false

#guard stepAgrees tinyD (allEdges tinyD) tinyIn tinySt
#guard stepAgrees seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0)
#guard stepAgrees seqD (allEdges seqD) (seqIn 3 0 0) (seqSt 4)
#guard stepAgrees seqD (allEdges seqD) (seqIn 3 1 1) (seqSt 4)

-- …and a refusal really happens, before anything is evaluated: a state of the
-- wrong shape is rejected rather than read out of bounds
#guard (match mkSim seqD with
        | none     => false
        | some sim =>
            match stepOf sim seqD (allEdges seqD) (seqIn 5 1 0) tinySt with
            | .error .runtimeShape => true
            | _                    => false)

def traceAgrees (D : DesignCert) (s : RuntimeState) (is : List Stim) : Bool :=
  match mkSim D with
  | none     => false
  | some sim =>
      match stepTrace (stepOf sim) D s is with
      | .ok rs   => rs.map encResult == (refTrace D s is).map encResult
      | .error _ => false

#guard traceAgrees tinyD tinySt [(allEdges tinyD, tinyIn), (allEdges tinyD, tinyIn)]
#guard traceAgrees seqD (seqSt 0)
  [(allEdges seqD, seqIn 5 1 0), (allEdges seqD, seqIn 3 0 0)]
#guard traceAgrees seqD (seqSt 0)
  [(allEdges seqD, seqIn 5 1 0), (allEdges seqD, seqIn 3 0 0),
   (allEdges seqD, seqIn 7 1 1), (allEdges seqD, seqIn 2 1 0)]

end FixtureStep

end Hw
end Projection
