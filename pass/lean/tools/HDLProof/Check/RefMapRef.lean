/-
`Check.RefMapRef` -- the refinement-map results in the shared reference semantics, and the
checks on the fused route.

`runRefMapF` and `runSpecFactsF` specialize each certificate with the proved fused specializer
(`projectMemFDesignAt`) and run `runRefMapOf` / `runSpecFactsOf` on the residual. These closed
Bools are what an emitted theorem file evaluates.

The checker reasons about `outM`, the trace of the finite-memory semantics. On a checked
implementation that trace IS `interpretDesign`'s (`refOut`, `rm_denote`), so this file restates
the streams over `refOut` (`portsR`, `GoodR`, `implUpToR`, `implStreamR`, `ImplObeysR`,
`progOfR`) and proves:
- `refmap_compose_ref`: two checked implementations of one checked spec show the same streams;
- `start_ref`: their map states at `R0` agree when their initial maps do (`InitAgree`);
- `refmap_progress_ref`: under justice, every allowed ranked channel gets an event.
The verdicts enter as `RefMapChecked` / `SpecFactsChecked`: some proofs pass the check. An
emitted F2 file proves that by `native_decide`; an emitted F1 file assumes it as a named axiom.
Nothing here mentions the specializer's state or residual. Mathlib-free.
-/
import HDLProof.Check.RefMapCompose
import LeanSemanticPrimitives.Compiler.CertIOCodec

open Projection
namespace HDLProof.Check
open HDLProof.Sym HDLProof.Kit Std.Tactic.BVDecide Projection.MemProjection Projection.Hw
  Projection.HwMem Compiler

/-! ## The checks on the fused route -/

/-- The refinement-map check, each certificate specialized with fuel `sf`/`wf` (`sfS`/`wfS`
for the spec). -/
def runRefMapF (sf wf sfS wfS : Nat) (DI DS : DesignCert) (sp : RefMapSpec) (s0 : StateM)
    (pfIB : ByteArray) (pfIS : List (List ByteArray)) (pfB : ByteArray)
    (pfS : List (List ByteArray)) : Bool :=
  admitsWB DI && admitsWB DS &&
    match MemFused.projectMemFDesignAt sf wf DI, MemFused.projectMemFDesignAt sfS wfS DS with
    | .ok RI, .ok RS => runRefMapOf DI DS RI RS sp s0 pfIB pfIS pfB pfS
    | _, _ => false

def runSpecFactsF (sf wf : Nat) (DS : DesignCert) (sp : RefMapSpec) (pfF : List ByteArray) : Bool :=
  admitsWB DS &&
    match MemFused.projectMemFDesignAt sf wf DS with
    | .ok RS => runSpecFactsOf DS RS sp pfF
    | .error _ => false

theorem runRefMapF_parts {sf wf sfS wfS : Nat} {DI DS : DesignCert} {sp : RefMapSpec} {s0 : StateM}
    {pfIB pfB : ByteArray} {pfIS pfS : List (List ByteArray)}
    (h : runRefMapF sf wf sfS wfS DI DS sp s0 pfIB pfIS pfB pfS = true) :
    ∃ RI RS, MemResidual DI RI ∧ admitsWB DI = true ∧ MemResidual DS RS ∧ admitsWB DS = true
      ∧ runRefMapOf DI DS RI RS sp s0 pfIB pfIS pfB pfS = true := by
  unfold runRefMapF at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨⟨hAI, hAS⟩, hr⟩ := h
  split at hr
  · rename_i RI RS hI hS
    exact ⟨RI, RS, MemResidual.of_projF hI, hAI, MemResidual.of_projF hS, hAS, hr⟩
  · cases hr

theorem runSpecFactsF_parts {sf wf : Nat} {DS : DesignCert} {sp : RefMapSpec} {pfF : List ByteArray}
    (h : runSpecFactsF sf wf DS sp pfF = true) :
    ∃ RS, MemResidual DS RS ∧ admitsWB DS = true ∧ runSpecFactsOf DS RS sp pfF = true := by
  unfold runSpecFactsF at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨hAS, hr⟩ := h
  split at hr
  · rename_i RS hS
    exact ⟨RS, MemResidual.of_projF hS, hAS, hr⟩
  · cases hr

/-! ## The checked trace is the reference trace -/

section
variable {DI DS : DesignCert} {RI RS : Program} {sp : RefMapSpec} {s0 : StateM}
  {pfIB pfB : ByteArray} {pfIS pfS : List (List ByteArray)} {ins : Nat → RuntimeInput}

