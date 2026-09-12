/-
  General graph-refinement theorem for the pass.lean fast-view bridge (step 5).

  `evalGraph` (the certificate evaluator, `LGraphModel.lean`) computes the unique
  topo fixpoint of a dependency-ordered graph.  Hence ANY environment `φ` that
  (a) agrees with the source environment on off-topo dependencies and (b)
  satisfies the per-node local recurrence `φ n = evalNode G φ n` equals
  `evalGraph` on every topo node.

  This is the design- and operator-independent core of the fast-model ⇔
  certificate bridge: a generated design proves step 5 by instantiating φ with
  its (encoded) fast-model node values and discharging the recurrence with the
  per-operator bridge lemmas (`OpBridge.lean`).
-/

import LeanSemanticPrimitives.Translation.LGraphModel

namespace GraphRefine

--------------------------------------------------------------------------------
-- envSet lookup helpers (used by generated per-node bridge proofs)
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
-- Dependency ordering: every node's deps lie strictly before it.
--
-- VALUE-FREE.  These read `depopts_of`, i.e. `NodeCert` (nid, op, width, deps),
-- and never touch a value -- so they are shared by every instance of the
-- traversal below, and with them the `native_decide` gates that discharge them.
--------------------------------------------------------------------------------

/-- A topo list is dependency-ordered when the head's deps are outside the
current `head :: tail` sublist (already processed, or sources) and the tail is
itself dependency-ordered. -/
def DepOrdered (G : GraphCert) : List Nat → Prop
  | [] => True
  | n :: ns => (∀ d ∈ depopts_of G n, d ∉ (n :: ns)) ∧ DepOrdered G ns

/-- Decidable `Bool` mirror of `DepOrdered`, so a concrete generated graph can
discharge dependency-ordering by `native_decide`. -/
def DepOrderedB (G : GraphCert) : List Nat → Bool
  | [] => true
  | n :: ns => (depopts_of G n).all (fun d => decide (d ∉ (n :: ns))) && DepOrderedB G ns

theorem depOrdered_of_bool (G : GraphCert) : ∀ l, DepOrderedB G l = true → DepOrdered G l := by
  intro l
  induction l with
  | nil => intro _; trivial
  | cons n ns ih =>
      intro h
      simp only [DepOrderedB, Bool.and_eq_true] at h
      refine ⟨?_, ih h.2⟩
      intro d hd
      have hall := (List.all_eq_true.mp h.1) d hd
      exact of_decide_eq_true hall

--------------------------------------------------------------------------------
-- ONE traversal, proved once, over any value domain.
--
-- Everything below is stated at `evalGraphG` / `evalNodeG` / `envSetG` with a
-- `[NodeSemantics V]` instance.  The `BV` and `CertVal` theorems the generated
-- designs use are corollaries at the two instances -- previously two hand-copied
-- proof scripts, ~150 lines of duplication.
--
-- `DepOrdered` / `DepOrderedB` / `depOrdered_of_bool` are NOT parameterized:
-- they quantify over `depopts_of`, which reads `NodeCert` (nid, op, width, deps)
-- and never touches a value.  Both instances share them, and with them the very
-- same `native_decide` gates.
--------------------------------------------------------------------------------

section Generic
variable {V : Type} [NodeSemantics V]

@[simp] theorem envSetG_eq (rho : Nat → V) (n : Nat) (v : V) :
    envSetG rho n v n = v := by
  unfold envSetG; simp

theorem envSetG_ne (rho : Nat → V) (n m : Nat) (v : V) (h : m ≠ n) :
    envSetG rho n v m = rho m := by
  unfold envSetG; simp [h]

theorem evalNodeG_congr_some (G : GraphCert) (e f : Nat → V) (n : Nat)
    (hsome : (G.nodes n).isSome)
    (h : ∀ d ∈ depopts_of G n, e d = f d) :
    evalNodeG G e n = evalNodeG G f n := by
  unfold evalNodeG
  cases hn : G.nodes n with
  | none => rw [hn] at hsome; exact absurd hsome (by decide)
  | some c =>
      have hdeps : ∀ d ∈ c.deps, e d = f d := by
        intro d hd
        exact h d (by unfold depopts_of; simp [hn]; exact hd)
      show NodeSemantics.interpOp c.op c.width (c.deps.map e)
         = NodeSemantics.interpOp c.op c.width (c.deps.map f)
      rw [List.map_congr_left hdeps]

