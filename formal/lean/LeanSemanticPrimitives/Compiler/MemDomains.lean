/-
# Which clock domains of a memory fire (DCERT3, decision #9)

A memory on more than one clock domain carries a commit table
(`MemoryDesc.commits`).  These predicates say which of its domains fire in a
step, and are shared by the source rule (`srcMemNext`, `DesignSemantics`) and
every target that mirrors it (TV's `memNext`, `ResidualSemantics`), so "which
domains fired" is spelled once.  They read only `clock` and `commits`.
-/
import LeanSemanticPrimitives.Compiler.Runtime

namespace Compiler

/-- Is clock ordinal `k` one of the domains memory `m` commits on: its `clock`,
or any ordinal an entry of its commit table names (DCERT3, decision #9). -/
def MemoryDesc.inDom (m : MemoryDesc) (k : Nat) : Bool :=
  k == m.clock || m.commits.any (fun c => c.mask.testBit k)

/-- Every domain of `m` is below this ordinal: the ordinals to inspect. -/
def MemoryDesc.domSpan (m : MemoryDesc) : Nat :=
  m.commits.foldl (fun a c => max a (c.mask.log2 + 1)) (m.clock + 1)

/-- Every domain of `m` fires this step. -/
def MemoryDesc.allFire (m : MemoryDesc) (e : ClockEdges) : Bool :=
  (List.range m.domSpan).all fun k => !m.inDom k || fires e k

/-- Commit-table entry `c` names EXACTLY the domains of `m` that fire. -/
def MemoryDesc.firedIs (m : MemoryDesc) (e : ClockEdges) (c : MemCommit) : Bool :=
  (List.range m.domSpan).all fun k => c.mask.testBit k == (m.inDom k && fires e k)

/-- Every domain fires when every DECLARED clock fires and the memory names only
declared ones. -/
theorem allFire_allEdges {D : DesignCert} {m : MemoryDesc}
    (hm : m.clock < D.clocks.size)
    (hc : ∀ c ∈ m.commits, c.mask < 2 ^ D.clocks.size) :
    m.allFire (allEdges D) = true := by
  simp only [MemoryDesc.allFire, List.all_eq_true, List.mem_range]
  intro k _
  by_cases hk : m.inDom k = true
  · simp only [MemoryDesc.inDom, Bool.or_eq_true, beq_iff_eq, Array.any_eq_true] at hk
    rcases hk with hk | ⟨i, hi, hb⟩
    · subst hk; simp [fires_allEdges hm]
    · -- a set bit of a mask below `2 ^ n` names a clock below `n`
      have hk' : k < D.clocks.size := by
        refine Nat.lt_of_not_le fun hle => ?_
        have hlt := Nat.lt_of_lt_of_le (hc _ (Array.getElem_mem hi))
          (Nat.pow_le_pow_right (by decide) hle)
        rw [Nat.testBit_lt_two_pow hlt] at hb
        exact Bool.false_ne_true hb
      simp [fires_allEdges hk']
  · simp [hk]

end Compiler
