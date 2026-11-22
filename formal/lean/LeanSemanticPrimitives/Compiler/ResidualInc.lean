/-
# vc's step, incremental: reuse last cycle's value of every binding whose operands did not change

`denoteResidual` builds the whole slot environment every step: every source, then every
binding in order. Between two steps of a real design almost nothing changes (on the census
certificates, 0.6-2.1% of the bindings change value per cycle, and 88-98% read only operands
that kept their value), so most of that work recomputes last cycle's values.

`incEnv R prev i s` takes LAST step's environment `prev` and updates it in place, in slot order:
- a constant source keeps its slot (its value never changes); every other source takes this
  step's value, and is marked changed when it differs from last step's;
- a binding none of whose operands is marked changed keeps last step's value; any other
  binding is recomputed, and marked changed when its value differs from last step's.

`incEnv_correct`: when every binding reads only earlier slots (`wellOrdered`, checked once) and
`prev` is the full environment of SOME earlier step, `incEnv` gives exactly this step's full
environment, `fullEnv R i s`, the one `denoteResidual` evaluates. So a run that starts with one
full step and then steps incrementally computes the same results as `denoteResidual` on every
step (`incStep_correct`, `runInc_eq_runFull`), and through `compileOpt_correct` the same as
`interpretDesign`.

The proof needs no knowledge of which values changed: "unchanged" is decided by `sameVal`, and
`sameVal a b = true` implies `a = b` (a memory image never compares equal, so a binding reading
one is always recomputed). A full environment is characterized by its fixed point (`IsFull`:
the sources, and every binding equal to its own expression), which is unique for a
well-ordered program; the incremental pass ends in such a fixed point.

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.ResidualFast
import LeanSemanticPrimitives.Compiler.ResidualOptSound

namespace Compiler
namespace ResidualInc

open Residual ResidualOpt

/-! ## Definitions -/

/-- Every binding reads only earlier slots. -/
def wellOrdered (R : ResidualProgram) : Bool :=
  (List.range R.bindings.size).all fun k =>
    match R.bindings[k]? with
    | some b => (exprRefs b.rhs).all (· < R.sources.size + k)
    | none => true

/-- The full environment of a step: the one `denoteResidual` evaluates. -/
def fullEnv (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeState) : SlotEnv :=
  R.bindings.foldl (fun env b => env.push (denoteExpr env b.rhs)) (sourceEnvArr R.sources i s)

/-- `denoteResidual`'s result, read from an environment. -/
def stepOfEnv (R : ResidualProgram) (env : SlotEnv) (e : ClockEdges) (s : RuntimeState) :
    RuntimeResult :=
  { outputs   := R.outputs.map fun o => bv_resize o.width (refBV env o.slot)
    nextState :=
      { flops := R.flopUpdates.mapIdx fun idx f => flopNext env e s idx f
        mems  := R.memoryUpdates.mapIdx fun idx m => memNext env e s idx m } }

/-- `stepOfEnv`, with the next memories stored by `FastBV.memsOfVals` (`ResidualFast`): each is
the function its slot holds, not a closure over `env`. So the result keeps no reference to the
environment, and the next incremental step can update it in place instead of copying it.
`@[csimp] stepOfEnv_eq_fast` makes `incStep` and every later caller run it. -/
def stepOfEnvFast (R : ResidualProgram) (env : SlotEnv) (e : ClockEdges) (s : RuntimeState) :
    RuntimeResult :=
  { outputs   := R.outputs.map fun o => bv_resize o.width (refBV env o.slot)
    nextState :=
      { flops := R.flopUpdates.mapIdx fun idx f => flopNext env e s idx f
        mems  := FastBV.memsOfVals (R.memoryUpdates.mapIdx fun idx m => memNextVal env e s idx m) } }

@[csimp] theorem stepOfEnv_eq_fast : @stepOfEnv = @stepOfEnvFast := by
  funext R env e s
  simp only [stepOfEnv, stepOfEnvFast, residualMems_eq]

