/-
# vc's incremental step, event-driven: visit only the bindings an operand of which changed

`ResidualInc.incEnv` decides, binding by binding, whether any operand changed: it visits every
binding and reads every operand's mark, even on a cycle where almost nothing changed (on the
census certificates that visit is most of a step: 14 of 19 ms on SoomRV's SoC). `evEnv` turns
it around: a slot that changes marks the bindings that read it (`fanout`, built once per
program) as pending, and the pass recomputes only pending bindings; any other binding keeps
last step's value, as in `incEnv`.

`evEnv_correct`: when every binding reads only earlier slots (`wellOrdered`), every reader of a
slot is in that slot's list (`FanOK`; `fanout_ok` proves it of `fanout`), and the previous
environment is the full environment of some earlier step, `evEnv` gives exactly this step's
full environment (`fullEnv`), the one `denoteResidual` evaluates. So `evStep` is
`denoteResidual` (`evStep_correct`), and a run is `ResidualInc.runFull`'s
(`runEv_eq_runFull`). The proof is `ResidualInc`'s: the pass ends in a fixed point (`IsFull`),
which is unique.

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.ResidualInc

namespace Compiler
namespace ResidualInc

open Residual ResidualOpt

/-! ## Definitions -/

/-- Add binding `k` to slot `r`'s list. -/
@[inline] def fanPush (k : Nat) (fan : Array (Array Nat)) (r : Nat) : Array (Array Nat) :=
  fan.modify r (·.push k)

/-- Add binding `k` to the list of every slot it reads. -/
def fanAdd (R : ResidualProgram) (fan : Array (Array Nat)) (k : Nat) : Array (Array Nat) :=
  match R.bindings[k]? with
  | some b => (exprRefs b.rhs).foldl (fanPush k) fan
  | none => fan

