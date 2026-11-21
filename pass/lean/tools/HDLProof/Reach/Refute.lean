/-
# Unreachability from an output invariant

A checkpoint is unreachable when an invariant of every reachable state excludes it. The invariant a k-induction run proves is about OUTPUTS: an output keeps a value on
every cycle. This module turns such a fact into `¬ Reachable`:

* `matches_agree`: a state that `Matches` a valid checkpoint agrees with the checkpoint's own
  state (`restore c`) where a step can see it (`Projection.CkptResume.StateAgree`), when its
  memory words fit their memories (every reachable state's do: `wordsFit_trace`).
* `not_reachable_of_outputs`: if every state reachable from `s0` gives outputs satisfying `P`
  on every input and clock vector, and the checkpoint's own state gives outputs that do not on
  some vector, then no run from `s0` reaches a state matching the checkpoint. The step from
  "the states agree" to "the outputs are equal" is T4 (`Projection.CkptResume.step_agree`).
* `not_reachable_of_verdict`: the same from the form every `sym_verify` verdict theorem has
  (`runInvKOn_sound`, `runInvKF_sound`, ...: an output property on `refOut` for every input
  sequence of the declared widths whose assumptions hold), so an invariant `sym_verify` proved
  is reused, not proved again. `refState_eq` rewrites `refState` (every clock firing) as
  `refTrace` on the matching ticks. The side conditions are one Boolean, `unreachSideB`, that
  a theorem file can decide by evaluation.

Mathlib-free; the axioms are audited at the end.
-/
import HDLProof.Reach.Defs
import LeanSemanticPrimitives.Projection.CkptResume
-- the verdict form of sym_verify (refState, refOut, asmBit, propBit)
import HDLProof.Sym.Trace
import HDLProof.Sym.Unroll

namespace HDLProof
namespace Reach

open Compiler Compiler.Direct Compiler.Ckpt Projection.CkptResume

variable {D : DesignCert}

/-- A state matching a valid checkpoint agrees with the checkpoint's state. -/
theorem matches_agree {hash : Nat} {c : Ckpt} (hv : validate D hash c = none) {s : RuntimeState}
    (hsz : s.mems.size = D.memories.size) (hfit : WordsFit D s) (hm : Matches D c s) :
    StateAgree D s (restore c) := by
  have hval := valid_of hv
  refine ⟨fun k hk => ?_, fun k hk a h0 ha => ?_⟩
  · have hkc : k < c.flops.size := by rw [hval.nflops]; exact hk
    have hfe := matches_flop hm hk
    have hok := flopOK_of (hval.flops k hkc)
    rw [getElem!_pos D.flops k hk] at hok
    simp only [Array.getElem?_eq_getElem hkc, Option.map_some, Option.getD_some] at hfe
    simp only [restore, Array.getElem?_map, Array.getElem?_eq_getElem hkc, Option.map_some,
      Option.getD_some]
    generalize c.flops[k] = p at hfe hok ⊢
    obtain ⟨w, v⟩ := p
    simp only at hfe hok ⊢
    unfold flopAt at hfe
    rw [← hok.1] at hfe ⊢
    simp only [bv_resize, bv_uint_mk_bv]
    rw [← mk_bv_emod w (bv_uint _)]
    congr 1
    rw [← wordAt_cast, hfe]
    exact (Int.emod_eq_of_lt (Int.natCast_nonneg v) (by exact_mod_cast hok.2)).symm
  · have hkc : k < c.mems.size := by rw [hval.nmems]; exact hk
    have hok := memOK_of (hval.mems k hkc)
    rw [getElem!_pos D.memories k hk] at hok
    have hdep : a.toNat < D.memories[k].depth := by omega
    have hw := matches_word hm hk hdep
    have hmc : memCkOf D c k = c.mems[k] := by
      simp [memCkOf, Array.getElem?_eq_getElem hkc]
    rw [hmc] at hw
    -- the state's word, at `dw` bits, is all of it
    have hks : k < s.mems.size := by omega
    have hfitw := hfit k hk a h0 ha
    simp only [memWordAt, wordAt, Int.ofNat_eq_natCast, Int.toNat_of_nonneg h0] at hw
    have hdf : (s.mems[k]?.getD fun _ => mk_bv D.memories[k].dw 0) = (s.mems[k]?.getD fun _ => mk_bv 0 0) := by
      simp [Array.getElem?_eq_getElem hks]
    rw [hdf] at hw
    generalize (s.mems[k]?.getD fun _ => mk_bv 0 0) a = x at hw hfitw ⊢
    have hxfit : bv_uint x < 2 ^ D.memories[k].dw := by
      have := bv_uint_lt x
      have hp : (2 : Int) ^ x.width ≤ 2 ^ D.memories[k].dw := by
        exact_mod_cast Nat.pow_le_pow_right (by decide) hfitw
      omega
    -- the checkpoint's word
    simp only [restore, Array.getElem?_map, Array.getElem?_eq_getElem hkc, Option.map_some,
      Option.getD_some, memImage, h0, ite_true]
    rw [bv_uint_mk_bv]
    have hlook : ((memTable c.mems[k])[a.toNat]?.getD c.mems[k].dflt) < 2 ^ c.mems[k].dw := by
      have hdep' : a.toNat < c.mems[k].depth := by rw [hok.depth]; exact hdep
      by_cases hkey : ∃ p ∈ c.mems[k].words.toList, p.1 = a.toNat
      · obtain ⟨⟨x', v⟩, hp, hx⟩ := hkey
        simp only at hx; subst hx
        rw [table_key _ hok.sorted hp hdep']; exact (hok.words _ hp).2.1
      · rw [table_other _ (fun p hp he => hkey ⟨p, hp, he⟩) hdep']; exact hok.dflt
    rw [Int.emod_eq_of_lt (Int.natCast_nonneg _) (by exact_mod_cast hlook)]
    unfold ckWordAt at hw
    rw [← hw, wordAt_cast]
    exact (Int.emod_eq_of_lt (bv_uint_nonneg x) hxfit).symm

/-- **Unreachability from an output invariant.** If every state reachable from `s0` gives
outputs satisfying `P` on every input and clock vector, and the checkpoint's own state does not
on some vector, no run from `s0` reaches a state that matches the checkpoint. -/
theorem not_reachable_of_outputs (hsem : DesignSemWF D) (haddr : MemAddr.addrError D = none)
    (hroot : imgRootError D = none) (hword : wordError D = none)
    {hash : Nat} {c : Ckpt} (hv : validate D hash c = none)
    {s0 : RuntimeState} (hsz : s0.mems.size = D.memories.size) (hfit : WordsFit D s0)
    {adm : Tick → Bool} (P : Array BV → Prop)
    (hinv : ∀ ts : List Tick, ts.all adm = true → ∀ e i,
      P (interpretDesign D e i (refTrace D s0 ts).finalState).outputs)
    (hbad : ∃ e i, ¬ P (interpretDesign D e i (restore c)).outputs) :
    ¬ Reachable D s0 adm (Matches D c) := by
  rintro ⟨ts, ha, hm⟩
  obtain ⟨e, i, hnot⟩ := hbad
  have hag := matches_agree hv (mems_size_trace hsz ts) (wordsFit_trace hsem hroot hword ts s0 hfit) hm
  have heq := (step_agree (i := i) hsem haddr hroot hag e).1
  exact hnot (heq ▸ hinv ts ha e i)

#audit_axioms HDLProof.Reach.matches_agree
#audit_axioms HDLProof.Reach.not_reachable_of_outputs

/-! ## From a `sym_verify` verdict -/

open HDLProof.Sym (refState refOut edgesAll asmBit propBit inAsmOK PropSpec)

theorem refTrace_snoc (D : DesignCert) :
    ∀ (s : RuntimeState) (ts : List Tick) (tk : Tick),
      (refTrace D s (ts ++ [tk])).finalState =
        (interpretDesign D tk.edges tk.input (refTrace D s ts).finalState).nextState
  | _, [], _ => rfl
  | s, t :: ts, tk => by
    simp only [List.cons_append, refTrace]
    exact refTrace_snoc D _ ts tk

/-- The ticks `sym_verify`'s traces take: every clock, the inputs `ins 0 .. ins (n-1)`. -/
def symTicks (D : DesignCert) (ins : Nat → RuntimeInput) (n : Nat) : List Tick :=
  (List.range n).map fun j => { edges := edgesAll D, input := ins j }

/-- `refState` is `refTrace` on those ticks. -/
theorem refState_eq (D : DesignCert) (r0 : RuntimeState) (ins : Nat → RuntimeInput) :
    ∀ n, refState D r0 ins n = (refTrace D r0 (symTicks D ins n)).finalState
  | 0 => rfl
  | n + 1 => by
    rw [symTicks, List.range_succ, List.map_append, List.map_singleton, refTrace_snoc]
    show (interpretDesign D (edgesAll D) (ins n) (refState D r0 ins n)).nextState = _
    rw [refState_eq D r0 ins n]
    rfl

/-- The ticks a `sym_verify` verdict speaks of: every clock fires, the inputs have the declared
widths, and the input assumptions hold. -/
def symAdm (D : DesignCert) (inW : List Nat) (asmIn : List (Nat × Int)) (tk : Tick) : Bool :=
  tk.edges == edgesAll D && tk.input.toList.map BV.width == inW && inAsmOK tk.input asmIn

/-- Every word of every memory, below its depth, at most `dw` bits wide: `WordsFit`, decided. -/
def wordsFitB (D : DesignCert) (s : RuntimeState) : Bool :=
  (List.range D.memories.size).all fun k =>
    (List.range D.memories[k]!.depth).all fun a =>
      decide (((s.mems[k]?.getD fun _ => mk_bv 0 0) (Int.ofNat a)).width ≤ D.memories[k]!.dw)

theorem wordsFitB_sound {s : RuntimeState} (h : wordsFitB D s = true) : WordsFit D s := by
  intro k hk a h0 ha
  unfold wordsFitB at h
  have h1 := List.all_eq_true.mp h k (List.mem_range.mpr hk)
  rw [getElem!_pos D.memories k hk] at h1
  have h2 := List.all_eq_true.mp h1 a.toNat (List.mem_range.mpr (by omega))
  have ha' : Int.ofNat a.toNat = a := Int.toNat_of_nonneg h0
  rw [ha'] at h2
  exact of_decide_eq_true h2

/-- The direct simulator accepts the certificate. -/
def designOKB (D : DesignCert) : Bool :=
  match Direct.checkDesign D with
  | .ok _ => true
  | .error _ => false

theorem designOKB_sound (h : designOKB D = true) : DesignSemWF D := by
  unfold designOKB at h
  cases hc : Direct.checkDesign D with
  | error e => rw [hc] at h; cases h
  | ok u => exact Direct.checkDesign_sound hc

/-- Every side condition of `not_reachable_of_verdict`, as one Boolean: the certificate checks
(`checkDesign`, the address check, `validate`'s two certificate rules), the checkpoint is valid,
and the start state has the certificate's memories, each word fitting. -/
def unreachSideB (D : DesignCert) (c : Ckpt) (r0 : RuntimeState) : Bool :=
  designOKB D && MemAddr.addrError D == none && imgRootError D == none && wordError D == none &&
    validate D (designHash D) c == none && r0.mems.size == D.memories.size && wordsFitB D r0

/-- The probe: an input vector of the declared widths that meets the input assumptions, on which
the checkpoint's own state, every clock firing, violates the property. -/
def probeB (D : DesignCert) (c : Ckpt) (inW : List Nat) (asmIn : List (Nat × Int))
    (prop : PropSpec) (i : RuntimeInput) : Bool :=
  i.toList.map BV.width == inW && inAsmOK i asmIn &&
    !propBit (interpretDesign D (edgesAll D) i (restore c)).outputs prop

/-- **Unreachability from a `sym_verify` verdict.** `hinv` is the conclusion of every route's
verdict theorem (`runInvKOn_sound`, `runInvKF_sound`, ...; the `invariant` of an emitted
theorem file) for a property with no output assumption. If the checkpoint's own state violates
the property on a probe input, no admitted run from `r0` reaches a state matching it. -/
theorem not_reachable_of_verdict {c : Ckpt} {r0 : RuntimeState} {inW : List Nat}
    {asmOut : Option PropSpec} {asmIn : List (Nat × Int)} {prop : PropSpec}
    (hside : unreachSideB D c r0 = true) (hno : asmOut = none)
    (hinv : ∀ ins : Nat → RuntimeInput, (∀ n, (ins n).toList.map BV.width = inW) →
      ∀ n, (∀ j, j ≤ n → asmBit (refOut D r0 ins j) (ins j) asmOut asmIn = true) →
        propBit (refOut D r0 ins n) prop = true)
    {i : RuntimeInput} (hprobe : probeB D c inW asmIn prop i = true) :
    ¬ Reachable D r0 (symAdm D inW asmIn) (Matches D c) := by
  subst hno
  simp only [unreachSideB, Bool.and_eq_true, beq_iff_eq] at hside
  obtain ⟨⟨⟨⟨⟨⟨hd, haddr⟩, hroot⟩, hword⟩, hv⟩, hsz⟩, hfit⟩ := hside
  simp only [probeB, Bool.and_eq_true, beq_iff_eq, Bool.not_eq_true'] at hprobe
  obtain ⟨⟨hiw, hia⟩, hbad⟩ := hprobe
  have hsem := designOKB_sound hd
  rintro ⟨ts, ha, hm⟩
  have hadm : ∀ tk ∈ ts, symAdm D inW asmIn tk = true := List.all_eq_true.mp ha
  -- the input sequence: the run's inputs, then the probe
  let ins : Nat → RuntimeInput := fun j => if h : j < ts.length then ts[j].input else i
  have hts : symTicks D ins ts.length = ts := by
    apply List.ext_getElem (by simp [symTicks])
    intro j h1 h2
    have hj := hadm ts[j] (List.getElem_mem h2)
    simp only [symAdm, Bool.and_eq_true, beq_iff_eq] at hj
    simp only [symTicks, List.getElem_map, List.getElem_range, ins, h2, ↓reduceDIte]
    have he := hj.1.1
    revert he
    generalize ts[j] = tk
    intro he
    cases tk
    simp only at he ⊢
    rw [he]
  have hins : ∀ n, (ins n).toList.map BV.width = inW := by
    intro n
    by_cases h : n < ts.length
    · have hj := hadm ts[n] (List.getElem_mem h)
      simp only [symAdm, Bool.and_eq_true, beq_iff_eq] at hj
      simp only [ins, h, ↓reduceDIte]; exact hj.1.2
    · simp only [ins, h, ↓reduceDIte]; exact hiw
  have hasm : ∀ j, j ≤ ts.length →
      asmBit (refOut D r0 ins j) (ins j) none asmIn = true := by
    intro j hj
    simp only [asmBit, Bool.true_and]
    by_cases h : j < ts.length
    · have hjj := hadm ts[j] (List.getElem_mem h)
      simp only [symAdm, Bool.and_eq_true] at hjj
      simp only [ins, h, ↓reduceDIte]; exact hjj.2
    · simp only [ins, h, ↓reduceDIte]; exact hia
  have hP := hinv ins hins ts.length hasm
  have hin : ins ts.length = i := by simp [ins]
  simp only [refOut, hin, refState_eq, hts] at hP
  have hag := matches_agree hv (mems_size_trace hsz ts)
    (wordsFit_trace hsem hroot hword ts r0 (wordsFitB_sound hfit)) hm
  have heq := (step_agree (i := i) hsem haddr hroot hag (edgesAll D)).1
  rw [heq] at hP
  rw [hP] at hbad
  cases hbad

#audit_axioms HDLProof.Reach.not_reachable_of_verdict

end Reach
end HDLProof
