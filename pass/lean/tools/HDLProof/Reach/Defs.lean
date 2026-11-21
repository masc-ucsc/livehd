/-
# Reachable states of a design

The definitions of a reachable state and the witness checker.

* `Reachable D s0 adm T`: some finite tick sequence, every tick admitted by `adm`, takes the
  reference semantics (`refTrace`, iterated `interpretDesign`) from `s0` to a state in `T`.
  `ReachableIn` fixes the number of steps.
* `Matches D c s`: state `s` agrees with checkpoint `c` everywhere a step can see it: each
  flop at its declared width, each memory word below `depth` at the memory's width. Words at or
  past `depth` and the memory's default play no part (no step reads them,
  `Projection/MemAddrSound`).
* `checkReach D s0 c adm ts`: runs `runDirect` (the checked direct simulator) on the witness
  `ts` and compares the final state with `c`. `checkReach_sound`: a `true` answer proves
  `ReachableIn D s0 adm (Matches D c) ts.length`. It is proved once, for every design,
  checkpoint and witness.
* `not_reachable_of_inv`: an inductive invariant that holds at `s0` and excludes the target
  proves the target unreachable.

Mathlib-free; the axioms are audited in `HDLProof/Check/Audit.lean`'s style at the end.
-/
import LeanSemanticPrimitives.Compiler.DirectTrace
import LeanSemanticPrimitives.Compiler.CkptSound
import LeanSemanticPrimitives.Projection.AxiomGate

namespace HDLProof
namespace Reach

open Compiler Compiler.Direct Compiler.Ckpt

/-! ## Reachability -/

/-- Some finite sequence of admitted ticks takes `s0` to a state in `T`. -/
def Reachable (D : DesignCert) (s0 : RuntimeState) (adm : Tick → Bool)
    (T : RuntimeState → Prop) : Prop :=
  ∃ ts : List Tick, ts.all adm = true ∧ T (refTrace D s0 ts).finalState

/-- As `Reachable`, in exactly `n` steps. -/
def ReachableIn (D : DesignCert) (s0 : RuntimeState) (adm : Tick → Bool)
    (T : RuntimeState → Prop) (n : Nat) : Prop :=
  ∃ ts : List Tick, ts.length = n ∧ ts.all adm = true ∧ T (refTrace D s0 ts).finalState

theorem ReachableIn.reachable {D : DesignCert} {s0 : RuntimeState} {adm : Tick → Bool}
    {T : RuntimeState → Prop} {n : Nat} (h : ReachableIn D s0 adm T n) :
    Reachable D s0 adm T := by
  obtain ⟨ts, _, ha, hT⟩ := h
  exact ⟨ts, ha, hT⟩

/-- Every tick is admitted. -/
def anyTick : Tick → Bool := fun _ => true

/-! ## A state against a checkpoint -/

/-- Flop `k` of `s`, unsigned at width `w`: what `sourceValue` reads. -/
def flopAt (s : RuntimeState) (k w : Nat) : Nat :=
  (bv_uint (s.flops[k]?.getD (mk_bv w 0))).toNat % 2 ^ w

/-- Word `a` of memory `k` of `s`, unsigned at `dw` bits. -/
def memWordAt (s : RuntimeState) (k dw a : Nat) : Nat :=
  wordAt dw (s.mems[k]?.getD fun _ => mk_bv dw 0) (Int.ofNat a)

/-- Word `a` of a checkpoint memory: the listed value, else its default. -/
def ckWordAt (m : MemCk) (a : Nat) : Nat := (memTable m)[a]?.getD m.dflt

/-- The first flop of `D` where `s` and checkpoint `c` differ, or `none`. -/
def flopDiff (D : DesignCert) (c : Ckpt) (s : RuntimeState) : Option (Nat × Nat × Nat) :=
  (List.range D.flops.size).findSome? fun k =>
    let w := D.flops[k]!.width
    let want := (c.flops[k]?.map (·.2)).getD 0
    let got := flopAt s k w
    if got == want then none else some (k, got, want)