/-- For each slot, the bindings that read it, in increasing order (a binding that reads a slot
twice is listed twice). Built once per program. -/
def fanout (R : ResidualProgram) : Array (Array Nat) :=
  (List.range R.bindings.size).foldl (fanAdd R)
    (Array.replicate (R.sources.size + R.bindings.size) #[])

/-- Mark every binding that reads slot `r` (per `fan`) pending. -/
@[inline] def markFan (fan : Array (Array Nat)) (r : Nat) (pend : Array Bool) : Array Bool :=
  (fan.getD r #[]).foldl (fun p k => p.set! k true) pend

/-- Sources `j ..< S`: a constant keeps its slot; any other takes this step's value, and when
that differs from the slot's, its readers become pending. -/
def evSources (R : ResidualProgram) (fan : Array (Array Nat)) (i : RuntimeInput)
    (s : RuntimeState) (j : Nat) (cur : SlotEnv) (pend : Array Bool) : SlotEnv × Array Bool :=
  if h : j < R.sources.size then
    match R.sources[j] with
    | .const _ _ => evSources R fan i s (j + 1) cur pend
    | sd =>
      let v := sourceValue i s sd
      if sameVal v (denoteRef cur j) then evSources R fan i s (j + 1) cur pend
      else evSources R fan i s (j + 1) (cur.set! j v) (markFan fan j pend)
  else (cur, pend)
termination_by R.sources.size - j

/-- Bindings `k ..< B`: one not pending keeps its slot; a pending one is recomputed, and when its
value differs from the slot's, its readers become pending. -/
def evLoop (R : ResidualProgram) (fan : Array (Array Nat)) (k : Nat) (cur : SlotEnv)
    (pend : Array Bool) : SlotEnv :=
  if h : k < R.bindings.size then
    if pend.getD k true then
      let v := denoteExpr cur R.bindings[k].rhs
      if sameVal v (denoteRef cur (R.sources.size + k)) then evLoop R fan (k + 1) cur pend
      else evLoop R fan (k + 1) (cur.set! (R.sources.size + k) v)
        (markFan fan (R.sources.size + k) pend)
    else evLoop R fan (k + 1) cur pend
  else cur
termination_by R.bindings.size - k

/-- This step's environment from last step's (`prev`, updated in place). -/
def evEnv (R : ResidualProgram) (fan : Array (Array Nat)) (prev : SlotEnv) (i : RuntimeInput)
    (s : RuntimeState) : SlotEnv :=
  let p := evSources R fan i s 0 prev (Array.replicate R.bindings.size false)
  evLoop R fan 0 p.1 p.2

/-- One event-driven step: the result, and this step's environment for the next. -/
def evStep (R : ResidualProgram) (fan : Array (Array Nat)) (prev : SlotEnv) (e : ClockEdges)
    (i : RuntimeInput) (s : RuntimeState) : RuntimeResult × SlotEnv :=
  let env := evEnv R fan prev i s
  (stepOfEnv R env e s, env)

/-! ## The fanout table lists every reader -/

/-- Every binding that reads slot `r` is in `r`'s list. -/
def FanOK (R : ResidualProgram) (fan : Array (Array Nat)) : Prop :=
  ∀ k (hk : k < R.bindings.size), ∀ r ∈ exprRefs R.bindings[k].rhs,
    r < R.sources.size + R.bindings.size → k ∈ fan.getD r #[]

theorem fanPush_size (k : Nat) (fan : Array (Array Nat)) (r : Nat) :
    (fanPush k fan r).size = fan.size := by
  simp [fanPush]

theorem fanPush_mono (k : Nat) (fan : Array (Array Nat)) (r : Nat) {x r' : Nat}
    (h : x ∈ fan.getD r' #[]) : x ∈ (fanPush k fan r).getD r' #[] := by
  simp only [fanPush, Array.getD_eq_getD_getElem?, Array.getElem?_modify] at h ⊢
  split
  · cases hr : fan[r']? with
    | none => rw [hr] at h; simp at h
    | some a => rw [hr] at h; simp only [Option.map_some, Option.getD_some] at h ⊢; exact Array.mem_push_of_mem _ h
  · exact h

theorem fanPush_self (k : Nat) (fan : Array (Array Nat)) {r : Nat} (hr : r < fan.size) :
    k ∈ (fanPush k fan r).getD r #[] := by
  simp only [fanPush, Array.getD_eq_getD_getElem?, Array.getElem?_modify, ite_true,
    Array.getElem?_eq_getElem hr, Option.map_some, Option.getD_some]
  exact Array.mem_push_self

theorem fanPush_fold (k : Nat) : ∀ (l : List Nat) (fan : Array (Array Nat)),
    (l.foldl (fanPush k) fan).size = fan.size ∧
    (∀ x r', x ∈ fan.getD r' #[] → x ∈ (l.foldl (fanPush k) fan).getD r' #[]) ∧
    (∀ r ∈ l, r < fan.size → k ∈ (l.foldl (fanPush k) fan).getD r #[])
  | [], fan => ⟨rfl, fun _ _ h => h, fun _ h => absurd h (by simp)⟩
  | r :: l, fan => by
    obtain ⟨hs, hm, hk⟩ := fanPush_fold k l (fanPush k fan r)
    rw [fanPush_size] at hs
    refine ⟨hs, fun x r' h => hm x r' (fanPush_mono k fan r h), ?_⟩
    intro r' hr' hlt
    simp only [List.foldl_cons]
    rcases List.mem_cons.mp hr' with h | h
    · subst h; exact hm k r' (fanPush_self k fan hlt)
    · exact hk r' h (by rw [fanPush_size]; exact hlt)

theorem fanAdd_spec (R : ResidualProgram) : ∀ (L : List Nat) (fan : Array (Array Nat)),
    (L.foldl (fanAdd R) fan).size = fan.size ∧
    (∀ x r', x ∈ fan.getD r' #[] → x ∈ (L.foldl (fanAdd R) fan).getD r' #[]) ∧
    (∀ k ∈ L, ∀ (hk : k < R.bindings.size), ∀ r ∈ exprRefs R.bindings[k].rhs,
      r < fan.size → k ∈ (L.foldl (fanAdd R) fan).getD r #[])
  | [], fan => ⟨rfl, fun _ _ h => h, fun _ h => absurd h (by simp)⟩
  | k :: L, fan => by
    have h1 : (fanAdd R fan k).size = fan.size ∧
        (∀ x r', x ∈ fan.getD r' #[] → x ∈ (fanAdd R fan k).getD r' #[]) ∧
        (∀ (hk : k < R.bindings.size), ∀ r ∈ exprRefs R.bindings[k].rhs,
          r < fan.size → k ∈ (fanAdd R fan k).getD r #[]) := by
      unfold fanAdd
      split
      · rename_i b hb
        obtain ⟨hs, hm, hk⟩ := fanPush_fold k (exprRefs b.rhs) fan
        refine ⟨hs, hm, ?_⟩
        intro hkB r hr hlt
        have : R.bindings[k] = b := by
          rw [Array.getElem?_eq_getElem hkB] at hb; exact Option.some.inj hb
        rw [this] at hr
        exact hk r hr hlt
      · rename_i hb
        refine ⟨rfl, fun _ _ h => h, ?_⟩
        intro hkB
        rw [Array.getElem?_eq_getElem hkB] at hb; simp at hb
    obtain ⟨hs1, hm1, hk1⟩ := h1
    obtain ⟨hs, hm, hk⟩ := fanAdd_spec R L (fanAdd R fan k)
    rw [hs1] at hs
    simp only [List.foldl_cons]
    refine ⟨hs, fun x r' h => hm x r' (hm1 x r' h), ?_⟩
    intro k' hk' hkB r hr hlt
    rcases List.mem_cons.mp hk' with h | h
    · subst h; exact hm _ _ (hk1 hkB r hr hlt)
    · exact hk k' h hkB r hr (by rw [hs1]; exact hlt)

/-- **`fanout` lists every reader of every slot.** -/
theorem fanout_ok (R : ResidualProgram) : FanOK R (fanout R) := by
  intro k hk r hr hlt
  obtain ⟨_, _, h⟩ := fanAdd_spec R (List.range R.bindings.size)
    (Array.replicate (R.sources.size + R.bindings.size) #[])
  exact h k (List.mem_range.mpr hk) hk r hr (by simpa using hlt)

/-! ## Marking -/

theorem markList_spec : ∀ (l : List Nat) (pend : Array Bool),
    (l.foldl (fun p k => p.set! k true) pend).size = pend.size ∧
    ∀ k', (l.foldl (fun p k => p.set! k true) pend)[k']? = some false →
      pend[k']? = some false ∧ k' ∉ l
  | [], pend => ⟨rfl, fun _ h => ⟨h, by simp⟩⟩
  | k :: l, pend => by
    obtain ⟨hs, hf⟩ := markList_spec l (pend.set! k true)
    simp only [List.foldl_cons]
    refine ⟨by rw [hs]; simp, ?_⟩
    intro k' hk'
    obtain ⟨h1, h2⟩ := hf k' hk'
    simp only [Array.set!_eq_setIfInBounds, Array.getElem?_setIfInBounds] at h1
    split at h1
    · split at h1 <;> simp at h1
    · rename_i hne
      exact ⟨h1, by simp only [List.mem_cons, not_or]; exact ⟨fun h => hne h.symm, h2⟩⟩

theorem markFan_size (fan : Array (Array Nat)) (r : Nat) (pend : Array Bool) :
    (markFan fan r pend).size = pend.size := by
  unfold markFan
  rw [← Array.foldl_toList]
  exact (markList_spec _ pend).1

/-- A binding `markFan` leaves unmarked was unmarked, and does not read slot `r` per `fan`. -/
theorem markFan_false {fan : Array (Array Nat)} {r : Nat} {pend : Array Bool} {k' : Nat}
    (h : (markFan fan r pend)[k']? = some false) :
    pend[k']? = some false ∧ k' ∉ fan.getD r #[] := by
  unfold markFan at h
  rw [← Array.foldl_toList] at h
  obtain ⟨h1, h2⟩ := (markList_spec _ pend).2 k' h
  exact ⟨h1, fun hm => h2 (Array.mem_def.mp hm)⟩

/-! ## The event-driven pass ends in the full environment -/

section
variable (R : ResidualProgram) (fan : Array (Array Nat)) (i : RuntimeInput) (s : RuntimeState)
  (prev : SlotEnv)

/-- After the sources below `j`: this step's values there, last step's above, and every binding
not pending reads, below `j`, only slots holding last step's values. -/
structure SrcInvE (j : Nat) (cur : SlotEnv) (pend : Array Bool) : Prop where
  size  : cur.size = prev.size
  psize : pend.size = R.bindings.size
  done  : ∀ j', j' < j → denoteRef cur j' = denoteRef (sourceEnvArr R.sources i s) j'
  rest  : ∀ r, j ≤ r → denoteRef cur r = denoteRef prev r
  clean : ∀ k' (hk' : k' < R.bindings.size), pend[k']? = some false →
            ∀ r ∈ exprRefs R.bindings[k'].rhs, r < j → denoteRef cur r = denoteRef prev r

/-- After the bindings below `k`: the sources and those bindings hold this step's values (each
binding its own expression), the rest last step's, and every binding from `k` on that is not
pending reads, below `S + k`, only slots holding last step's values. -/
structure LoopInvE (k : Nat) (cur : SlotEnv) (pend : Array Bool) : Prop where
  size  : cur.size = R.sources.size + R.bindings.size
  psize : pend.size = R.bindings.size
  srcs  : ∀ j, j < R.sources.size → denoteRef cur j = denoteRef (sourceEnvArr R.sources i s) j
  done  : ∀ k' (hk' : k' < R.bindings.size), k' < k →
            denoteRef cur (R.sources.size + k') = denoteExpr cur R.bindings[k'].rhs
  rest  : ∀ r, R.sources.size + k ≤ r → denoteRef cur r = denoteRef prev r
  clean : ∀ k' (hk' : k' < R.bindings.size), k ≤ k' → pend[k']? = some false →
            ∀ r ∈ exprRefs R.bindings[k'].rhs, r < R.sources.size + k →
              denoteRef cur r = denoteRef prev r

theorem evSources_spec {i' : RuntimeInput} {s' : RuntimeState}
    (hp : IsFull R (sourceEnvArr R.sources i' s') prev)
    (hpS : prev.size = R.sources.size + R.bindings.size) (hfan : FanOK R fan) :
    ∀ (n j : Nat) (cur : SlotEnv) (pend : Array Bool), R.sources.size - j = n →
      j ≤ R.sources.size → SrcInvE R i s prev j cur pend →
      SrcInvE R i s prev R.sources.size (evSources R fan i s j cur pend).1
        (evSources R fan i s j cur pend).2 := by
  intro n
  induction n with
  | zero =>
    intro j cur pend hn hj hinv
    have hjS : j = R.sources.size := by omega
    rw [evSources, dite_eq_right (by omega)]
    subst hjS; exact hinv
  | succ n ih =>
    intro j cur pend hn hj hinv
    have hjS : j < R.sources.size := by omega
    rw [evSources, dite_eq_left hjS]
    split
    · -- a constant source keeps its slot
      rename_i w v hc
      apply ih (j + 1) cur pend (by omega) (by omega)
      exact {
        size := hinv.size
        psize := hinv.psize
        done := by
          intro j' hj'
          rcases Nat.lt_or_ge j' j with h | h
          · exact hinv.done j' h
          · have : j' = j := by omega
            subst this
            rw [hinv.rest j' (Nat.le_refl _), hp.srcs j' hjS, denoteRef_sourceEnvArr _ _ _ hjS,
              denoteRef_sourceEnvArr _ _ _ hjS, hc]
            rfl
        rest := fun r hr => hinv.rest r (by omega)
        clean := by
          intro k' hk' hpk r hr hrj
          rcases Nat.lt_or_ge r j with h | h
          · exact hinv.clean k' hk' hpk r hr h
          · exact hinv.rest r h }
    · -- any other source takes this step's value
      rename_i hnc
      have hj' : j < cur.size := by rw [hinv.size, hpS]; omega
      dsimp only
      split
      · -- the same value: nothing changes
        rename_i hsame
        apply ih (j + 1) cur pend (by omega) (by omega)
        exact {
          size := hinv.size
          psize := hinv.psize
          done := by
            intro j' hj'
            rcases Nat.lt_or_ge j' j with h | h
            · exact hinv.done j' h
            · have : j' = j := by omega
              subst this
              rw [denoteRef_sourceEnvArr _ _ _ hjS, ← sameVal_eq hsame]
          rest := fun r hr => hinv.rest r (by omega)
          clean := by
            intro k' hk' hpk r hr hrj
            rcases Nat.lt_or_ge r j with h | h
            · exact hinv.clean k' hk' hpk r hr h
            · exact hinv.rest r h }
      · -- a new value: its readers become pending
        apply ih (j + 1) _ _ (by omega) (by omega)
        exact {
          size := by simp [hinv.size]
          psize := by rw [markFan_size, hinv.psize]
          done := by
            intro j' hjj
            rcases Nat.lt_or_ge j' j with h | h
            · rw [denoteRef_set_ne (by omega)]; exact hinv.done j' h
            · have : j' = j := by omega
              subst this
              rw [denoteRef_set_self hj', denoteRef_sourceEnvArr _ _ _ hjS]
          rest := by
            intro r hr
            rw [denoteRef_set_ne (by omega)]; exact hinv.rest r (by omega)
          clean := by
            intro k' hk' hpk r hr hrj
            obtain ⟨hpk0, hnot⟩ := markFan_false hpk
            rcases Nat.lt_or_ge r j with h | h
            · rw [denoteRef_set_ne (by omega)]; exact hinv.clean k' hk' hpk0 r hr h
            · have : r = j := by omega
              subst this
              exact absurd (hfan k' hk' r hr (by omega)) hnot }

theorem evLoop_spec {i' : RuntimeInput} {s' : RuntimeState} (hw : wellOrdered R = true)
    (hp : IsFull R (sourceEnvArr R.sources i' s') prev) (hfan : FanOK R fan) :
    ∀ (n k : Nat) (cur : SlotEnv) (pend : Array Bool), R.bindings.size - k = n →
      k ≤ R.bindings.size → LoopInvE R i s prev k cur pend →
      IsFull R (sourceEnvArr R.sources i s) (evLoop R fan k cur pend) := by
  intro n
  induction n with
  | zero =>
    intro k cur pend hn hk hinv
    have hkB : k = R.bindings.size := by omega
    rw [evLoop, dite_eq_right (by omega)]
    exact {
      size := hinv.size
      srcs := hinv.srcs
      bind := fun k' hk' => hinv.done k' hk' (by omega) }
  | succ n ih =>
    intro k cur pend hn hk hinv
    have hkB : k < R.bindings.size := by omega
    have hslot : R.sources.size + k < cur.size := by rw [hinv.size]; omega
    have hne : ∀ r ∈ exprRefs R.bindings[k].rhs, r ≠ R.sources.size + k := by
      intro r hr; have := wellOrdered_spec hw hkB r hr; omega
    rw [evLoop, dite_eq_left hkB]
    split
    · -- pending: recompute
      dsimp only
      split
      · -- the same value: nothing changes
        rename_i _ hsame
        apply ih (k + 1) cur pend (by omega) (by omega)
        exact {
          size := hinv.size
          psize := hinv.psize
          srcs := hinv.srcs
          done := by
            intro k' hk' hkk
            rcases Nat.lt_or_ge k' k with h | h
            · exact hinv.done k' hk' h
            · have : k' = k := by omega
              subst this
              exact (sameVal_eq hsame).symm
          rest := fun r hr => hinv.rest r (by omega)
          clean := by
            intro k' hk' hkk hpk r hr hrk
            rcases Nat.lt_or_ge r (R.sources.size + k) with h | h
            · exact hinv.clean k' hk' (by omega) hpk r hr h
            · exact hinv.rest r h }
      · -- a new value: its readers become pending
        apply ih (k + 1) _ _ (by omega) (by omega)
        exact {
          size := by simp [hinv.size]
          psize := by rw [markFan_size, hinv.psize]
          srcs := by
            intro j hj
            rw [denoteRef_set_ne (by omega)]; exact hinv.srcs j hj
          done := by
            intro k' hk' hkk
            rcases Nat.lt_or_ge k' k with h | h
            · rw [denoteRef_set_ne (by omega), hinv.done k' hk' h]
              apply denoteExpr_congr
              intro r hr
              have := wellOrdered_spec hw hk' r hr
              exact (denoteRef_set_ne (by omega)).symm
            · have : k' = k := by omega
              subst this
              rw [denoteRef_set_self hslot]
              apply denoteExpr_congr
              intro r hr
              exact (denoteRef_set_ne (hne r hr)).symm
          rest := by
            intro r hr
            rw [denoteRef_set_ne (by omega)]; exact hinv.rest r (by omega)
          clean := by
            intro k' hk' hkk hpk r hr hrk
            obtain ⟨hpk0, hnot⟩ := markFan_false hpk
            rcases Nat.lt_or_ge r (R.sources.size + k) with h | h
            · rw [denoteRef_set_ne (by omega)]; exact hinv.clean k' hk' (by omega) hpk0 r hr h
            · have : r = R.sources.size + k := by omega
              subst this
              exact absurd (hfan k' hk' _ hr (by omega)) hnot }
    · -- not pending: every operand kept its value, so the binding keeps its own
      rename_i hpend
      obtain ⟨_, hpk⟩ := getD_false (Bool.eq_false_iff.mpr hpend)
      have hun : ∀ r ∈ exprRefs R.bindings[k].rhs, denoteRef prev r = denoteRef cur r := by
        intro r hr
        exact (hinv.clean k hkB (Nat.le_refl _) hpk r hr (wellOrdered_spec hw hkB r hr)).symm
      apply ih (k + 1) cur pend (by omega) (by omega)
      exact {
        size := hinv.size
        psize := hinv.psize
        srcs := hinv.srcs
        done := by
          intro k' hk' hkk
          rcases Nat.lt_or_ge k' k with h | h
          · exact hinv.done k' hk' h
          · have : k' = k := by omega
            subst this
            rw [hinv.rest _ (Nat.le_refl _), hp.bind k' hk']
            exact denoteExpr_congr _ hun
        rest := fun r hr => hinv.rest r (by omega)
        clean := by
          intro k' hk' hkk hpk' r hr hrk
          rcases Nat.lt_or_ge r (R.sources.size + k) with h | h
          · exact hinv.clean k' hk' (by omega) hpk' r hr h
          · exact hinv.rest r h }

end

/-- **The event-driven pass is the full step.** From the full environment of any earlier step,
with a fanout table that lists every reader, `evEnv` computes exactly this step's full
environment. -/
theorem evEnv_correct {R : ResidualProgram} {fan : Array (Array Nat)} (hw : wellOrdered R = true)
    (hfan : FanOK R fan) (i' : RuntimeInput) (s' : RuntimeState) (i : RuntimeInput)
    (s : RuntimeState) :
    evEnv R fan (fullEnv R i' s') i s = fullEnv R i s := by
  have hp := isFull_fullEnv hw i' s'
  have hpS := hp.size
  have h0 : SrcInvE R i s (fullEnv R i' s') 0 (fullEnv R i' s')
      (Array.replicate R.bindings.size false) := {
    size := rfl
    psize := by simp
    done := fun j' h => absurd h (by omega)
    rest := fun _ _ => rfl
    clean := fun _ _ _ _ _ h => absurd h (by omega) }
  have h1 := evSources_spec R fan i s (fullEnv R i' s') hp hpS hfan _ 0 _ _ rfl (by omega) h0
  have h2 : LoopInvE R i s (fullEnv R i' s') 0
      (evSources R fan i s 0 (fullEnv R i' s') (Array.replicate R.bindings.size false)).1
      (evSources R fan i s 0 (fullEnv R i' s') (Array.replicate R.bindings.size false)).2 := {
    size := by rw [h1.size, hpS]
    psize := h1.psize
    srcs := h1.done
    done := fun _ _ h => absurd h (by omega)
    rest := fun r hr => h1.rest r (by omega)
    clean := fun k' hk' _ hpk r hr hrk => h1.clean k' hk' hpk r hr (by omega) }
  have h3 := evLoop_spec R fan i s (fullEnv R i' s') hw hp hfan _ 0 _ _ rfl (by omega) h2
  exact isFull_unique hw h3 (isFull_fullEnv hw i s)

/-- **One event-driven step is `denoteResidual`**, and leaves this step's full environment for
the next. -/
theorem evStep_correct {R : ResidualProgram} {fan : Array (Array Nat)} (hw : wellOrdered R = true)
    (hfan : FanOK R fan) (i' : RuntimeInput) (s' : RuntimeState) (e : ClockEdges)
    (i : RuntimeInput) (s : RuntimeState) :
    evStep R fan (fullEnv R i' s') e i s = (denoteResidual R e i s, fullEnv R i s) := by
  simp only [evStep, evEnv_correct hw hfan, denoteResidual_eq_stepOfEnv]

/-! ## Runs -/

/-- Event-driven steps, each from the environment the one before left. -/
def runEvFrom (R : ResidualProgram) (fan : Array (Array Nat)) (env : SlotEnv) : RuntimeState →
    List (ClockEdges × RuntimeInput) → List RuntimeResult
  | _, [] => []
  | s, (e, i) :: rest =>
    let p := evStep R fan env e i s
    p.1 :: runEvFrom R fan p.2 p.1.nextState rest

/-- A run: the first step full, the others event-driven, with the program's fanout table. -/
def runEv (R : ResidualProgram) : RuntimeState → List (ClockEdges × RuntimeInput) →
    List RuntimeResult
  | _, [] => []
  | s, (e, i) :: rest =>
    let env := fullEnv R i s
    let r := stepOfEnv R env e s
    r :: runEvFrom R (fanout R) env r.nextState rest

theorem runEvFrom_eq {R : ResidualProgram} {fan : Array (Array Nat)} (hw : wellOrdered R = true)
    (hfan : FanOK R fan) :
    ∀ (ticks : List (ClockEdges × RuntimeInput)) (i' : RuntimeInput) (s' s : RuntimeState),
      runEvFrom R fan (fullEnv R i' s') s ticks = runFull R s ticks
  | [], _, _, _ => rfl
  | (e, i) :: rest, i', s', s => by
    simp only [runEvFrom, runFull, evStep_correct hw hfan]
    rw [runEvFrom_eq hw hfan rest i s]

/-- **An event-driven run is a run of `denoteResidual`**, step for step. -/
theorem runEv_eq_runFull {R : ResidualProgram} (hw : wellOrdered R = true)
    (s : RuntimeState) (ticks : List (ClockEdges × RuntimeInput)) :
    runEv R s ticks = runFull R s ticks := by
  cases ticks with
  | nil => rfl
  | cons t rest =>
    obtain ⟨e, i⟩ := t
    simp only [runEv, runFull, ← denoteResidual_eq_stepOfEnv]
    rw [runEvFrom_eq hw (fanout_ok R) rest i s]

end ResidualInc
end Compiler

#print axioms Compiler.ResidualInc.fanout_ok
#print axioms Compiler.ResidualInc.evEnv_correct
#print axioms Compiler.ResidualInc.runEv_eq_runFull
