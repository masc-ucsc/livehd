/-
# `DesignCertWF` — one arithmetic condition replaces three `native_decide` gates

Step 2 (second half).

Every step-5 proof on the manual path discharges three structural facts per
design by `native_decide`:

  * `G.topo.Nodup`
  * `DepOrdered G G.topo`
  * `∀ n ∈ G.topo, (G.nodes n).isSome`

With `GraphCert`'s arbitrary LGraph ids there is no cheaper way — `DepOrdered`
quantifies over list membership, so it is genuinely a search.

Dense slot indices collapse all three.  `topo_nodup` and `wf_isSome` hold
*unconditionally* (they are facts about `slotsFrom` and `Array.getElem?`), and
`DepOrdered` follows from the single condition

    DepsBounded D  :  every dep of node `i` is `< sources.size + i`

which is what "the exporter emitted a topological order" means in the dense
space.  So the per-design obligation shrinks from three graph searches to one
bounds check, and `depsBoundedB` decides it in one linear pass.
-/
import LeanSemanticPrimitives.Compiler.DesignCert
import LeanSemanticPrimitives.Translation.GraphRefine

open GraphRefine

namespace Compiler
namespace DesignCert

--------------------------------------------------------------------------------
-- The condition
--------------------------------------------------------------------------------

/-- Every dependency of node `i` names a strictly earlier slot.  This is the
whole of "dependency-ordered" in the dense space. -/
def DepsBounded (D : DesignCert) : Prop :=
  ∀ (i : Nat) (c : DenseNodeCert), D.nodes[i]? = some c →
    ∀ d ∈ c.deps.toList, d < D.sources.size + i

/-- Outputs, flop pins and memory images must all name real slots. -/
def SlotsInRange (D : DesignCert) : Prop :=
  (∀ o ∈ D.outputs.toList, o.slot < D.numSlots) ∧
  (∀ f ∈ D.flops.toList, f.din < D.numSlots ∧
     (∀ e, f.enable = some e → e < D.numSlots) ∧
     (∀ r, f.resetPin = some r → r < D.numSlots)) ∧
  (∀ m ∈ D.memories.toList, m.nextImg < D.numSlots)

structure DesignCertWF (D : DesignCert) : Prop where
  depsBounded  : DepsBounded D
  slotsInRange : SlotsInRange D

--------------------------------------------------------------------------------
-- Decidable mirror
--------------------------------------------------------------------------------

def depsBoundedB (D : DesignCert) : Bool :=
  (slotsFrom 0 D.nodes.size).all fun i =>
    match D.nodes[i]? with
    | none   => true
    | some c => c.deps.toList.all fun d => decide (d < D.sources.size + i)

theorem depsBounded_of_bool (D : DesignCert) (h : depsBoundedB D = true) :
    DepsBounded D := by
  intro i c hc d hd
  have hi : i < D.nodes.size := by
    by_cases h1 : i < D.nodes.size
    · exact h1
    · rw [Array.getElem?_eq_none (Nat.le_of_not_lt h1)] at hc
      exact absurd hc (by simp)
  have hmem : i ∈ slotsFrom 0 D.nodes.size :=
    (mem_slotsFrom (s := i) D.nodes.size 0).mpr ⟨Nat.zero_le _, by omega⟩
  have hall := (List.all_eq_true.mp h) i hmem
  rw [hc] at hall
  exact of_decide_eq_true ((List.all_eq_true.mp hall) d hd)

--------------------------------------------------------------------------------
-- The three structural facts
--------------------------------------------------------------------------------

variable {D : DesignCert}

/-- Unconditional: every topo slot really carries a node. -/
theorem wf_isSome : ∀ n ∈ D.topo, (D.toGraphCert.nodes n).isSome := by
  intro n hn
  have h := mem_topo_iff.mp hn
  simp only [numSources, numSlots] at h
  simp only [toGraphCert, nodeAt?, Nat.not_lt.mpr h.1, if_false, Option.isSome_map]
  rw [Array.getElem?_eq_getElem (by omega)]
  rfl

/-- `depopts_of` at a topo slot is exactly that node's dep array. -/
theorem depopts_slotOfNode {i : Nat} {c : DenseNodeCert} (hc : D.nodes[i]? = some c) :
    depopts_of D.toGraphCert (D.slotOfNode i) = c.deps.toList := by
  simp only [depopts_of, toGraphCert, nodeAt?_slotOfNode, hc, Option.map_some]

/-- The generalized statement: every suffix of topo is dependency-ordered.
Induction on the suffix length, generalizing the starting node index. -/
theorem depOrdered_slotsFrom (hdb : DepsBounded D) :
    ∀ (k j : Nat), DepOrdered D.toGraphCert (slotsFrom (D.sources.size + j) k) := by
  intro k
  induction k with
  | zero => intro j; trivial
  | succ k ih =>
      intro j
      refine ⟨?_, ?_⟩
      · -- the head's deps lie outside the whole remaining suffix
        intro d hd hcontra
        -- what are the head's deps?
        have hslot : D.sources.size + j = D.slotOfNode j := rfl
        have hlt : d < D.sources.size + j := by
          cases hnode : D.nodes[j]? with
          | none =>
              rw [depopts_of, toGraphCert] at hd
              simp only [hslot, nodeAt?_slotOfNode, hnode] at hd
              exact absurd hd (by simp)
          | some c =>
              rw [hslot, depopts_slotOfNode hnode] at hd
              exact hdb j c hnode d hd
        -- membership in `(S+j) :: slotsFrom (S+j+1) k` forces `d ≥ S+j`
        cases List.mem_cons.mp hcontra with
        | inl heq => omega
        | inr hin =>
            have := (mem_slotsFrom (s := d) k (D.sources.size + j + 1)).mp hin
            omega
      · -- the tail, at index `j+1`
        have : D.sources.size + j + 1 = D.sources.size + (j + 1) := by omega
        rw [this]
        exact ih (j + 1)

theorem wf_depOrdered (hdb : DepsBounded D) : DepOrdered D.toGraphCert D.topo := by
  have h := depOrdered_slotsFrom hdb D.nodes.size 0
  simpa [topo] using h

/-- The three facts `evalGraphG_of_localAgree` needs, packaged. -/
theorem wf_graph_facts (hwf : DesignCertWF D) :
    D.toGraphCert.topo.Nodup ∧
    DepOrdered D.toGraphCert D.toGraphCert.topo ∧
    (∀ n ∈ D.toGraphCert.topo, (D.toGraphCert.nodes n).isSome) :=
  ⟨topo_nodup, wf_depOrdered hwf.depsBounded, wf_isSome⟩

end DesignCert
end Compiler