/-- Evaluating a dependency-ordered, `Nodup` suffix `ns` from an environment `e`
(that already carries φ on every dep lying outside `ns`) yields φ on `ns` and
leaves `e` untouched off `ns`, when φ satisfies the local recurrence on `ns`. -/
theorem evalGraphG_char (G : GraphCert) (φ : Nat → V) :
    ∀ (ns : List Nat) (e : Nat → V),
      ns.Nodup →
      DepOrdered G ns →
      (∀ n ∈ ns, (G.nodes n).isSome) →
      (∀ n ∈ ns, φ n = evalNodeG G φ n) →
      (∀ n ∈ ns, ∀ d ∈ depopts_of G n, d ∉ ns → e d = φ d) →
      ∀ m, evalGraphG ns G e m = (if m ∈ ns then φ m else e m) := by
  intro ns
  induction ns with
  | nil => intro e _ _ _ _ _ m; simp [evalGraphG]
  | cons n ns ih =>
      intro e hnodup hdepord hsome hrec hdeps m
      simp only [evalGraphG]
      have hn_notin : n ∉ ns := by simpa using (List.nodup_cons.mp hnodup).1
      have hdepord_head : ∀ d ∈ depopts_of G n, d ∉ (n :: ns) := hdepord.1
      have hdepord_tail : DepOrdered G ns := hdepord.2
      have hnode : evalNodeG G e n = φ n := by
        have hagree : ∀ d ∈ depopts_of G n, e d = φ d := by
          intro d hd
          exact hdeps n (by simp) d hd (hdepord_head d hd)
        have := evalNodeG_congr_some G e φ n (hsome n (by simp)) hagree
        rw [this]; exact (hrec n (by simp)).symm
      have htail_deps : ∀ n' ∈ ns, ∀ d ∈ depopts_of G n',
          d ∉ ns → (envSetG e n (evalNodeG G e n)) d = φ d := by
        intro n' hn' d hd hdnotin
        unfold envSetG
        by_cases hdn : d = n
        · simp [hdn, hnode]
        · simp only [if_neg hdn]
          have hdnotincons : d ∉ (n :: ns) := by
            intro hc; cases List.mem_cons.mp hc with
            | inl h => exact hdn h
            | inr h => exact hdnotin h
          exact hdeps n' (by simp [hn']) d hd hdnotincons
      have hnn : ∀ n' ∈ ns, (G.nodes n').isSome := fun n' h => hsome n' (by simp [h])
      have hrn : ∀ n' ∈ ns, φ n' = evalNodeG G φ n' := fun n' h => hrec n' (by simp [h])
      have hnodup_tail : ns.Nodup := (List.nodup_cons.mp hnodup).2
      have key := ih (envSetG e n (evalNodeG G e n)) hnodup_tail hdepord_tail hnn hrn htail_deps m
      rw [key]
      by_cases hm_ns : m ∈ ns
      · simp [hm_ns, List.mem_cons]
      · by_cases hmn : m = n
        · subst hmn
          simp only [if_neg hm_ns]
          unfold envSetG
          simp [hnode, List.mem_cons]
        · simp only [if_neg hm_ns]
          unfold envSetG; simp only [if_neg hmn]
          have hmnotin : m ∉ (n :: ns) := by
            intro hc; cases List.mem_cons.mp hc with
            | inl h => exact hmn h
            | inr h => exact hm_ns h
          simp [hmnotin]

/-- Uniqueness of the topo fixpoint: any φ satisfying the local recurrence and
agreeing with the source env off-topo IS `evalGraphG`.  This is the theorem every
generated design instantiates for step 5. -/
theorem evalGraphG_of_localAgree (G : GraphCert) (φ src : Nat → V)
    (hnodup : G.topo.Nodup)
    (hdepord : DepOrdered G G.topo)
    (hsome : ∀ n ∈ G.topo, (G.nodes n).isSome)
    (hrec : ∀ n ∈ G.topo, φ n = evalNodeG G φ n)
    (hsrc : ∀ n ∈ G.topo, ∀ d ∈ depopts_of G n, d ∉ G.topo → src d = φ d) :
    ∀ n ∈ G.topo, evalGraphG G.topo G src n = φ n := by
  intro n hn
  have := evalGraphG_char G φ G.topo src hnodup hdepord hsome hrec
    (by intro n' hn' d hd hdnotin; exact hsrc n' hn' d hd hdnotin) n
  rw [this]; simp [hn]

/-- `evalGraphG` only updates ids in the topo list, so an id outside it reads
through to the base environment.  Needed when a design output (or a flop din) is
driven directly by a SOURCE rather than by a computed node. -/
theorem evalGraphG_not_mem (G : GraphCert) (rho : Nat → V) :
    ∀ (ns : List Nat) (d : Nat), d ∉ ns → evalGraphG ns G rho d = rho d := by
  intro ns
  induction ns generalizing rho with
  | nil => intro d _; rfl
  | cons n ns ih =>
    intro d hd
    simp only [List.mem_cons, not_or] at hd
    show evalGraphG ns G (envSetG rho n (evalNodeG G rho n)) d = rho d
    rw [ih _ d hd.2]
    simp only [envSetG, if_neg hd.1]

