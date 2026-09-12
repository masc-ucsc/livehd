/-
# `DirectSemantics` — the LGraph certificate executed directly

Direction 2, phases 2 and 3.

`interpretDesign` already says what a `DesignCert` MEANS, and it is the
specification B1+B2 is proved against.  What it is not is a simulator: its
environment is a nested `Nat → CertVal` built by repeated `envSetG`, so one
lookup costs O(N) and evaluating every slot costs O(N²).  Its own source says it
is never executed.

This module turns that same semantics into something that runs.  One cycle is
ONE left-to-right pass over `D.nodes`, each node's value appended at its own
dense slot, every dependency an earlier array read.

## What makes this DIRECT

The evaluator dispatches on `DenseNodeCert.op`, i.e. on `LGraphOp`, through
`eval_op_cert`.  It never calls `compileDesign`, never constructs a
`ResidualProgram`, and never mentions `ResidualExpr` — this file does not even
import the residual language.  Operator dispatch therefore remains visibly the
semantics of LGraph rather than the semantics of a target language, which is the
whole distinction between Direction 2 and B1+B2.

## The proof is uniqueness of the topo fixpoint, not a fresh induction

`GraphRefine.evalGraphG_of_localAgree` already proves, generically over any
`[NodeSemantics V]`, that ANY environment satisfying the per-node local
recurrence IS `evalGraphG`.  So the work here is to show that "read slot `k` out
of the dense array" satisfies that recurrence — three `Array.push` stability
facts — and B2's theorem concludes.  The shape deliberately mirrors
`compileGraph_correct`, with `compileOp_correct` replaced by nothing at all:
the dense array holds `eval_op_cert` applied to the node's own operator, so the
recurrence is an equality between two spellings of the same call.
-/
import LeanSemanticPrimitives.Compiler.DirectCheck

namespace Compiler
namespace Direct

open Compiler.DesignCert GraphRefine

--------------------------------------------------------------------------------
-- The evaluator
--------------------------------------------------------------------------------

/-- One node, against the slot array built so far.  The dispatch is
`eval_op_cert` on the node's own `LGraphOp`. -/
@[inline] def evalDenseNode (env : SlotEnv) (c : DenseNodeCert) : CertVal :=
  eval_op_cert c.op c.width (c.deps.toList.map (denoteRef env))

/-- Proof-facing spelling: a structural recursion over the node LIST.  It
mirrors `runBindings` exactly, so the size / stability / value-at facts below
are the same three the compiled path needs — re-proved for this evaluator, not
borrowed from it. -/
def evalDenseList : List DenseNodeCert → SlotEnv → SlotEnv
  | [],      env => env
  | c :: cs, env => evalDenseList cs (env.push (evalDenseNode env c))

/-- Execution-facing spelling: ONE left-to-right pass over the node array.
`Array.foldl` compiles to a loop, so a 110k-node design costs 110k iterations
and no stack depth. -/
def evalDense (nodes : Array DenseNodeCert) (env : SlotEnv) : SlotEnv :=
  nodes.foldl (fun e c => e.push (evalDenseNode e c)) env

theorem evalDenseList_eq_foldl : ∀ (cs : List DenseNodeCert) (env : SlotEnv),
    evalDenseList cs env = cs.foldl (fun e c => e.push (evalDenseNode e c)) env := by
  intro cs
  induction cs with
  | nil => intro env; rfl
  | cons c cs ih => intro env; simp only [evalDenseList, List.foldl_cons, ih]

theorem evalDense_eq_list (nodes : Array DenseNodeCert) (env : SlotEnv) :
    evalDense nodes env = evalDenseList nodes.toList env := by
  rw [evalDenseList_eq_foldl, evalDense, Array.foldl_toList]

--------------------------------------------------------------------------------
-- The three array facts
--------------------------------------------------------------------------------

theorem evalDenseList_size : ∀ (cs : List DenseNodeCert) (env : SlotEnv),
    (evalDenseList cs env).size = env.size + cs.length := by
  intro cs
  induction cs with
  | nil => intro env; simp [evalDenseList]
  | cons c cs ih =>
      intro env
      simp only [evalDenseList, ih, Array.size_push, List.length_cons]; omega