/-- On a checked implementation, every cycle's outputs are `interpretDesign`'s. -/
theorem rm_denote (hrI : MemResidual DI RI) (hAI : admitsWB DI = true)
    (hchk : runRefMapOf DI DS RI RS sp s0 pfIB pfIS pfB pfS = true)
    (hins : ∀ n, (ins n).toList.map BV.width = sp.inv.inW) :
    ∀ n, outM DI s0 ins n = refOut DI (StateM.denote s0) ins n := by
  have hconj := (rm_parts hchk).1
  have hside := (conjT_parts hconj).1
  have hinv := invT_all hrI hAI hconj hins
  have hR : ∀ n, runtimeFullOKB DI (ins n) (traceM DI s0 ins n) (edgesAll DI) = true :=
    fun n => runtime_of_invC hside (hinv n).1 (hinv n).2 (hins n)
  exact fun n => (traceM_denote hAI hR n).2

end

/-! ## The streams in the reference semantics -/

/-- The ports of cycle `n` of `interpretDesign`'s run from `r0`. -/
def portsR (DI : DesignCert) (r0 : RuntimeState) (ins : Nat → RuntimeInput) (n : Nat) : Ports :=
  ⟨refOut DI r0 ins n, refOut DI r0 ins (n + 1), ins n⟩

/-- The cycles up to `n` meet the assumptions. -/
def GoodR (DI : DesignCert) (sp : RefMapSpec) (r0 : RuntimeState) (ins : Nat → RuntimeInput)
    (n : Nat) : Prop :=
  ∀ j, j ≤ n → asmBit (refOut DI r0 ins j) (ins j) sp.inv.asmOut sp.inv.asmIn = true

/-- What the slots that fired show, over cycles `R0 .. n-1`. -/
def implUpToR (DI : DesignCert) (sp : RefMapSpec) (r0 : RuntimeState) (ins : Nat → RuntimeInput) :
    Nat → List (Nat × (List Int × List Int))
  | 0 => []
  | n + 1 => implUpToR DI sp r0 ins n
      ++ (if sp.R0 ≤ n then obsL (portsR DI r0 ins n) sp.slots else [])

/-- Channel `x` up to cycle `n`: each event's observed and response words. -/
def implStreamR (DI : DesignCert) (sp : RefMapSpec) (r0 : RuntimeState) (ins : Nat → RuntimeInput)
    (x n : Nat) : List (List Int × List Int) :=
  Theory.Spec.chanOf x (implUpToR DI sp r0 ins n)

/-- The environment `E` answers every fired slot from its channel's observed history. -/
def ImplObeysR (DI : DesignCert) (sp : RefMapSpec) (r0 : RuntimeState) (ins : Nat → RuntimeInput)
    (E : Theory.Env Nat (List Int) (List Int)) : Prop :=
  ∀ n, Theory.Spec.ObeysA E (implUpToR DI sp r0 ins n)

/-- The progress data, read from `interpretDesign`'s ports. -/
def progOfR (DI : DesignCert) (sp : RefMapSpec) (r0 : RuntimeState) (ins : Nat → RuntimeInput) :
    Theory.Progress Nat Nat where
  rank n c := match sp.chans[c]? with
    | some ch => match ch.rank with
      | some (ro, _) => wordAt (refOut DI r0 ins n) ro
      | none => 0
    | none => 0
  sel n c := match sp.chans[c]? with
    | some ch => match ch.rank with
      | some (_, so) => selOf (refOut DI r0 ins n) so
      | none => none
    | none => none
  reqEn n j := match sp.reqs[j]? with
    | some r => r.en.bit (portsR DI r0 ins n)
    | none => false
  fires n j := match sp.reqs[j]? with
    | some r => r.fires.bit (portsR DI r0 ins n)
    | none => false

section
variable {DI : DesignCert} {sp : RefMapSpec} {s0 : StateM} {ins : Nat → RuntimeInput}
  (hden : ∀ n, outM DI s0 ins n = refOut DI (StateM.denote s0) ins n)
include hden

theorem portsAt_ref (n : Nat) : portsAt DI s0 ins n = portsR DI (StateM.denote s0) ins n := by
  simp only [portsAt, portsR, hden]

theorem good_ref (n : Nat) : Good DI sp s0 ins n ↔ GoodR DI sp (StateM.denote s0) ins n := by
  simp only [Good, GoodR, hden]

theorem implUpTo_ref : ∀ n, implUpTo DI sp s0 ins n = implUpToR DI sp (StateM.denote s0) ins n
  | 0 => rfl
  | n + 1 => by simp only [implUpTo, implUpToR, implUpTo_ref n, portsAt_ref hden]

