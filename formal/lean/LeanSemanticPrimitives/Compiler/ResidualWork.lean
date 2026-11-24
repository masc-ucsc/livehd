/-
# vc's event-driven pass, skipping chunks with nothing pending

The event-driven pass (`ResidualFlat.evLoopF`) visits every binding's pending mark, one by one.
On the census certificates a step recomputes 1-3% of the bindings, and they cluster: on
SoomRV's SoC 4% of the chunks of 16 bindings hold one. The visit itself is most of the
remaining loop cost (2.8 of about 8 ms per step on SoC).

`evEnvFW` keeps one more mark per chunk of 16 bindings, set whenever a binding in the
chunk becomes pending, and at the start of a chunk whose mark is clear it jumps to the next
chunk: a chunk with nothing pending costs one test instead of sixteen.

`evEnvFW_eq` proves it equal to `evEnvF` for every program, fanout table, environment, input
and state, with no hypothesis: a binding never becomes pending without its chunk's mark
(`ChunkInv`), so a skipped chunk holds no pending binding, and `evLoopF` would leave it as it
is. `@[csimp] evEnvF_eq_work` makes compiled code that calls `evEnvF` run `evEnvFW`; every
theorem stays about `evEnvF`.

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.ResidualFlat

namespace Compiler
namespace ResidualFlat

open Residual ResidualOpt ResidualInc