/-- Appending later nodes never disturbs an earlier slot.  This is what makes
"the array as it stood when node `i` ran" and "the final array" interchangeable
on `i`'s dependencies. -/
theorem evalDenseList_stable : ∀ (cs : List DenseNodeCert) (env : SlotEnv) (j : Nat),
    j < env.size → (evalDenseList cs env)[j]? = env[j]? := by
  intro cs
  induction cs with
  | nil => intro env j _; rfl
  | cons c cs ih =>
      intro env j hj
      simp only [evalDenseList]
      rw [ih _ j (by simp only [Array.size_push]; omega)]
      rw [Array.getElem?_push_lt hj, Array.getElem?_eq_getElem hj]

/-- Node `k` lands at slot `env.size + k`, holding `eval_op_cert` applied to its
own operator in the array that existed at that moment. -/
theorem evalDenseList_at : ∀ (cs : List DenseNodeCert) (env : SlotEnv) (k : Nat),
    k < cs.length →
      (evalDenseList cs env)[env.size + k]? =
        (cs[k]?).map fun c => evalDenseNode (evalDenseList (cs.take k) env) c := by
  intro cs
  induction cs with
  | nil => intro env k hk; simp at hk
  | cons c cs ih =>
      intro env k hk
      cases k with
      | zero =>
          simp only [evalDenseList, Nat.add_zero, List.take_zero, List.getElem?_cons_zero,
            Option.map_some]
          rw [evalDenseList_stable _ _ _ (by simp only [Array.size_push]; omega)]
          exact Array.getElem?_push_size
      | succ k =>
          have hk' : k < cs.length := by simpa using hk
          have hsz : env.size + (k + 1) = (env.push (evalDenseNode env c)).size + k := by
            simp only [Array.size_push]; omega
          simp only [evalDenseList, hsz, ih _ k hk', List.take_succ_cons,
            List.getElem?_cons_succ]

theorem evalDenseList_append : ∀ (l1 l2 : List DenseNodeCert) (env : SlotEnv),
    evalDenseList (l1 ++ l2) env = evalDenseList l2 (evalDenseList l1 env) := by
  intro l1
  induction l1 with
  | nil => intro l2 env; rfl
  | cons c l1 ih => intro l2 env; simp only [List.cons_append, evalDenseList, ih]

/-- The array agrees with the array that existed when node `i` ran, on every
slot strictly below `S + i`. -/
theorem evalDenseList_prefix_agree (cs : List DenseNodeCert) (env : SlotEnv) (i r : Nat)
    (hi : i ≤ cs.length) (hr : r < env.size + i) :
    (evalDenseList cs env)[r]? = (evalDenseList (cs.take i) env)[r]? := by
  have hsz : (evalDenseList (cs.take i) env).size = env.size + i := by
    rw [evalDenseList_size, List.length_take, Nat.min_eq_left hi]
  have key : evalDenseList cs env = evalDenseList (cs.drop i) (evalDenseList (cs.take i) env) := by
    rw [← evalDenseList_append, List.take_append_drop]
  rw [key]
  exact evalDenseList_stable (cs.drop i) (evalDenseList (cs.take i) env) r (by omega)

--------------------------------------------------------------------------------
-- Slot agreement with the reference environment
--------------------------------------------------------------------------------

/-- A SOURCE slot reads the same value on both sides. -/
theorem srcEnv_agree_dense (D : DesignCert) (cs : List DenseNodeCert)
    (inp : RuntimeInput) (st : RuntimeState) (d : Nat) (hd : d < D.sources.size) :
    srcEnv D inp st d = denoteRef (evalDenseList cs (sourceEnvArr D.sources inp st)) d := by
  have hsz0 : (sourceEnvArr D.sources inp st).size = D.sources.size := sourceEnvArr_size _ _ _
  have hstab : (evalDenseList cs (sourceEnvArr D.sources inp st))[d]?
      = (sourceEnvArr D.sources inp st)[d]? :=
    evalDenseList_stable _ _ d (by omega)
  simp only [denoteRef]
  rw [hstab]
  simp only [sourceEnvArr, Array.getElem?_map, Array.getElem?_eq_getElem hd, srcEnv,
    Option.map_some, Option.getD_some]