/-- Equal values, decided cheaply; a memory image is never equal to anything. -/
@[inline] def sameVal : CertVal → CertVal → Bool
  | .bv x, .bv y => x.width == y.width && x.value == y.value
  | _, _ => false

/-- Some operand of `e` satisfies `p` (without building the operand list). -/
@[inline] def anyRef (p : Nat → Bool) : ResidualExpr → Bool
  | .rsum _ _ args | .rmult _ args | .rand _ args | .rorBits _ args | .rxor _ args
  | .rredOr _ args | .req _ args | .rshl _ args | .rmuxN _ args => args.any p
  | .rnot _ a => p a
  | .rult _ a b | .rugt _ a b | .rslt _ a b | .rsgt _ a b | .rsra _ a b | .rsext _ a b
  | .rgetMask _ a b | .rudiv _ a b | .rsdiv _ a b => p a || p b
  | .rsetMask _ a m v => p a || p m || p v
  | .rmux _ s f t => p s || p f || p t
  | .rmemRead _ m a en => p m || p a || p en
  | .rmemWrite m a d en => p m || p a || p d || p en
  | .rmemWriteBE _ _ m a d be => p m || p a || p d || p be
  | .rmemLoad _ _ m bus en => p m || p bus || p en
  | .rmemToBus _ _ m => p m

/-- Sources `j ..< S`: a constant keeps its slot; any other takes this step's value. `dirty`
marks the changed slots below `j`. -/
def incSources (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeState) (j : Nat)
    (cur : SlotEnv) (dirty : Array Bool) : SlotEnv × Array Bool :=
  if h : j < R.sources.size then
    match R.sources[j] with
    | .const _ _ => incSources R i s (j + 1) cur (dirty.push false)
    | sd =>
      let v := sourceValue i s sd
      let ch := !sameVal v (denoteRef cur j)
      incSources R i s (j + 1) (cur.set! j v) (dirty.push ch)
  else (cur, dirty)
termination_by R.sources.size - j

/-- Bindings `k ..< B`: one with no changed operand keeps its slot; any other is recomputed. -/
def incLoop (R : ResidualProgram) (k : Nat) (cur : SlotEnv) (dirty : Array Bool) : SlotEnv :=
  if h : k < R.bindings.size then
    let b := R.bindings[k]
    let slot := R.sources.size + k
    if anyRef (fun r => dirty.getD r true) b.rhs then
      let v := denoteExpr cur b.rhs
      let ch := !sameVal v (denoteRef cur slot)
      incLoop R (k + 1) (cur.set! slot v) (dirty.push ch)
    else
      incLoop R (k + 1) cur (dirty.push false)
  else cur
termination_by R.bindings.size - k

/-- This step's environment from last step's (`prev`, updated in place). -/
def incEnv (R : ResidualProgram) (prev : SlotEnv) (i : RuntimeInput) (s : RuntimeState) :
    SlotEnv :=
  let (c, d) := incSources R i s 0 prev (Array.mkEmpty (R.sources.size + R.bindings.size))
  incLoop R 0 c d

/-- One incremental step: the result, and this step's environment for the next. -/
def incStep (R : ResidualProgram) (prev : SlotEnv) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : RuntimeResult × SlotEnv :=
  let env := incEnv R prev i s
  (stepOfEnv R env e s, env)

/-! ## Facts about values, operands and environments -/

theorem sameVal_eq {a b : CertVal} (h : sameVal a b = true) : a = b := by
  cases a with
  | bv x =>
    cases b with
    | bv y =>
      simp only [sameVal, Bool.and_eq_true, beq_iff_eq] at h
      obtain ⟨hw, hv⟩ := h
      cases x; cases y
      simp_all
    | mem _ => simp [sameVal] at h
  | mem _ => simp [sameVal] at h

theorem anyRef_eq (p : Nat → Bool) (e : ResidualExpr) : anyRef p e = (exprRefs e).any p := by
  cases e <;> simp [anyRef, exprRefs, Bool.or_assoc]

theorem denoteRef_set_self {env : SlotEnv} {j : Nat} {v : CertVal} (h : j < env.size) :
    denoteRef (env.set! j v) j = v := by
  simp [denoteRef, Array.getElem?_setIfInBounds_self_of_lt h]