/-- Mark the chunk of every binding that reads slot `r` (per `fan`). -/
@[inline] def markChunks (fan : Array (Array Nat)) (r : Nat) (cflag : Array Bool) : Array Bool :=
  (fan.getD r #[]).foldl (fun c k => c.set! (k / 16) true) cflag

/-- `evSourcesF`, marking the readers' chunks as well. -/
def evSourcesFW (R : ResidualProgram) (fan : Array (Array Nat)) (i : RuntimeInput)
    (s : RuntimeStateF) (j : Nat) (cur : SlotEnvF) (pend cflag : Array Bool) :
    SlotEnvF × Array Bool × Array Bool :=
  if h : j < R.sources.size then
    match R.sources[j] with
    | .const _ _ => evSourcesFW R fan i s (j + 1) cur pend cflag
    | sd =>
      let v := sourceValueF i s sd
      if sameValF v (denoteRefF cur j) then evSourcesFW R fan i s (j + 1) cur pend cflag
      else evSourcesFW R fan i s (j + 1) (cur.set! j v) (markFan fan j pend) (markChunks fan j cflag)
  else (cur, pend, cflag)
termination_by R.sources.size - j

/-- `evLoopF`, jumping over a chunk whose mark is clear. -/
def evLoopFW (R : ResidualProgram) (fan : Array (Array Nat)) (k : Nat) (cur : SlotEnvF)
    (pend cflag : Array Bool) : SlotEnvF :=
  if h : k < R.bindings.size then
    if k % 16 == 0 && !(cflag.getD (k / 16) true) then
      evLoopFW R fan (k + 16) cur pend cflag
    else if pend.getD k true then
      let v := denoteExprF cur R.bindings[k].rhs
      if sameValF v (denoteRefF cur (R.sources.size + k)) then evLoopFW R fan (k + 1) cur pend cflag
      else evLoopFW R fan (k + 1) (cur.set! (R.sources.size + k) v)
        (markFan fan (R.sources.size + k) pend) (markChunks fan (R.sources.size + k) cflag)
    else evLoopFW R fan (k + 1) cur pend cflag
  else cur
termination_by R.bindings.size - k
decreasing_by all_goals omega

/-- `evEnvF`, skipping chunks with nothing pending. -/
def evEnvFW (R : ResidualProgram) (fan : Array (Array Nat)) (prev : SlotEnvF) (i : RuntimeInput)
    (s : RuntimeStateF) : SlotEnvF :=
  let p := evSourcesFW R fan i s 0 prev (Array.replicate R.bindings.size false)
    (Array.replicate ((R.bindings.size + 16 - 1) / 16) false)
  evLoopFW R fan 0 p.1 p.2.1 p.2.2

/-! ## Every pending binding's chunk is marked -/

/-- Every pending binding below `B` has its chunk's mark set. -/
def ChunkInv (B : Nat) (pend cflag : Array Bool) : Prop :=
  ∀ k, k < B → pend[k]? = some true → cflag[k / 16]? = some true

theorem markList_true : ∀ (l : List Nat) (pend : Array Bool) (k : Nat),
    (l.foldl (fun p k => p.set! k true) pend)[k]? = some true →
      pend[k]? = some true ∨ k ∈ l
  | [], _, _, h => Or.inl h
  | x :: l, pend, k, h => by
    simp only [List.foldl_cons] at h
    rcases markList_true l _ k h with h1 | h1
    · simp only [Array.set!_eq_setIfInBounds, Array.getElem?_setIfInBounds] at h1
      split at h1
      · rename_i hx; exact Or.inr (hx ▸ List.mem_cons_self)
      · exact Or.inl h1
    · exact Or.inr (List.mem_cons_of_mem _ h1)

theorem chunkList_spec : ∀ (l : List Nat) (cflag : Array Bool),
    (l.foldl (fun c k => c.set! (k / 16) true) cflag).size = cflag.size ∧
    (∀ q : Nat, cflag[q]? = some true → (l.foldl (fun c k => c.set! (k / 16) true) cflag)[q]? = some true) ∧
    (∀ k ∈ l, k / 16 < cflag.size →
      (l.foldl (fun c k => c.set! (k / 16) true) cflag)[k / 16]? = some true)
  | [], cflag => ⟨rfl, fun _ h => h, fun _ h => absurd h (by simp)⟩
  | x :: l, cflag => by
    obtain ⟨hs, hm, hk⟩ := chunkList_spec l (cflag.set! (x / 16) true)
    simp only [List.foldl_cons, Array.set!_eq_setIfInBounds, Array.size_setIfInBounds] at hs hm hk ⊢
    refine ⟨hs, ?_, ?_⟩
    · intro q hq
      apply hm
      rw [Array.getElem?_setIfInBounds]
      split
      · rename_i hxq
        have : q < cflag.size := by
          rcases Nat.lt_or_ge q cflag.size with h | h
          · exact h
          · rw [Array.getElem?_eq_none h] at hq; simp at hq
        rw [ite_eq_left (hxq ▸ this)]
      · exact hq
    · intro k hkl hlt
      rcases List.mem_cons.mp hkl with h | h
      · subst h
        apply hm
        rw [Array.getElem?_setIfInBounds, ite_eq_left rfl, ite_eq_left hlt]
      · exact hk k h hlt

theorem markFan_size' (fan : Array (Array Nat)) (r : Nat) (pend : Array Bool) :
    (markFan fan r pend).size = pend.size := markFan_size fan r pend

theorem markChunks_size (fan : Array (Array Nat)) (r : Nat) (cflag : Array Bool) :
    (markChunks fan r cflag).size = cflag.size := by
  unfold markChunks; rw [← Array.foldl_toList]; exact (chunkList_spec _ cflag).1

/-- Marking a slot's readers and their chunks keeps every pending binding's chunk marked. -/
theorem chunkInv_mark {B : Nat} {pend cflag : Array Bool} (fan : Array (Array Nat)) (r : Nat)
    (hc : cflag.size = (B + 16 - 1) / 16) (h : ChunkInv B pend cflag) :
    ChunkInv B (markFan fan r pend) (markChunks fan r cflag) := by
  intro k hk hp
  unfold markFan at hp
  unfold markChunks
  rw [← Array.foldl_toList] at hp ⊢
  obtain ⟨_, hm, hl⟩ := chunkList_spec (fan.getD r #[]).toList cflag
  rcases markList_true _ pend k hp with h1 | h1
  · exact hm _ (h k hk h1)
  · apply hl k h1
    rw [hc]; omega

/-! ## The chunked pass is the pass -/

/-- A run of bindings none of which is pending changes nothing. -/
theorem evLoopF_skip (R : ResidualProgram) (fan : Array (Array Nat)) (cur : SlotEnvF)
    (pend : Array Bool) : ∀ (m k : Nat),
      (∀ k', k ≤ k' → k' < k + m → k' < R.bindings.size → pend.getD k' true = false) →
      evLoopF R fan k cur pend = evLoopF R fan (k + m) cur pend
  | 0, k, _ => rfl
  | m + 1, k, h => by
    by_cases hk : k < R.bindings.size
    · have hp : pend.getD k true = false := h k (Nat.le_refl _) (by omega) hk
      conv => lhs; rw [evLoopF, dite_eq_left hk]
      rw [ite_eq_right (by simp [hp])]
      rw [evLoopF_skip R fan cur pend m (k + 1) (fun k' h1 h2 h3 => h k' (by omega) (by omega) h3)]
      congr 1; omega
    · rw [evLoopF, dite_eq_right hk, evLoopF, dite_eq_right (by omega)]

theorem evLoopFW_eq (R : ResidualProgram) (fan : Array (Array Nat)) :
    ∀ (n k : Nat) (cur : SlotEnvF) (pend cflag : Array Bool), R.bindings.size - k ≤ n →
      pend.size = R.bindings.size →
      cflag.size = (R.bindings.size + 16 - 1) / 16 →
      ChunkInv R.bindings.size pend cflag →
      evLoopFW R fan k cur pend cflag = evLoopF R fan k cur pend := by
  intro n
  induction n with
  | zero =>
    intro k cur pend cflag hn hp hc hinv
    rw [evLoopFW, dite_eq_right (by omega), evLoopF, dite_eq_right (by omega)]
  | succ m ih =>
    intro k cur pend cflag hn hp hc hinv
    by_cases hk : k < R.bindings.size
    · rw [evLoopFW, dite_eq_left hk]
      by_cases hskip : (k % 16 == 0 && !(cflag.getD (k / 16) true)) = true
      · -- a chunk with nothing pending: jump over it
        rw [ite_eq_left hskip]
        simp only [Bool.and_eq_true, beq_iff_eq, Bool.not_eq_true'] at hskip
        obtain ⟨hmod, hflag⟩ := hskip
        have hq : cflag[k / 16]? = some false := (getD_false hflag).2
        have hnone : ∀ k', k ≤ k' → k' < k + 16 → k' < R.bindings.size →
            pend.getD k' true = false := by
          intro k' h1 h2 h3
          have hdiv : k' / 16 = k / 16 := by omega
          rw [Array.getD_eq_getD_getElem?, Array.getElem?_eq_getElem (by omega)]
          cases hv : pend[k'] with
          | false => rfl
          | true =>
            have := hinv k' h3 (by rw [Array.getElem?_eq_getElem (by omega), hv])
            rw [hdiv, hq] at this; simp at this
        rw [evLoopF_skip R fan cur pend 16 k hnone]
        exact ih _ _ _ _ (by omega) hp hc hinv
      · rw [ite_eq_right hskip]
        rw [evLoopF, dite_eq_left hk]
        split
        · dsimp only
          split
          · exact ih _ _ _ _ (by omega) hp hc hinv
          · exact ih _ _ _ _ (by omega) (by rw [markFan_size, hp]) (by rw [markChunks_size, hc])
              (chunkInv_mark fan _ hc hinv)
        · exact ih _ _ _ _ (by omega) hp hc hinv
    · rw [evLoopFW, dite_eq_right hk, evLoopF, dite_eq_right hk]

theorem evSourcesFW_eq (R : ResidualProgram) (fan : Array (Array Nat)) (i : RuntimeInput)
    (s : RuntimeStateF) :
    ∀ (n j : Nat) (cur : SlotEnvF) (pend cflag : Array Bool), R.sources.size - j = n →
      pend.size = R.bindings.size →
      cflag.size = (R.bindings.size + 16 - 1) / 16 →
      ChunkInv R.bindings.size pend cflag →
      ((evSourcesFW R fan i s j cur pend cflag).1, (evSourcesFW R fan i s j cur pend cflag).2.1)
          = evSourcesF R fan i s j cur pend ∧
        (evSourcesFW R fan i s j cur pend cflag).2.1.size = R.bindings.size ∧
        (evSourcesFW R fan i s j cur pend cflag).2.2.size = (R.bindings.size + 16 - 1) / 16 ∧
        ChunkInv R.bindings.size (evSourcesFW R fan i s j cur pend cflag).2.1
          (evSourcesFW R fan i s j cur pend cflag).2.2 := by
  intro n
  induction n with
  | zero =>
    intro j cur pend cflag hn hp hc hinv
    rw [evSourcesFW, evSourcesF, dite_eq_right (by omega), dite_eq_right (by omega)]
    exact ⟨rfl, hp, hc, hinv⟩
  | succ n ih =>
    intro j cur pend cflag hn hp hc hinv
    have hj : j < R.sources.size := by omega
    rw [evSourcesFW, evSourcesF, dite_eq_left hj, dite_eq_left hj]
    generalize R.sources[j] = sd
    cases sd with
    | const w v => exact ih (j + 1) cur pend cflag (by omega) hp hc hinv
    | _ =>
      dsimp only
      split
      · exact ih (j + 1) cur pend cflag (by omega) hp hc hinv
      · exact ih (j + 1) _ _ _ (by omega) (by rw [markFan_size, hp]) (by rw [markChunks_size, hc])
          (chunkInv_mark fan _ hc hinv)

/-- **The chunked pass is the event-driven pass**, for every program, table, environment,
input and state. -/
theorem evEnvFW_eq (R : ResidualProgram) (fan : Array (Array Nat)) (prev : SlotEnvF)
    (i : RuntimeInput) (s : RuntimeStateF) : evEnvFW R fan prev i s = evEnvF R fan prev i s := by
  have h0 : ChunkInv R.bindings.size (Array.replicate R.bindings.size false)
      (Array.replicate ((R.bindings.size + 16 - 1) / 16) false) := by
    intro k hk hp
    rw [Array.getElem?_replicate, ite_eq_left hk] at hp; simp at hp
  obtain ⟨hsrc, hps, hcs, hinv⟩ := evSourcesFW_eq R fan i s _ 0 prev
    (Array.replicate R.bindings.size false)
    (Array.replicate ((R.bindings.size + 16 - 1) / 16) false) rfl (by simp) (by simp) h0
  simp only [evEnvFW, evEnvF]
  rw [evLoopFW_eq R fan _ 0 _ _ _ (Nat.le_refl _) hps hcs hinv, ← hsrc]

@[csimp] theorem evEnvF_eq_work : @evEnvF = @evEnvFW := by
  funext R fan prev i s
  exact (evEnvFW_eq R fan prev i s).symm

end ResidualFlat
end Compiler

#print axioms Compiler.ResidualFlat.evEnvFW_eq