/-- The graph-level theorem, with the two environments and the candidate
fixpoint as ORDINARY parameters.  Spelled this way because `set` is a Mathlib
tactic and this module is deliberately Mathlib-free: the simulator's import
chain should not pull in a 7 GB library. -/
theorem evalDense_topo_gen (D : DesignCert) (hdb : DesignCert.DepsBounded D)
    (src : Nat → CertVal) (env0 ENV : SlotEnv) (φ : Nat → CertVal)
    (hsz0 : env0.size = D.sources.size)
    (hENV : ENV = evalDenseList D.nodes.toList env0)
    (hφ : φ = fun k => denoteRef ENV k)
    (hsrcslot : ∀ d, d < D.sources.size → src d = φ d) :
    ∀ n ∈ D.topo, evalGraphG D.toGraphCert.topo D.toGraphCert src n = φ n := by
  have hlen : D.nodes.toList.length = D.nodes.size := Array.length_toList
  -- the local recurrence at every topo slot
  have hrec : ∀ n ∈ D.toGraphCert.topo, φ n = evalNodeG D.toGraphCert φ n := by
    intro n hn
    obtain ⟨hlo, hhi⟩ := mem_topo_iff.mp hn
    simp only [numSources, numSlots] at hlo hhi
    obtain ⟨i, hni⟩ : ∃ i : Nat, n = D.slotOfNode i :=
      ⟨n - D.sources.size, by simp only [slotOfNode]; omega⟩
    have hiN : i < D.nodes.size := by simp only [slotOfNode] at hni; omega
    obtain ⟨c, hc⟩ : ∃ c, D.nodes[i]? = some c :=
      ⟨D.nodes[i], Array.getElem?_eq_getElem hiN⟩
    -- the array as it stood when node `i` ran
    have hφn : φ n = evalDenseNode (evalDenseList (D.nodes.toList.take i) env0) c := by
      have hat := evalDenseList_at D.nodes.toList env0 i (by omega)
      have hcl : D.nodes.toList[i]? = some c := by simpa using hc
      rw [hcl, Option.map_some, hsz0] at hat
      simp only [hφ, hENV, denoteRef, hni, slotOfNode, hat, Option.getD_some]
    -- every dep of node i is a slot strictly below n, so the two arrays agree there
    have hagree : ∀ r ∈ c.deps.toList,
        denoteRef (evalDenseList (D.nodes.toList.take i) env0) r = φ r := by
      intro r hr
      have hrlt : r < D.sources.size + i := hdb i c hc r hr
      have hag := evalDenseList_prefix_agree D.nodes.toList env0 i r (by omega) (by omega)
      simp only [hφ, hENV, denoteRef, hag]
    -- both sides are `eval_op_cert` on the SAME operator; only the environment differs
    have hnode : evalNodeG D.toGraphCert φ n
        = eval_op_cert c.op c.width (c.deps.toList.map φ) := by
      simp only [evalNodeG, toGraphCert, hni, nodeAt?_slotOfNode, hc, Option.map_some]
      rfl
    rw [hφn, hnode, evalDenseNode, List.map_congr_left hagree]
  -- deps that leave the topo list are sources
  have hsrc : ∀ n ∈ D.toGraphCert.topo, ∀ d ∈ depopts_of D.toGraphCert n,
      d ∉ D.toGraphCert.topo → src d = φ d := by
    intro n hn d hd hdnot
    obtain ⟨hlo, hhi⟩ := mem_topo_iff.mp hn
    simp only [numSources, numSlots] at hlo hhi
    obtain ⟨i, hni⟩ : ∃ i : Nat, n = D.slotOfNode i :=
      ⟨n - D.sources.size, by simp only [slotOfNode]; omega⟩
    have hiN : i < D.nodes.size := by simp only [slotOfNode] at hni; omega
    obtain ⟨c, hc⟩ : ∃ c, D.nodes[i]? = some c :=
      ⟨D.nodes[i], Array.getElem?_eq_getElem hiN⟩
    rw [hni, depopts_slotOfNode hc] at hd
    have hdlt : d < D.sources.size + i := hdb i c hc d hd
    have hds : d < D.sources.size := by
      by_cases h : d < D.sources.size
      · exact h
      · exact absurd (mem_topo_iff.mpr ⟨by simpa [numSources] using Nat.le_of_not_lt h,
          by simp only [numSlots]; omega⟩) hdnot
    exact hsrcslot d hds
  intro n hn
  exact evalGraphG_of_localAgree D.toGraphCert φ src
    topo_nodup (wf_depOrdered hdb) wf_isSome hrec hsrc n hn

