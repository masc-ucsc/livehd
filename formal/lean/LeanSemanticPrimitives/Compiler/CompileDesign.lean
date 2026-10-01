/-
# `compileDesign_correct`

Step 7: outputs, sequential state, and the top-level theorem.

The graph-level step (`compileGraph_correct`) is *uniqueness of the topo
fixpoint*, not a fresh induction — see `CompileGraph.lean`'s header.
-/
import Mathlib
import LeanSemanticPrimitives.Compiler.CompileGraph

namespace Compiler
open Residual GraphRefine DesignCert

theorem runBindings_append : ∀ (l1 l2 : List ResidualBinding) (env : SlotEnv),
    runBindings (l1 ++ l2) env = runBindings l2 (runBindings l1 env) := by
  intro l1
  induction l1 with
  | nil => intro l2 env; rfl
  | cons b l1 ih => intro l2 env; simp only [List.cons_append, runBindings, ih]

/-- The compiled environment agrees with the environment that existed when
binding `i` was evaluated, on every slot strictly below `S + i`. -/
theorem runBindings_prefix_agree (bs : List ResidualBinding) (env : SlotEnv) (i r : Nat)
    (hi : i ≤ bs.length) (hr : r < env.size + i) :
    (runBindings bs env)[r]? = (runBindings (bs.take i) env)[r]? := by
  have hsz : (runBindings (bs.take i) env).size = env.size + i := by
    rw [runBindings_size, List.length_take, Nat.min_eq_left hi]
  have key : runBindings bs env = runBindings (bs.drop i) (runBindings (bs.take i) env) := by
    conv_lhs => rw [← List.take_append_drop i bs]
    rw [runBindings_append]
  rw [key]
  exact runBindings_stable (bs.drop i) (runBindings (bs.take i) env) r (by omega)

/-- A SOURCE slot reads the same value on both sides: the interpreter's
`srcEnv` and the compiled environment's initial segment are the same array. -/
theorem srcEnv_agree (D : DesignCert) (bs : Array ResidualBinding)
    (inp : RuntimeInput) (st : RuntimeState) (d : Nat) (hd : d < D.sources.size) :
    srcEnv D inp st d
      = denoteRef (runBindings bs.toList (sourceEnvArr D.sources inp st)) d := by
  have hsz0 : (sourceEnvArr D.sources inp st).size = D.sources.size :=
    sourceEnvArr_size _ _ _
  have hstab : (runBindings bs.toList (sourceEnvArr D.sources inp st))[d]?
      = (sourceEnvArr D.sources inp st)[d]? :=
    runBindings_stable _ _ d (by omega)
  simp only [denoteRef]
  rw [hstab]
  simp only [sourceEnvArr, Array.getElem?_map, Array.getElem?_eq_getElem hd, srcEnv,
    Option.map_some, Option.getD_some]

--------------------------------------------------------------------------------
-- The graph-level theorem
--------------------------------------------------------------------------------