theorem denoteRef_set_ne {env : SlotEnv} {j r : Nat} {v : CertVal} (h : r ≠ j) :
    denoteRef (env.set! j v) r = denoteRef env r := by
  simp [denoteRef, Array.getElem?_setIfInBounds_ne (Ne.symm h)]

theorem denoteRef_sourceEnvArr (srcs : Array SourceDesc) (i : RuntimeInput) (s : RuntimeState)
    {j : Nat} (h : j < srcs.size) : denoteRef (sourceEnvArr srcs i s) j = sourceValue i s srcs[j] := by
  simp [denoteRef, sourceEnvArr, Array.getElem?_map, Array.getElem?_eq_getElem h]

/-- An operand not marked changed is a slot below the marks, marked unchanged. -/
theorem getD_false {dirty : Array Bool} {r : Nat} (h : dirty.getD r true = false) :
    r < dirty.size ∧ dirty[r]? = some false := by
  rw [Array.getD_eq_getD_getElem?] at h
  rcases Nat.lt_or_ge r dirty.size with hr | hr
  · refine ⟨hr, ?_⟩
    rw [Array.getElem?_eq_getElem hr] at h ⊢
    simpa using h
  · rw [Array.getElem?_eq_none hr] at h; simp at h

theorem push_getElem?_lt {dirty : Array Bool} {x : Bool} {r : Nat} (h : r < dirty.size) :
    (dirty.push x)[r]? = dirty[r]? := by
  rw [Array.getElem?_push, ite_eq_right (by omega)]

theorem push_getElem?_size {dirty : Array Bool} {x : Bool} :
    (dirty.push x)[dirty.size]? = some x := by
  simp

theorem wellOrdered_spec {R : ResidualProgram} (h : wellOrdered R = true) {k : Nat}
    (hk : k < R.bindings.size) : ∀ r ∈ exprRefs R.bindings[k].rhs, r < R.sources.size + k := by
  unfold wellOrdered at h
  have h2 := List.all_eq_true.mp h k (List.mem_range.mpr hk)
  rw [Array.getElem?_eq_getElem hk] at h2
  simpa using h2

/-! ## `runBindings` -/

theorem runBindings_append : ∀ (l1 l2 : List ResidualBinding) (env : SlotEnv),
    runBindings (l1 ++ l2) env = runBindings l2 (runBindings l1 env)
  | [], _, _ => rfl
  | b :: l1, l2, env => by
    simp only [List.cons_append, runBindings]
    exact runBindings_append l1 l2 _

theorem runBindings_size : ∀ (l : List ResidualBinding) (env : SlotEnv),
    (runBindings l env).size = env.size + l.length
  | [], _ => by simp [runBindings]
  | b :: l, env => by
    simp only [runBindings, runBindings_size l, Array.size_push, List.length_cons]
    omega

theorem runBindings_lt : ∀ (l : List ResidualBinding) (env : SlotEnv) {r : Nat}, r < env.size →
    denoteRef (runBindings l env) r = denoteRef env r
  | [], _, _, _ => rfl
  | b :: l, env, r, h => by
    simp only [runBindings]
    rw [runBindings_lt l _ (by simp; omega), denoteRef_push_lt h]

theorem runBindings_slot : ∀ (l : List ResidualBinding) (env : SlotEnv) {k : Nat}
    (hk : k < l.length),
    denoteRef (runBindings l env) (env.size + k) = denoteExpr (runBindings (l.take k) env) l[k].rhs
  | [], _, _, hk => absurd hk (by simp)
  | b :: l, env, 0, _ => by
    simp only [runBindings, List.take_zero, List.getElem_cons_zero, Nat.add_zero]
    rw [runBindings_lt l _ (by simp), denoteRef_push_size]
  | b :: l, env, k + 1, hk => by
    simp only [runBindings, List.take_succ_cons, List.getElem_cons_succ]
    have h := runBindings_slot l (env.push (denoteExpr env b.rhs)) (k := k) (by simpa using hk)
    rw [Array.size_push] at h
    rw [show env.size + (k + 1) = env.size + 1 + k by omega, h]