/-- On every topo slot, the dense array holds exactly what `evalGraphG` computes. -/
theorem evalDense_topo (D : DesignCert) (hdb : DesignCert.DepsBounded D)
    (inp : RuntimeInput) (st : RuntimeState) :
    ∀ n ∈ D.topo,
      evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D inp st) n
        = denoteRef (evalDenseList D.nodes.toList (sourceEnvArr D.sources inp st)) n :=
  evalDense_topo_gen D hdb (srcEnv D inp st) (sourceEnvArr D.sources inp st)
    (evalDenseList D.nodes.toList (sourceEnvArr D.sources inp st))
    (fun k => denoteRef (evalDenseList D.nodes.toList (sourceEnvArr D.sources inp st)) k)
    (sourceEnvArr_size _ _ _) rfl rfl
    (fun d hd => srcEnv_agree_dense D D.nodes.toList inp st d hd)

/-- Total slot agreement: EVERY slot, not just topo slots.  The off-topo cases
are the ones that bite — an output or a flop `din` driven straight by a source. -/
theorem evalDense_slot_agree (D : DesignCert) (hdb : DesignCert.DepsBounded D)
    (inp : RuntimeInput) (st : RuntimeState) (k : Nat) :
    evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D inp st) k
      = denoteRef (evalDenseList D.nodes.toList (sourceEnvArr D.sources inp st)) k := by
  by_cases hk : k ∈ D.topo
  · exact evalDense_topo D hdb inp st k hk
  · rw [evalGraphG_not_mem D.toGraphCert _ D.toGraphCert.topo k hk]
    by_cases hks : k < D.sources.size
    · exact srcEnv_agree_dense D D.nodes.toList inp st k hks
    · have hkge : D.sources.size + D.nodes.size ≤ k := by
        by_cases hin : k < D.sources.size + D.nodes.size
        · exact absurd (mem_topo_iff.mpr ⟨by simpa [numSources] using Nat.le_of_not_lt hks,
              by simpa [numSlots] using hin⟩) hk
        · omega
      have hsz : (evalDenseList D.nodes.toList (sourceEnvArr D.sources inp st)).size
          = D.sources.size + D.nodes.size := by
        rw [evalDenseList_size, sourceEnvArr_size, Array.length_toList]
      have hnone : (evalDenseList D.nodes.toList (sourceEnvArr D.sources inp st))[k]? = none :=
        Array.getElem?_eq_none (by omega)
      simp only [srcEnv, Array.getElem?_eq_none (Nat.le_of_not_lt hks), denoteRef, hnone,
        Option.getD_none]

--------------------------------------------------------------------------------
-- One cycle
--------------------------------------------------------------------------------

/-- The flop rule, spelled against the dense array.  Written with nested `if`s
over explicit reads rather than delegating to `srcFlopNext`, so
`directFlopNext_agree` genuinely re-checks reset PRIORITY over enable, reset
POLARITY, the reset VALUE and the hold fallback instead of unfolding to them. -/
def directFlopNext (env : SlotEnv) (s : RuntimeState) (idx : Nat) (f : FlopDesc) : BV :=
  let inReset : Bool :=
    match f.resetPin with
    | none   => false
    | some r => let rv := bv_nonzero (denoteRef env r).asBV
                if f.resetActiveLow then !rv else rv
  if inReset then mk_bv f.width f.resetValue
  else
    let en : Bool :=
      match f.enable with
      | none   => true
      | some e => bv_nonzero (denoteRef env e).asBV
    if en then bv_resize f.width (denoteRef env f.din).asBV
    else s.flops[idx]?.getD (mk_bv f.width 0)