theorem compileGraph_correct (D : DesignCert) (hdb : DesignCert.DepsBounded D)
    (bs : Array ResidualBinding) (hbs : compileGraph D = .ok bs)
    (inp : RuntimeInput) (st : RuntimeState) :
    ∀ n ∈ D.topo,
      evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D inp st) n
        = denoteRef (runBindings bs.toList (sourceEnvArr D.sources inp st)) n := by
  set env0 := sourceEnvArr D.sources inp st with henv0
  set ENV := runBindings bs.toList env0 with hENV
  set φ : Nat → CertVal := fun k => denoteRef ENV k with hφ
  have hsz0 : env0.size = D.sources.size := by
    rw [henv0]; exact sourceEnvArr_size _ _ _
  have hlen : bs.toList.length = D.nodes.size := by
    simp only [Array.length_toList]; exact compileGraph_size D bs hbs
  -- source slots read through to the runtime source values
  have hsrcslot : ∀ d, d < D.sources.size → srcEnv D inp st d = φ d := by
    intro d hd
    have := srcEnv_agree D bs inp st d hd
    rw [← henv0, ← hENV] at this
    simpa [hφ] using this
  -- the local recurrence at every topo slot
  have hrec : ∀ n ∈ D.toGraphCert.topo, φ n = evalNodeG D.toGraphCert φ n := by
    intro n hn
    obtain ⟨hlo, hhi⟩ := mem_topo_iff.mp hn
    simp only [numSources, numSlots] at hlo hhi
    -- n is slot `S + i`
    set i := n - D.sources.size with hi
    have hni : n = D.slotOfNode i := by simp only [slotOfNode, hi]; omega
    have hiN : i < D.nodes.size := by omega
    obtain ⟨c, hc⟩ : ∃ c, D.nodes[i]? = some c :=
      ⟨D.nodes[i], Array.getElem?_eq_getElem hiN⟩
    obtain ⟨b, hb, hcb⟩ := compileGraph_binding D bs hbs i c hc
    -- the environment as it stood when binding i ran
    set ENVi := runBindings (bs.toList.take i) env0 with hENVi
    have hENVisz : ENVi.size = D.sources.size + i := by
      rw [hENVi, runBindings_size, List.length_take, Nat.min_eq_left (by omega), hsz0]
    -- φ at slot n IS binding i's value
    have hφn : φ n = denoteExpr ENVi b.rhs := by
      have hat := runBindings_at bs.toList env0 i (by omega)
      have hbl : bs.toList[i]? = some b := by simpa using hb
      rw [hbl, Option.map_some, ← hENV, ← hENVi, hsz0] at hat
      simp only [hφ, denoteRef, hni, slotOfNode, hat, Option.getD_some]
    -- every dep of node i is a slot strictly below n, so ENVi and ENV agree there
    have hagree : ∀ r ∈ c.deps.toList, denoteRef ENVi r = φ r := by
      intro r hr
      have hrlt : r < D.sources.size + i := hdb i c hc r hr
      have hag := runBindings_prefix_agree bs.toList env0 i r (by omega) (by omega)
      rw [← hENV, ← hENVi] at hag
      simp only [hφ, denoteRef, hag]
    -- and the node's semantics is exactly `compileOp`'s target, by step 5
    have hnode : evalNodeG D.toGraphCert φ n
        = eval_op_cert c.op c.width (c.deps.toList.map φ) := by
      simp only [evalNodeG, toGraphCert, hni, nodeAt?_slotOfNode, hc, Option.map_some]
      rfl
    rw [hφn, hnode]
    exact compileOp_correct (D.slotOfNode i) c ENVi φ b.rhs hcb hagree
  -- deps that leave the topo list are sources
  have hsrc : ∀ n ∈ D.toGraphCert.topo, ∀ d ∈ depopts_of D.toGraphCert n,
      d ∉ D.toGraphCert.topo → srcEnv D inp st d = φ d := by
    intro n hn d hd hdnot
    obtain ⟨hlo, hhi⟩ := mem_topo_iff.mp hn
    simp only [numSources, numSlots] at hlo hhi
    set i := n - D.sources.size with hi
    have hni : n = D.slotOfNode i := by simp only [slotOfNode, hi]; omega
    have hiN : i < D.nodes.size := by omega
    obtain ⟨c, hc⟩ : ∃ c, D.nodes[i]? = some c :=
      ⟨D.nodes[i], Array.getElem?_eq_getElem hiN⟩
    rw [hni, depopts_slotOfNode hc] at hd
    have hdlt : d < D.sources.size + i := hdb i c hc d hd
    have : d < D.sources.size := by
      by_cases hds : d < D.sources.size
      · exact hds
      · exact absurd (mem_topo_iff.mpr ⟨by simpa [numSources] using Nat.le_of_not_lt hds,
          by simp only [numSlots]; omega⟩) hdnot
    exact hsrcslot d this
  -- B2's fixpoint theorem does the induction
  intro n hn
  exact evalGraphG_of_localAgree D.toGraphCert φ (srcEnv D inp st)
    topo_nodup (wf_depOrdered hdb) wf_isSome hrec hsrc n hn