/-! ## Full environments: the fixed point of a well-ordered program -/

/-- `env` is a full environment of `R` for the sources `src`. -/
structure IsFull (R : ResidualProgram) (src env : SlotEnv) : Prop where
  size : env.size = R.sources.size + R.bindings.size
  srcs : ∀ j, j < R.sources.size → denoteRef env j = denoteRef src j
  bind : ∀ k (hk : k < R.bindings.size),
    denoteRef env (R.sources.size + k) = denoteExpr env R.bindings[k].rhs

theorem isFull_run {R : ResidualProgram} (hw : wellOrdered R = true) {src : SlotEnv}
    (hs : src.size = R.sources.size) : IsFull R src (runBindings R.bindings.toList src) where
  size := by rw [runBindings_size, hs]; simp
  srcs := fun j hj => runBindings_lt _ _ (by omega)
  bind := by
    intro k hk
    have hk' : k < R.bindings.toList.length := by simpa using hk
    have h := runBindings_slot R.bindings.toList src hk'
    rw [hs] at h
    rw [h, Array.getElem_toList]
    apply denoteExpr_congr
    intro r hr
    have hrk := wellOrdered_spec hw hk r hr
    have hsplit := runBindings_append (R.bindings.toList.take k) (R.bindings.toList.drop k) src
    rw [List.take_append_drop] at hsplit
    rw [hsplit, runBindings_lt (R.bindings.toList.drop k) _
      (by rw [runBindings_size, List.length_take]; simp; omega)]