theorem directFlopNext_agree (rho : Nat → CertVal) (env : SlotEnv)
    (hag : ∀ k, rho k = denoteRef env k) (st : RuntimeState) (idx : Nat) (f : FlopDesc) :
    srcFlopNext rho st idx f = directFlopNext env st idx f := by
  have hb : ∀ r, (rho r).asBV = (denoteRef env r).asBV := by intro r; rw [hag r]
  have hxor : ∀ rv : Bool,
      xor f.resetActiveLow rv = (if f.resetActiveLow = true then !rv else rv) := by
    intro rv; cases f.resetActiveLow <;> simp
  simp only [srcFlopNext, directFlopNext, hb, hxor]
  rfl

/-- One transition of the post-lowering IR, unchecked.  `directStep` is the
public entry point; this is the body its theorem is about. -/
def directStepRaw (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : RuntimeResult :=
  let env := evalDense D.nodes (sourceEnvArr D.sources i s)
  { outputs   := D.outputs.map fun o => bv_resize o.width (denoteRef env o.slot).asBV
    nextState :=
      { flops := D.flops.mapIdx fun idx f => directFlopNext env s idx f
        mems  := D.memories.map fun m => (denoteRef env m.nextImg).asMem } }

/-- **The public one-step API.**  Fail-closed: a certificate outside the
accepted fragment, or an input/state of the wrong shape, is refused BEFORE any
evaluation, so no fallback branch is ever user-visible. -/
def directStep (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) :
    Except SimError RuntimeResult :=
  match checkDesign D with
  | .error e => .error e
  | .ok _ =>
    match checkRuntime D i s with
    | .error e => .error e
    | .ok _ => .ok (directStepRaw D i s)

--------------------------------------------------------------------------------
-- Correctness
--------------------------------------------------------------------------------

/-- The unchecked evaluator already implements the reference semantics; the only
thing it needs is dependency ordering. -/
theorem directStepRaw_correct (D : DesignCert) (hdb : DesignCert.DepsBounded D)
    (i : RuntimeInput) (s : RuntimeState) :
    directStepRaw D i s = interpretDesign D i s := by
  have hrho : evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s)
      = denoteRef (evalDenseList D.nodes.toList (sourceEnvArr D.sources i s)) := by
    funext k; exact evalDense_slot_agree D hdb i s k
  simp only [directStepRaw, interpretDesign, evalDense_eq_list, hrho]
  congr 1
  congr 1
  apply Array.ext
  · simp
  · intro idx h1 h2
    have hi : idx < D.flops.size := by simpa using h2
    simp only [Array.getElem_mapIdx]
    exact (directFlopNext_agree
      (denoteRef (evalDenseList D.nodes.toList (sourceEnvArr D.sources i s)))
      (evalDenseList D.nodes.toList (sourceEnvArr D.sources i s))
      (fun _ => rfl) s idx D.flops[idx]).symm

/-- **`directStep_correct`.**  Successful direct execution IS the certificate's
one-cycle meaning.  The `.ok` premise is the only semantic hypothesis: it
witnesses dependency ordering (through `checkDesign_sound`), so no separate
well-formedness assumption is needed. -/
theorem directStep_correct (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (r : RuntimeResult) (h : directStep D i s = .ok r) :
    r = interpretDesign D i s := by
  unfold directStep at h
  cases hc : checkDesign D with
  | error e => rw [hc] at h; exact absurd h (by simp)
  | ok u =>
      rw [hc] at h
      cases hr : checkRuntime D i s with
      | error e => rw [hr] at h; exact absurd h (by simp)
      | ok v =>
          rw [hr] at h
          injection h with h
          subst h
          exact directStepRaw_correct D (checkDesign_sound hc).depsBounded i s

/-- Determinism, and preservation of every array shape the certificate declares. -/
theorem directStep_deterministic (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (r₁ r₂ : RuntimeResult) (h₁ : directStep D i s = .ok r₁) (h₂ : directStep D i s = .ok r₂) :
    r₁ = r₂ := by
  have hEq := h₁.symm.trans h₂
  injection hEq with h

theorem directStepRaw_sizes (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) :
    (directStepRaw D i s).outputs.size = D.outputs.size ∧
    (directStepRaw D i s).nextState.flops.size = D.flops.size ∧
    (directStepRaw D i s).nextState.mems.size = D.memories.size := by
  simp [directStepRaw]

end Direct
end Compiler