theorem implStream_ref (x n : Nat) :
    implStream DI sp s0 ins x n = implStreamR DI sp (StateM.denote s0) ins x n := by
  simp only [implStream, implStreamR, implUpTo_ref hden]

theorem implObeys_ref (E : Theory.Env Nat (List Int) (List Int)) :
    ImplObeys DI sp s0 ins E ↔ ImplObeysR DI sp (StateM.denote s0) ins E := by
  simp only [ImplObeys, ImplObeysR, implUpTo_ref hden]

theorem progOf_ref : progOf DI sp s0 ins = progOfR DI sp (StateM.denote s0) ins := by
  simp only [progOf, progOfR, hden, portsAt_ref hden]
  rfl

end

/-! ## What an emitted verdict states -/

/-- A certificate from its DCERT text (`default` when it does not read). Emitted theorem files
define every certificate by it, so two files that include the same text name the same
certificate by unfolding alone (the texts are equal literals). -/
def certOf (text : String) : DesignCert :=
  match CertIO.readCertChecked text.toUTF8 with
  | .ok d => d
  | .error _ => default


/-- Some proofs pass the refinement-map check: the statement an emitted F2 theorem proves
(`native_decide`) and an emitted F1 axiom assumes. -/
def RefMapChecked (sf wf sfS wfS : Nat) (DI DS : DesignCert) (sp : RefMapSpec) (s0 : StateM) : Prop :=
  ∃ pfIB pfIS pfB pfS, runRefMapF sf wf sfS wfS DI DS sp s0 pfIB pfIS pfB pfS = true

/-- Some proofs pass the spec-facts check. -/
def SpecFactsChecked (sf wf : Nat) (DS : DesignCert) (sp : RefMapSpec) : Prop :=
  ∃ pfF, runSpecFactsF sf wf DS sp pfF = true

/-- The parts of a checked map: its facts, and that its trace is the reference trace. -/
theorem checked_parts {sf wf sfS wfS : Nat} {DI DS : DesignCert} {sp : RefMapSpec} {s0 : StateM}
    (h : RefMapChecked sf wf sfS wfS DI DS sp s0) {ins : Nat → RuntimeInput}
    (hins : ∀ n, (ins n).toList.map BV.width = sp.inv.inW) :
    admitsWB DS = true ∧ RefMapFacts DI DS sp s0 ins
      ∧ ∀ n, outM DI s0 ins n = refOut DI (StateM.denote s0) ins n := by
  obtain ⟨_, _, _, _, h⟩ := h
  obtain ⟨RI, RS, hrI, hAI, hrS, hAS, hc⟩ := runRefMapF_parts h
  exact ⟨hAS, runRefMapOf_sound hrI hAI hrS hAS hc hins, rm_denote hrI hAI hc hins⟩

/-- The facts of a checked spec, for any map with the same spec. -/
theorem specFacts_checked {sf wf : Nat} {DS : DesignCert} {spF sp : RefMapSpec}
    (h : SpecFactsChecked sf wf DS spF) (hs : SameSpec spF sp) :
    specSideOK DS sp = true ∧ SpecFactsOK DS sp := by
  obtain ⟨_, h⟩ := h
  obtain ⟨RS, hrS, hAS, hc⟩ := runSpecFactsF_parts h
  exact ⟨by rw [← specSide_same hs]; exact (sf_parts hc).1,
    specFacts_same hs (runSpecFactsOf_sound hrS hAS hc)⟩

/-! ## The start -/

/-- Spec flop `k` starts the same in both maps: both pin it to one constant, or both read
implementation outputs at cycle 0 whose values agree. -/
def InitAgree (spA spB : RefMapSpec) (oA oB : Array BV) (k : Nat) : Prop :=
  (∃ v, spA.init[k]? = some (.cnst v) ∧ spB.init[k]? = some (.cnst v))
  ∨ ∃ a b, spA.init[k]? = some (.out0 a) ∧ spB.init[k]? = some (.out0 b)
      ∧ bv_uint (oA.getD a default) = bv_uint (oB.getD b default)