--------------------------------------------------------------------------------
-- Total slot agreement: for EVERY slot, not just topo slots.
--
-- The off-topo cases are the ones that bite in practice: a design output or a
-- flop `din` driven directly by a SOURCE rather than by a computed node.
--------------------------------------------------------------------------------

theorem slot_agree (D : DesignCert) (hdb : DesignCert.DepsBounded D)
    (bs : Array ResidualBinding) (hbs : compileGraph D = .ok bs)
    (inp : RuntimeInput) (st : RuntimeState) (k : Nat) :
    evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D inp st) k
      = denoteRef (runBindings bs.toList (sourceEnvArr D.sources inp st)) k := by
  by_cases hk : k ∈ D.topo
  · exact compileGraph_correct D hdb bs hbs inp st k hk
  · rw [evalGraphG_not_mem D.toGraphCert _ D.toGraphCert.topo k hk]
    by_cases hks : k < D.sources.size
    · exact srcEnv_agree D bs inp st k hks
    · -- beyond every slot: both sides are the out-of-range value
      have hkge : D.sources.size + D.nodes.size ≤ k := by
        by_cases hin : k < D.sources.size + D.nodes.size
        · exact absurd (mem_topo_iff.mpr ⟨by simpa [numSources] using Nat.le_of_not_lt hks,
              by simpa [numSlots] using hin⟩) hk
        · omega
      have hsz : (runBindings bs.toList (sourceEnvArr D.sources inp st)).size
          = D.sources.size + D.nodes.size := by
        rw [runBindings_size, sourceEnvArr_size, Array.length_toList,
          compileGraph_size D bs hbs]
      have hnone : (runBindings bs.toList (sourceEnvArr D.sources inp st))[k]? = none :=
        Array.getElem?_eq_none (by omega)
      simp only [srcEnv, Array.getElem?_eq_none (Nat.le_of_not_lt hks), denoteRef, hnone,
        Option.getD_none]

--------------------------------------------------------------------------------
-- Step 7: outputs and sequential state
--------------------------------------------------------------------------------

def compileOutput (o : OutputDesc) : ResidualOutput := { slot := o.slot, width := o.width }

/-- Every Flop field is carried across.  Dropping `resetValue` or flipping
`resetActiveLow` here is what `flopNext_agree` refuses to prove. -/
def compileFlop (f : FlopDesc) : ResidualFlopUpdate :=
  { width := f.width, din := f.din, enable := f.enable, resetPin := f.resetPin,
    resetValue := f.resetValue, resetActiveLow := f.resetActiveLow }

def compileMemory (m : MemoryDesc) : ResidualMemoryUpdate :=
  { aw := m.aw, dw := m.dw, nextImg := m.nextImg }

/-- Reset priority, reset polarity, reset value, enable behaviour, and the
old-state fallback — all five checked at once, against two independently
written rules (`xor` + `match` on the source side, nested `if`s on the target). -/
theorem flopNext_agree (rho : Nat → CertVal) (env : SlotEnv)
    (hag : ∀ k, rho k = denoteRef env k) (st : RuntimeState) (idx : Nat) (f : FlopDesc) :
    srcFlopNext rho st idx f = flopNext env st idx (compileFlop f) := by
  have hb : ∀ r, (rho r).asBV = refBV env r := by intro r; rw [hag r]; rfl
  have hxor : ∀ rv : Bool,
      xor f.resetActiveLow rv = (if f.resetActiveLow = true then !rv else rv) := by
    intro rv; cases f.resetActiveLow <;> simp
  simp only [srcFlopNext, flopNext, compileFlop, hb, hxor]
  rfl

--------------------------------------------------------------------------------
-- `compileDesign`
--------------------------------------------------------------------------------