end Generic

--------------------------------------------------------------------------------
-- The two instances.  Each is the generic theorem at one value domain; the
-- aliases in LGraphModel are definitional, so `exact` closes every one.
--------------------------------------------------------------------------------

@[simp] theorem envSet_eq (rho : Nat → BV) (n : Nat) (v : BV) :
    envSet rho n v n = v := envSetG_eq rho n v

theorem envSet_ne (rho : Nat → BV) (n m : Nat) (v : BV) (h : m ≠ n) :
    envSet rho n v m = rho m := envSetG_ne rho n m v h

theorem evalNode_congr_some (G : GraphCert) (e f : Nat → BV) (n : Nat)
    (hsome : (G.nodes n).isSome) (h : ∀ d ∈ depopts_of G n, e d = f d) :
    evalNode G e n = evalNode G f n := evalNodeG_congr_some G e f n hsome h

theorem evalGraph_char (G : GraphCert) (φ : Nat → BV) :
    ∀ (ns : List Nat) (e : Nat → BV),
      ns.Nodup → DepOrdered G ns → (∀ n ∈ ns, (G.nodes n).isSome) →
      (∀ n ∈ ns, φ n = evalNode G φ n) →
      (∀ n ∈ ns, ∀ d ∈ depopts_of G n, d ∉ ns → e d = φ d) →
      ∀ m, evalGraph ns G e m = (if m ∈ ns then φ m else e m) :=
  evalGraphG_char G φ

theorem evalGraph_of_localAgree (G : GraphCert) (φ src : Nat → BV)
    (hnodup : G.topo.Nodup) (hdepord : DepOrdered G G.topo)
    (hsome : ∀ n ∈ G.topo, (G.nodes n).isSome)
    (hrec : ∀ n ∈ G.topo, φ n = evalNode G φ n)
    (hsrc : ∀ n ∈ G.topo, ∀ d ∈ depopts_of G n, d ∉ G.topo → src d = φ d) :
    ∀ n ∈ G.topo, evalGraph G.topo G src n = φ n :=
  evalGraphG_of_localAgree G φ src hnodup hdepord hsome hrec hsrc

theorem evalGraph_not_mem (G : GraphCert) (rho : Nat → BV) :
    ∀ (ns : List Nat) (d : Nat), d ∉ ns → evalGraph ns G rho d = rho d :=
  evalGraphG_not_mem G rho

@[simp] theorem envSetC_eq (rho : Nat → CertVal) (n : Nat) (v : CertVal) :
    envSetC rho n v n = v := envSetG_eq rho n v

theorem envSetC_ne (rho : Nat → CertVal) (n m : Nat) (v : CertVal) (h : m ≠ n) :
    envSetC rho n v m = rho m := envSetG_ne rho n m v h

theorem evalNodeC_congr_some (G : GraphCert) (e f : Nat → CertVal) (n : Nat)
    (hsome : (G.nodes n).isSome) (h : ∀ d ∈ depopts_of G n, e d = f d) :
    evalNodeC G e n = evalNodeC G f n := evalNodeG_congr_some G e f n hsome h

theorem evalGraphC_char (G : GraphCert) (φ : Nat → CertVal) :
    ∀ (ns : List Nat) (e : Nat → CertVal),
      ns.Nodup → DepOrdered G ns → (∀ n ∈ ns, (G.nodes n).isSome) →
      (∀ n ∈ ns, φ n = evalNodeC G φ n) →
      (∀ n ∈ ns, ∀ d ∈ depopts_of G n, d ∉ ns → e d = φ d) →
      ∀ m, evalGraphC ns G e m = (if m ∈ ns then φ m else e m) :=
  evalGraphG_char G φ

theorem evalGraphC_of_localAgree (G : GraphCert) (φ src : Nat → CertVal)
    (hnodup : G.topo.Nodup) (hdepord : DepOrdered G G.topo)
    (hsome : ∀ n ∈ G.topo, (G.nodes n).isSome)
    (hrec : ∀ n ∈ G.topo, φ n = evalNodeC G φ n)
    (hsrc : ∀ n ∈ G.topo, ∀ d ∈ depopts_of G n, d ∉ G.topo → src d = φ d) :
    ∀ n ∈ G.topo, evalGraphC G.topo G src n = φ n :=
  evalGraphG_of_localAgree G φ src hnodup hdepord hsome hrec hsrc

theorem evalGraphC_not_mem (G : GraphCert) (rho : Nat → CertVal) :
    ∀ (ns : List Nat) (d : Nat), d ∉ ns → evalGraphC ns G rho d = rho d :=
  evalGraphG_not_mem G rho

end GraphRefine
