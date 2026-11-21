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

Mathlib-free; the axioms are audited at the end.
-/
import HDLProof.Reach.Defs
import LeanSemanticPrimitives.Projection.CkptResume

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

end Reach
end HDLProof