/-- `InitAgree` from the maps' init lines: the same list, no `free` entry, and every
implementation output an `out0` entry reads agreeing at cycle 0. -/
theorem initAgree_of_same {spA spB : RefMapSpec} {oA oB : Array BV} (hinit : spA.init = spB.init)
    (hfree : spA.init.all (fun e => match e with | .free => false | _ => true) = true)
    (hout : ∀ o, InitE.out0 o ∈ spA.init → bv_uint (oA.getD o default) = bv_uint (oB.getD o default)) :
    ∀ k, k < spA.init.length → InitAgree spA spB oA oB k := by
  intro k hk
  have he : spA.init[k]? = some spA.init[k] := List.getElem?_eq_getElem hk
  have hmem : spA.init[k] ∈ spA.init := List.getElem_mem hk
  have hnf := List.all_eq_true.mp hfree _ hmem
  unfold InitAgree
  rw [← hinit]
  generalize spA.init[k] = e at he hmem hnf
  cases e with
  | cnst v => exact Or.inl ⟨v, he, he⟩
  | out0 o => exact Or.inr ⟨o, o, he, he, hout o hmem⟩
  | free => simp at hnf

theorem start_of_facts {DIa DIb DS : DesignCert} {spA spB : RefMapSpec} {s0A s0B : StateM}
    {insA insB : Nat → RuntimeInput} (hsame : SameSpec spA spB)
    (hFA : RefMapFacts DIa DS spA s0A insA) (hFB : RefMapFacts DIb DS spB s0B insB)
    (hgA : Good DIa spA s0A insA spA.R0) (hgB : Good DIb spB s0B insB spB.R0)
    (hI : ∀ k, k < spA.LS.fws.length →
      InitAgree spA spB (outM DIa s0A insA 0) (outM DIb s0B insB 0) k) :
    alphaSt spA (outM DIa s0A insA spA.R0) = alphaSt spB (outM DIb s0B insB spB.R0) := by
  obtain ⟨iA, -, nA⟩ := hFA.base hgA
  obtain ⟨iB, -, nB⟩ := hFB.base hgB
  apply specNorm_ext nA ((specNorm_same hsame _).mpr nB)
  intro k hk
  rcases hI k hk with ⟨v, ha, hb⟩ | ⟨a, b, ha, hb, hab⟩
  · obtain ⟨b1, h1, e1⟩ := iA k _ ha
    obtain ⟨b2, h2, e2⟩ := iB k _ hb
    exact ⟨b1, b2, h1, h2, e1.trans e2.symm⟩
  · obtain ⟨b1, h1, e1⟩ := iA k _ ha
    obtain ⟨b2, h2, e2⟩ := iB k _ hb
    exact ⟨b1, b2, h1, h2, by rw [e1, e2]; exact hab⟩

/-- The start in the reference semantics: two checked maps whose initial maps agree start from
the same map state at `R0`. -/
theorem start_ref {sfA wfA sfSa wfSa sfB wfB sfSb wfSb : Nat} {DIa DIb DS : DesignCert}
    {spA spB : RefMapSpec} {s0A s0B : StateM}
    (hA : RefMapChecked sfA wfA sfSa wfSa DIa DS spA s0A)
    (hB : RefMapChecked sfB wfB sfSb wfSb DIb DS spB s0B) (hsame : SameSpec spA spB)
    {insA insB : Nat → RuntimeInput}
    (hinsA : ∀ n, (insA n).toList.map BV.width = spA.inv.inW)
    (hinsB : ∀ n, (insB n).toList.map BV.width = spB.inv.inW)
    (hgA : GoodR DIa spA (StateM.denote s0A) insA spA.R0)
    (hgB : GoodR DIb spB (StateM.denote s0B) insB spB.R0)
    (hI : ∀ k, k < spA.LS.fws.length → InitAgree spA spB
      (refOut DIa (StateM.denote s0A) insA 0) (refOut DIb (StateM.denote s0B) insB 0) k) :
    alphaSt spA (refOut DIa (StateM.denote s0A) insA spA.R0)
      = alphaSt spB (refOut DIb (StateM.denote s0B) insB spB.R0) := by
  obtain ⟨-, hFA, hdA⟩ := checked_parts hA hinsA
  obtain ⟨-, hFB, hdB⟩ := checked_parts hB hinsB
  rw [← hdA, ← hdB]
  exact start_of_facts hsame hFA hFB ((good_ref hdA _).mpr hgA) ((good_ref hdB _).mpr hgB)
    (fun k hk => by rw [hdA, hdB]; exact hI k hk)

/-! ## Composition and progress, in the reference semantics -/