/-- First dependency that is not strictly earlier, if any.  Checking this inside
`compileDesign` rather than assuming it means `hc : compileDesign D = .ok R`
WITNESSES dependency-ordering, so the final theorem needs no separate
well-formedness hypothesis for it. -/
def firstBadDep (D : DesignCert) : Option (Nat × Nat) :=
  (DesignCert.slotsFrom 0 D.nodes.size).findSome? fun i =>
    match D.nodes[i]? with
    | none   => none
    | some c => (c.deps.toList.find? fun d => decide ¬(d < D.sources.size + i)).map
                  fun d => (D.slotOfNode i, d)

def compileDesign (D : DesignCert) : Except CompileError ResidualProgram :=
  match firstBadDep D with
  | some (n, d) => .error (.depNotEarlier n d)
  | none =>
    match compileGraph D with
    | .error e => .error e
    | .ok bs =>
      .ok { sources       := D.sources
            bindings      := bs
            outputs       := D.outputs.map compileOutput
            flopUpdates   := D.flops.map compileFlop
            memoryUpdates := D.memories.map compileMemory }

theorem depsBounded_of_firstBadDep (D : DesignCert) (h : firstBadDep D = none) :
    DesignCert.DepsBounded D := by
  intro i c hc d hd
  have hi : i < D.nodes.size := by
    by_cases h1 : i < D.nodes.size
    · exact h1
    · rw [Array.getElem?_eq_none (Nat.le_of_not_lt h1)] at hc; exact absurd hc (by simp)
  have hmem : i ∈ DesignCert.slotsFrom 0 D.nodes.size :=
    (DesignCert.mem_slotsFrom (s := i) D.nodes.size 0).mpr ⟨Nat.zero_le _, by omega⟩
  rw [firstBadDep, List.findSome?_eq_none_iff] at h
  have hi2 := h i hmem
  rw [hc] at hi2
  simp only [Option.map_eq_none_iff, List.find?_eq_none] at hi2
  simpa using hi2 d hd

theorem compileDesign_parts (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) :
    DesignCert.DepsBounded D
    ∧ compileGraph D = .ok R.bindings
    ∧ R.sources = D.sources
    ∧ R.outputs = D.outputs.map compileOutput
    ∧ R.flopUpdates = D.flops.map compileFlop
    ∧ R.memoryUpdates = D.memories.map compileMemory := by
  simp only [compileDesign] at hc
  split at hc
  case _ p hbad => exact absurd hc (by simp)
  case _ hnone =>
    split at hc
    case _ e he => exact absurd hc (by simp)
    case _ bs hbs =>
      injection hc with hR; subst hR
      exact ⟨depsBounded_of_firstBadDep D hnone, hbs, rfl, rfl, rfl, rfl⟩

--------------------------------------------------------------------------------
-- The theorem
--------------------------------------------------------------------------------

/-- **`compileDesign_correct`.**  For every `DesignCert` the compiler ACCEPTS,
the compiled program means exactly what the certificate means — at every input
and every state.

Note what is absent: no `DesignCertWF` hypothesis and no `RuntimeWF` hypothesis.
`compileDesign` checks dependency-ordering itself (`firstBadDep`) and refuses
unsupported operators, arities and zero widths (`compileOp`), so `hc` witnesses
everything the proof needs; and neither side's output, flop or memory rule
depends on the state's size, so no runtime shape condition is required either.
Fewer hypotheses is strictly more theorem. -/
theorem compileDesign_correct (D : DesignCert) (R : ResidualProgram)
    (hc : compileDesign D = .ok R) :
    ∀ (inp : RuntimeInput) (st : RuntimeState),
      denoteResidual R inp st = interpretDesign D inp st := by
  obtain ⟨hdb, hbs, hsrcs, houts, hflops, hmems⟩ := compileDesign_parts D R hc
  intro inp st
  set env := runBindings R.bindings.toList (sourceEnvArr D.sources inp st) with henv
  -- the ONE fact that does all the work: both sides read every slot alike
  have hrho : evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D inp st) = denoteRef env := by
    funext k; rw [henv]; exact slot_agree D hdb R.bindings hbs inp st k
  simp only [denoteResidual, interpretDesign, hsrcs, houts, hflops, hmems, ← henv, hrho]
  congr 1
  · -- next state
    congr 1
    · -- flops: reset priority / polarity / value / enable / old-state fallback
      apply Array.ext
      · simp
      · intro i h1 h2
        have hi : i < D.flops.size := by simpa using h2
        simp only [Array.getElem_mapIdx, Array.getElem_map]
        exact (flopNext_agree (denoteRef env) env (fun _ => rfl) st i D.flops[i]).symm
    · -- memories: the post-write image slot
      apply Array.ext
      · simp
      · intro i h1 h2
        simp only [Array.getElem_map, compileMemory, refMem]
  · -- outputs
    apply Array.ext
    · simp
    · intro i h1 h2
      simp only [Array.getElem_map, compileOutput, refBV]