/-- Checkpoint `c`'s memory `k`, or an empty one of `D`'s shape when `c` has none. -/
def memCkOf (D : DesignCert) (c : Ckpt) (k : Nat) : MemCk :=
  let md := D.memories[k]!
  c.mems[k]?.getD { aw := md.aw, dw := md.dw, depth := md.depth, dflt := 0, words := #[] }

/-- The first memory word below `depth` where `s` and `c` differ: memory, address, the state's
word, the checkpoint's word. -/
def wordDiff (D : DesignCert) (c : Ckpt) (s : RuntimeState) :
    Option (Nat × Nat × Nat × Nat) :=
  (List.range D.memories.size).findSome? fun k =>
    let md := D.memories[k]!
    let m := memCkOf D c k
    (List.range md.depth).findSome? fun a =>
      let got := memWordAt s k md.dw a
      let want := ckWordAt m a
      if got == want then none else some (k, a, got, want)

/-- `s` agrees with `c` everywhere a step can see it. -/
def matchesB (D : DesignCert) (c : Ckpt) (s : RuntimeState) : Bool :=
  (flopDiff D c s).isNone && (wordDiff D c s).isNone

/-- The target a checkpoint names. -/
def Matches (D : DesignCert) (c : Ckpt) (s : RuntimeState) : Prop := matchesB D c s = true

theorem matches_flop {D : DesignCert} {c : Ckpt} {s : RuntimeState} (h : Matches D c s)
    {k : Nat} (hk : k < D.flops.size) :
    flopAt s k D.flops[k].width = (c.flops[k]?.map (·.2)).getD 0 := by
  unfold Matches matchesB at h
  simp only [Bool.and_eq_true, Option.isNone_iff_eq_none] at h
  have := List.findSome?_eq_none_iff.mp h.1 k (List.mem_range.mpr hk)
  simp only [getElem!_pos D.flops k hk] at this
  split at this
  · rename_i he; exact beq_iff_eq.mp he
  · cases this

theorem matches_word {D : DesignCert} {c : Ckpt} {s : RuntimeState} (h : Matches D c s)
    {k : Nat} (hk : k < D.memories.size) {a : Nat} (ha : a < D.memories[k].depth) :
    memWordAt s k D.memories[k].dw a = ckWordAt (memCkOf D c k) a := by
  unfold Matches matchesB at h
  simp only [Bool.and_eq_true, Option.isNone_iff_eq_none] at h
  have h1 := List.findSome?_eq_none_iff.mp h.2 k (List.mem_range.mpr hk)
  simp only [getElem!_pos D.memories k hk] at h1
  have := List.findSome?_eq_none_iff.mp h1 a (List.mem_range.mpr ha)
  split at this
  · rename_i he; exact beq_iff_eq.mp he
  · cases this

/-! ## The witness checker -/

/-- Run the witness `ts` from `s0` with the checked direct simulator and compare the final
state with `c`; every tick must be admitted. -/
def checkReach (D : DesignCert) (s0 : RuntimeState) (c : Ckpt) (adm : Tick → Bool)
    (ts : List Tick) : Bool :=
  ts.all adm &&
    match runDirect D s0 ts with
    | .ok t    => matchesB D c t.finalState
    | .error _ => false

/-- **The witness checker is sound**: a `true` answer proves that `c` is reachable from `s0`
in `ts.length` admitted steps of the reference semantics. -/
theorem checkReach_sound {D : DesignCert} {s0 : RuntimeState} {c : Ckpt} {adm : Tick → Bool}
    {ts : List Tick} (h : checkReach D s0 c adm ts = true) :
    ReachableIn D s0 adm (Matches D c) ts.length := by
  unfold checkReach at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨ha, hr⟩ := h
  refine ⟨ts, rfl, ha, ?_⟩
  cases hrun : runDirect D s0 ts with
  | error e => rw [hrun] at hr; cases hr
  | ok t =>
    rw [hrun] at hr
    rw [← runDirect_correct D s0 ts t hrun]
    exact hr

theorem checkReach_reachable {D : DesignCert} {s0 : RuntimeState} {c : Ckpt}
    {adm : Tick → Bool} {ts : List Tick} (h : checkReach D s0 c adm ts = true) :
    Reachable D s0 adm (Matches D c) :=
  (checkReach_sound h).reachable

/-! ## Unreachability by an inductive invariant -/

/-- The final state of a trace is one more step from the final state of its prefix. -/
theorem refTrace_cons_final (D : DesignCert) (s : RuntimeState) (tk : Tick) (ts : List Tick) :
    (refTrace D s (tk :: ts)).finalState =
      (refTrace D (interpretDesign D tk.edges tk.input s).nextState ts).finalState := rfl

/-- An invariant that holds at `s0`, is kept by every admitted step, and excludes `T` proves
that no admitted run from `s0` reaches `T`. -/
theorem not_reachable_of_inv {D : DesignCert} {s0 : RuntimeState} {adm : Tick → Bool}
    {T : RuntimeState → Prop} (Inv : RuntimeState → Prop) (h0 : Inv s0)
    (hstep : ∀ s tk, Inv s → adm tk = true →
      Inv (interpretDesign D tk.edges tk.input s).nextState)
    (hT : ∀ s, Inv s → ¬ T s) : ¬ Reachable D s0 adm T := by
  rintro ⟨ts, ha, hreach⟩
  have hinv : ∀ (ts : List Tick) (s : RuntimeState), Inv s → ts.all adm = true →
      Inv (refTrace D s ts).finalState := by
    intro ts
    induction ts with
    | nil => intro s hs _; exact hs
    | cons tk ts ih =>
      intro s hs ha
      simp only [List.all_cons, Bool.and_eq_true] at ha
      rw [refTrace_cons_final]
      exact ih _ (hstep s tk hs ha.1) ha.2
  exact hT _ (hinv ts s0 h0 ha) hreach

#audit_axioms HDLProof.Reach.checkReach_sound
#audit_axioms HDLProof.Reach.not_reachable_of_inv
#audit_axioms HDLProof.Reach.matches_flop
#audit_axioms HDLProof.Reach.matches_word

end Reach
end HDLProof