theorem isFull_unique {R : ResidualProgram} (hw : wellOrdered R = true) {src e1 e2 : SlotEnv}
    (h1 : IsFull R src e1) (h2 : IsFull R src e2) : e1 = e2 := by
  have key : ∀ n r, r < n → denoteRef e1 r = denoteRef e2 r := by
    intro n
    induction n with
    | zero => intro r hr; omega
    | succ n ih =>
      intro r hr
      by_cases hrn : r < n
      · exact ih r hrn
      have hrn' : r = n := by omega
      subst hrn'
      by_cases hs : r < R.sources.size
      · rw [h1.srcs r hs, h2.srcs r hs]
      · by_cases hb : r < R.sources.size + R.bindings.size
        · obtain ⟨k, rfl⟩ : ∃ k, r = R.sources.size + k := ⟨r - R.sources.size, by omega⟩
          have hk : k < R.bindings.size := by omega
          rw [h1.bind k hk, h2.bind k hk]
          exact denoteExpr_congr _ (fun r' hr' => ih r' (by have := wellOrdered_spec hw hk r' hr'; omega))
        · have he1 : e1.size ≤ r := by rw [h1.size]; omega
          have he2 : e2.size ≤ r := by rw [h2.size]; omega
          simp [denoteRef, Array.getElem?_eq_none he1, Array.getElem?_eq_none he2]
  apply Array.ext
  · rw [h1.size, h2.size]
  · intro r hr1 hr2
    have := key (r + 1) r (by omega)
    simp only [denoteRef, Array.getElem?_eq_getElem hr1, Array.getElem?_eq_getElem hr2,
      Option.getD_some] at this
    exact this

theorem fullEnv_eq (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeState) :
    fullEnv R i s = runBindings R.bindings.toList (sourceEnvArr R.sources i s) := by
  rw [fullEnv, runBindings_eq_foldl, Array.foldl_toList]

theorem isFull_fullEnv {R : ResidualProgram} (hw : wellOrdered R = true) (i : RuntimeInput)
    (s : RuntimeState) : IsFull R (sourceEnvArr R.sources i s) (fullEnv R i s) := by
  rw [fullEnv_eq]
  exact isFull_run hw (sourceEnvArr_size _ _ _)

theorem denoteResidual_eq_stepOfEnv (R : ResidualProgram) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : denoteResidual R e i s = stepOfEnv R (fullEnv R i s) e s := by
  rw [fullEnv_eq]
  rfl

/-! ## The incremental pass ends in the full environment -/

section
variable (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeState) (prev : SlotEnv)

/-- After the sources below `j`: this step's values there, last step's (`prev`) above, and every
slot marked unchanged holding last step's value. -/
structure SrcInv (j : Nat) (cur : SlotEnv) (dirty : Array Bool) : Prop where
  size  : cur.size = prev.size
  dsize : dirty.size = j
  done  : ∀ j', j' < j → denoteRef cur j' = denoteRef (sourceEnvArr R.sources i s) j'
  rest  : ∀ r, j ≤ r → denoteRef cur r = denoteRef prev r
  clean : ∀ r, dirty[r]? = some false → denoteRef cur r = denoteRef prev r

/-- After the bindings below `k`: the sources and those bindings hold this step's values
(each binding its own expression), the rest last step's, and every slot marked unchanged holds
last step's value. -/
structure LoopInv (k : Nat) (cur : SlotEnv) (dirty : Array Bool) : Prop where
  size  : cur.size = R.sources.size + R.bindings.size
  dsize : dirty.size = R.sources.size + k
  srcs  : ∀ j, j < R.sources.size → denoteRef cur j = denoteRef (sourceEnvArr R.sources i s) j
  done  : ∀ k' (hk' : k' < R.bindings.size), k' < k →
            denoteRef cur (R.sources.size + k') = denoteExpr cur R.bindings[k'].rhs
  rest  : ∀ r, R.sources.size + k ≤ r → denoteRef cur r = denoteRef prev r
  clean : ∀ r, dirty[r]? = some false → denoteRef cur r = denoteRef prev r

theorem clean_push_false {cur : SlotEnv} {dirty : Array Bool} {n : Nat}
    (hd : dirty.size = n) (hrest : denoteRef cur n = denoteRef prev n)
    (hclean : ∀ r, dirty[r]? = some false → denoteRef cur r = denoteRef prev r) :
    ∀ r, (dirty.push false)[r]? = some false → denoteRef cur r = denoteRef prev r := by
  intro r hr
  rw [Array.getElem?_push] at hr
  split at hr
  · rename_i hrn; rw [hrn, hd]; exact hrest
  · exact hclean r hr

theorem incSources_spec {i' : RuntimeInput} {s' : RuntimeState}
    (hp : IsFull R (sourceEnvArr R.sources i' s') prev)
    (hpS : prev.size = R.sources.size + R.bindings.size) :
    ∀ (n j : Nat) (cur : SlotEnv) (dirty : Array Bool), R.sources.size - j = n →
      j ≤ R.sources.size → SrcInv R i s prev j cur dirty →
      SrcInv R i s prev R.sources.size (incSources R i s j cur dirty).1
        (incSources R i s j cur dirty).2 := by
  intro n
  induction n with
  | zero =>
    intro j cur dirty hn hj hinv
    have hjS : j = R.sources.size := by omega
    rw [incSources, dite_eq_right (by omega)]
    subst hjS; exact hinv
  | succ n ih =>
    intro j cur dirty hn hj hinv
    have hjS : j < R.sources.size := by omega
    rw [incSources, dite_eq_left hjS]
    split
    · -- a constant source keeps its slot
      rename_i w v hc
      apply ih (j + 1) cur (dirty.push false) (by omega) (by omega)
      exact {
        size := hinv.size
        dsize := by simp [hinv.dsize]
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
        clean := clean_push_false prev hinv.dsize (hinv.rest j (Nat.le_refl _)) hinv.clean }
    · -- any other source takes this step's value
      rename_i hnc
      have hj' : j < cur.size := by rw [hinv.size, hpS]; omega
      apply ih (j + 1) _ _ (by omega) (by omega)
      exact {
        size := by simp [hinv.size]
        dsize := by simp [hinv.dsize]
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
          intro r hr
          rw [Array.getElem?_push] at hr
          split at hr
          · rename_i hrn
            rw [hinv.dsize] at hrn
            subst hrn
            simp only [Option.some.injEq, Bool.not_eq_false'] at hr
            rw [denoteRef_set_self hj', sameVal_eq hr, hinv.rest r (Nat.le_refl _)]
          · have hr' : r < j := by
              rcases Nat.lt_or_ge r dirty.size with h | h
              · rw [hinv.dsize] at h; exact h
              · rw [Array.getElem?_eq_none h] at hr; simp at hr
            rw [denoteRef_set_ne (by omega)]; exact hinv.clean r hr }

theorem incLoop_spec {i' : RuntimeInput} {s' : RuntimeState} (hw : wellOrdered R = true)
    (hp : IsFull R (sourceEnvArr R.sources i' s') prev) :
    ∀ (n k : Nat) (cur : SlotEnv) (dirty : Array Bool), R.bindings.size - k = n →
      k ≤ R.bindings.size → LoopInv R i s prev k cur dirty →
      IsFull R (sourceEnvArr R.sources i s) (incLoop R k cur dirty) := by
  intro n
  induction n with
  | zero =>
    intro k cur dirty hn hk hinv
    have hkB : k = R.bindings.size := by omega
    rw [incLoop, dite_eq_right (by omega)]
    exact {
      size := hinv.size
      srcs := hinv.srcs
      bind := fun k' hk' => hinv.done k' hk' (by omega) }
  | succ n ih =>
    intro k cur dirty hn hk hinv
    have hkB : k < R.bindings.size := by omega
    have hslot : R.sources.size + k < cur.size := by rw [hinv.size]; omega
    rw [incLoop, dite_eq_left hkB]
    simp only []
    split
    · -- some operand changed: recompute
      rename_i hany
      apply ih (k + 1) _ _ (by omega) (by omega)
      have hne : ∀ r ∈ exprRefs R.bindings[k].rhs, r ≠ R.sources.size + k := by
        intro r hr; have := wellOrdered_spec hw hkB r hr; omega
      exact {
        size := by simp [hinv.size]
        dsize := by simp [hinv.dsize]; omega
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
          intro r hr
          rw [Array.getElem?_push] at hr
          split at hr
          · rename_i hrn
            rw [hinv.dsize] at hrn
            subst hrn
            simp only [Option.some.injEq, Bool.not_eq_false'] at hr
            rw [denoteRef_set_self hslot, sameVal_eq hr, hinv.rest _ (Nat.le_refl _)]
          · have hr' : r < R.sources.size + k := by
              rcases Nat.lt_or_ge r dirty.size with h | h
              · rw [hinv.dsize] at h; exact h
              · rw [Array.getElem?_eq_none h] at hr; simp at hr
            rw [denoteRef_set_ne (by omega)]; exact hinv.clean r hr }
    · -- no operand changed: keep last step's value
      rename_i hany
      rw [anyRef_eq] at hany
      have hun : ∀ r ∈ exprRefs R.bindings[k].rhs, denoteRef prev r = denoteRef cur r := by
        intro r hr
        have hp' : dirty.getD r true = false := by
          have := List.any_eq_false.mp (by simpa using hany) r hr
          simpa using this
        obtain ⟨_, hd⟩ := getD_false hp'
        exact (hinv.clean r hd).symm
      apply ih (k + 1) _ _ (by omega) (by omega)
      exact {
        size := hinv.size
        dsize := by simp [hinv.dsize]; omega
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
        clean := clean_push_false prev (by rw [hinv.dsize]) (hinv.rest _ (Nat.le_refl _)) hinv.clean }

end

/-- **The incremental pass is the full step.** From the full environment of any earlier step,
`incEnv` computes exactly this step's full environment. -/
theorem incEnv_correct {R : ResidualProgram} (hw : wellOrdered R = true)
    (i' : RuntimeInput) (s' : RuntimeState) (i : RuntimeInput) (s : RuntimeState) :
    incEnv R (fullEnv R i' s') i s = fullEnv R i s := by
  have hp := isFull_fullEnv hw i' s'
  have hpS := hp.size
  have h0 : SrcInv R i s (fullEnv R i' s') 0 (fullEnv R i' s')
      (Array.mkEmpty (R.sources.size + R.bindings.size)) := {
    size := rfl
    dsize := by simp
    done := fun j' h => absurd h (by omega)
    rest := fun _ _ => rfl
    clean := by intro r hr; simp at hr }
  have h1 := incSources_spec R i s (fullEnv R i' s') hp hpS _ 0 _ _ rfl (by omega) h0
  have h2 : LoopInv R i s (fullEnv R i' s') 0
      (incSources R i s 0 (fullEnv R i' s') (Array.mkEmpty (R.sources.size + R.bindings.size))).1
      (incSources R i s 0 (fullEnv R i' s') (Array.mkEmpty (R.sources.size + R.bindings.size))).2 := {
    size := by rw [h1.size, hpS]
    dsize := by rw [h1.dsize]; omega
    srcs := h1.done
    done := fun _ _ h => absurd h (by omega)
    rest := fun r hr => h1.rest r (by omega)
    clean := h1.clean }
  have h3 := incLoop_spec R i s (fullEnv R i' s') hw hp _ 0 _ _ rfl (by omega) h2
  exact isFull_unique hw h3 (isFull_fullEnv hw i s)

/-- **One incremental step is `denoteResidual`**, and leaves this step's full environment for
the next. -/
theorem incStep_correct {R : ResidualProgram} (hw : wellOrdered R = true)
    (i' : RuntimeInput) (s' : RuntimeState) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) :
    incStep R (fullEnv R i' s') e i s = (denoteResidual R e i s, fullEnv R i s) := by
  simp only [incStep, incEnv_correct hw, denoteResidual_eq_stepOfEnv]

/-! ## Runs -/

/-- Steps of `denoteResidual`, each from the state the one before left. -/
def runFull (R : ResidualProgram) : RuntimeState → List (ClockEdges × RuntimeInput) →
    List RuntimeResult
  | _, [] => []
  | s, (e, i) :: rest =>
    let r := denoteResidual R e i s
    r :: runFull R r.nextState rest

/-- The same steps, each from the environment the one before left. -/
def runIncFrom (R : ResidualProgram) (env : SlotEnv) : RuntimeState →
    List (ClockEdges × RuntimeInput) → List RuntimeResult
  | _, [] => []
  | s, (e, i) :: rest =>
    let p := incStep R env e i s
    p.1 :: runIncFrom R p.2 p.1.nextState rest

/-- A run: the first step full, the others incremental. -/
def runInc (R : ResidualProgram) : RuntimeState → List (ClockEdges × RuntimeInput) →
    List RuntimeResult
  | _, [] => []
  | s, (e, i) :: rest =>
    let env := fullEnv R i s
    let r := stepOfEnv R env e s
    r :: runIncFrom R env r.nextState rest

theorem runIncFrom_eq {R : ResidualProgram} (hw : wellOrdered R = true) :
    ∀ (ticks : List (ClockEdges × RuntimeInput)) (i' : RuntimeInput) (s' s : RuntimeState),
      runIncFrom R (fullEnv R i' s') s ticks = runFull R s ticks
  | [], _, _, _ => rfl
  | (e, i) :: rest, i', s', s => by
    simp only [runIncFrom, runFull, incStep_correct hw]
    rw [runIncFrom_eq hw rest i s]

/-- **An incremental run is a run of `denoteResidual`**, step for step. -/
theorem runInc_eq_runFull {R : ResidualProgram} (hw : wellOrdered R = true)
    (s : RuntimeState) (ticks : List (ClockEdges × RuntimeInput)) :
    runInc R s ticks = runFull R s ticks := by
  cases ticks with
  | nil => rfl
  | cons t rest =>
    obtain ⟨e, i⟩ := t
    simp only [runInc, runFull, ← denoteResidual_eq_stepOfEnv]
    rw [runIncFrom_eq hw rest i s]

end ResidualInc
end Compiler

#print axioms Compiler.ResidualInc.incEnv_correct
#print axioms Compiler.ResidualInc.runInc_eq_runFull