/-- Boolean "did it compile?".  Its own definition rather than an `Except`
helper, so the `native_decide` target is a single constructor test and does not
depend on which spelling the library happens to provide. -/
def compilesOk (D : DesignCert) : Bool :=
  match compileDesign D with
  | .ok _    => true
  | .error _ => false

/-- Witness the `.ok` side condition from that BOOLEAN check.

Discharging `compileDesign D = .ok <Top>_residual` directly asks the evaluator to
build *two* `ResidualProgram`s and compare them field by field with
`DecidableEq`.  `compilesOk` is one constructor test, and this lemma turns it
back into the equation `compileDesign_correct` wants. -/
theorem compileDesign_ok_witness (D : DesignCert) (h : compilesOk D = true) :
    compileDesign D = .ok (match compileDesign D with | .ok R => R | .error _ => default) := by
  unfold compilesOk at h
  cases hc : compileDesign D with
  | error e => rw [hc] at h; exact absurd h (by simp)
  | ok R    => simp only [hc]

--------------------------------------------------------------------------------
-- The generated-file interface.
--
-- WHY THIS EXISTS.  The obvious shape for a generated design is
--
--     def <Top>_residual  := match compileDesign <Top>_designCert with ...
--     theorem <Top>_compiles : compileDesign <Top>_designCert = .ok <Top>_residual := ...
--
-- and it is a trap.  The second declaration's STATEMENT names a definition whose
-- body is a `match` on `compileDesign <Top>_designCert`, so type-checking it asks
-- the KERNEL to decide `.ok <Top>_residual` defeq `.ok (match compileDesign D …)`.
-- The kernel does not stop at a delta step: it reduces `compileDesign D`, and
-- `Array.push` is `⟨as.toList ++ [a]⟩`, so building 4,772 bindings costs O(N^2)
-- list cells *as kernel terms*.  Measured on `SingleCycleCPU`: >1 h and 120 GB
-- before it was killed, against 38 s / 7.4 GB for the same design when no theorem
-- names a `ResidualProgram`.
--
-- So: no `ResidualProgram` ever appears in a theorem statement.  `compileAndRun`
-- keeps it inside a function body, the witness is a Bool, and the generated
-- theorem is one delta-unfold away from `compileAndRun_correct`.
--------------------------------------------------------------------------------

/-- Compile and run, in one total function.  A design the compiler refuses
returns `default` rather than being a partial function. -/
def compileAndRun (D : DesignCert) (inp : RuntimeInput) (st : RuntimeState) : RuntimeResult :=
  match compileDesign D with
  | .ok R    => denoteResidual R inp st
  | .error _ => default

/-- The whole per-design obligation, reduced to ONE boolean check.

`cases hc : compileDesign D` generalizes the compiler's result instead of
evaluating it, so neither this proof nor its instantiation ever reduces
`compileDesign` in the kernel. -/
theorem compileAndRun_correct (D : DesignCert) (h : compilesOk D = true) :
    ∀ inp st, compileAndRun D inp st = interpretDesign D inp st := by
  intro inp st
  unfold compileAndRun
  unfold compilesOk at h
  cases hc : compileDesign D with
  | error e => rw [hc] at h; exact absurd h (by simp)
  | ok R    => simpa using compileDesign_correct D R hc inp st

end Compiler