/-- **Two checked implementations of one checked spec show the same streams** in
`interpretDesign`'s semantics. `DIa` and `DIb` each pass the refinement-map check against `DS`,
with maps of the same spec, and `DS` passes the spec facts (checked once, on a map `spF` of the
same spec). Take any two runs that meet their assumptions, start from the same map state at
`R0` (`start_ref`), and are each answered by the same environment `E` from their own histories.
On every channel the observed and response words agree on the common prefix, at any two cycle
counts. -/
theorem refmap_compose_ref {sfA wfA sfSa wfSa sfB wfB sfSb wfSb sfF wfF : Nat}
    {DIa DIb DS : DesignCert} {spA spB spF : RefMapSpec} {s0A s0B : StateM}
    (hA : RefMapChecked sfA wfA sfSa wfSa DIa DS spA s0A)
    (hB : RefMapChecked sfB wfB sfSb wfSb DIb DS spB s0B)
    (hF : SpecFactsChecked sfF wfF DS spF) (hfa : SameSpec spF spA) (hsame : SameSpec spA spB)
    {insA insB : Nat → RuntimeInput}
    (hinsA : ∀ n, (insA n).toList.map BV.width = spA.inv.inW)
    (hinsB : ∀ n, (insB n).toList.map BV.width = spB.inv.inW)
    (hgA : ∀ n, GoodR DIa spA (StateM.denote s0A) insA n)
    (hgB : ∀ n, GoodR DIb spB (StateM.denote s0B) insB n)
    (hstart : alphaSt spA (refOut DIa (StateM.denote s0A) insA spA.R0)
      = alphaSt spB (refOut DIb (StateM.denote s0B) insB spB.R0))
    {E : Theory.Env Nat (List Int) (List Int)}
    (hEA : ImplObeysR DIa spA (StateM.denote s0A) insA E)
    (hEB : ImplObeysR DIb spB (StateM.denote s0B) insB E) :
    ∀ nA nB x, Theory.PrefixAgree (implStreamR DIa spA (StateM.denote s0A) insA x nA)
      (implStreamR DIb spB (StateM.denote s0B) insB x nB) := by
  obtain ⟨hAS, hFA, hdA⟩ := checked_parts hA hinsA
  obtain ⟨-, hFB, hdB⟩ := checked_parts hB hinsB
  obtain ⟨hside, hSF⟩ := specFacts_checked hF hfa
  intro nA nB x
  rw [← implStream_ref hdA, ← implStream_ref hdB]
  refine compose_of_facts hAS hside hSF hsame hFA hFB
    (fun n => (good_ref hdA n).mpr (hgA n)) (fun n => (good_ref hdB n).mpr (hgB n))
    (by rw [hdA, hdB]; exact hstart) ((implObeys_ref hdA E).mpr hEA) ((implObeys_ref hdB E).mpr hEB)
    nA nB x

/-- **Progress** in `interpretDesign`'s semantics. Under justice of the environment
requirements, a ranked channel that is allowed at a cycle from `R0` on gets, at that cycle or
later, a slot of its own that fires. -/
theorem refmap_progress_ref {sf wf sfS wfS sfF wfF : Nat} {DI DS : DesignCert}
    {sp spF : RefMapSpec} {s0 : StateM}
    (hchk : RefMapChecked sf wf sfS wfS DI DS sp s0)
    (hF : SpecFactsChecked sfF wfF DS spF) (hfs : SameSpec spF sp)
    {ins : Nat → RuntimeInput} (hins : ∀ n, (ins n).toList.map BV.width = sp.inv.inW)
    (hg : ∀ n, GoodR DI sp (StateM.denote s0) ins n)
    (hJ : (progOfR DI sp (StateM.denote s0) ins).Just) {c : Nat} {ch : ChanS} {r : Nat × Nat}
    (hch : sp.chans[c]? = some ch) (hrank : ch.rank = some r) :
    ∀ n, sp.R0 ≤ n → (specOf DS sp).en (alphaSt sp (refOut DI (StateM.denote s0) ins n)) c = true →
      ∃ m, n ≤ m ∧ ∃ sl ∈ sp.slots, sl.chan = c
        ∧ sl.kind.bit (portsR DI (StateM.denote s0) ins m) = true := by
  obtain ⟨hAS, hFc, hd⟩ := checked_parts hchk hins
  obtain ⟨hside, hSF⟩ := specFacts_checked hF hfs
  intro n hn hen
  rw [← hd] at hen
  obtain ⟨m, hm, sl, hsl, h1, h2⟩ := progress_of_facts hAS hside hSF hFc
    (fun n => (good_ref hd n).mpr (hg n)) (by rw [progOf_ref hd]; exact hJ) hch hrank n hn hen
  exact ⟨m, hm, sl, hsl, h1, by rw [← portsAt_ref hd]; exact h2⟩

end HDLProof.Check
