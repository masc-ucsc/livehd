/-
# `DesignCert` — a finite, dense certificate for a whole design

Step 2 of the B1+B2 verified-compiler plan.

The existing `GraphCert` carries `nodes : Nat → Option NodeCert` and a `topo`
list of arbitrary LGraph ids.  That shape is fine for a *spec* but it makes the
three structural facts every step-5 proof needs (`topo.Nodup`, `DepOrdered`,
`isSome` on topo) into per-design `native_decide` obligations, and it makes a
dependency lookup a list search.

`DesignCert` uses **one flat, dense slot index space**:

  * slots `0 .. sources.size-1`            are SOURCES (inputs, flop Qs, memory images)
  * slot  `sources.size + i`               is node `i`

so "my dependencies precede me" becomes the single arithmetic condition
`d < sources.size + i` (`DepsBounded`), and *all three* structural facts follow
from it as theorems (`wf_nodup`, `wf_depOrdered`, `wf_isSome`) rather than being
checked per design.  Original LGraph ids survive only as debugging metadata.
-/
import LeanSemanticPrimitives.Translation.LGraphModel

namespace Compiler

--------------------------------------------------------------------------------
-- Descriptors
--------------------------------------------------------------------------------

/-- What a source slot reads at the START of a cycle.  These are the only slots
whose value does not come from an operator. -/
inductive SourceDesc where
  /-- primary input `idx` of the design -/
  | input  (idx : Nat) (width : Nat)
  /-- a constant driver.  Constants are certificate SOURCES, never node ops —
  which is why the census finds `Op_Const` in zero node positions. -/
  | const  (width : Nat) (value : Int)
  /-- current (pre-edge) value of flop `idx` -/
  | flopQ  (idx : Nat) (width : Nat)
  /-- current (pre-edge) image of memory `idx` -/
  | memImg (idx : Nat) (aw dw : Nat)
deriving Repr, Inhabited, DecidableEq

/-- A node in the dense space.  `deps` are GLOBAL slot indices. -/
structure DenseNodeCert where
  op    : LGraphOp
  width : Nat
  deps  : Array Nat
  /-- original LGraph id — debugging metadata ONLY, never read by semantics. -/
  origin : Nat := 0
deriving Repr, Inhabited, DecidableEq

structure OutputDesc where
  slot  : Nat
  width : Nat
deriving Repr, Inhabited, DecidableEq

/-- Every pin `graph/cell.cpp` gives a Flop, not just the three the manual
emitter reads.  `resetValue` and `resetActiveLow` exist precisely because the
manual emitter dropped `initial` and conflated `negreset`; transcribing the
emitter instead would turn that bug into a theorem. -/
structure FlopDesc where
  width          : Nat
  /-- slot driving `din` -/
  din            : Nat
  enable         : Option Nat
  resetPin       : Option Nat
  /-- value loaded on reset (the `initial` pin; 0 when absent) -/
  resetValue     : Int
  /-- `true` for `negreset` (active low), `false` for `reset_pin` (active high) -/
  resetActiveLow : Bool
deriving Repr, Inhabited, DecidableEq

/-- A memory's next image is the slot holding the last write of its write chain.
Reads are ordinary graph nodes, so nothing about them appears here. -/
structure MemoryDesc where
  aw      : Nat
  dw      : Nat
  nextImg : Nat
deriving Repr, Inhabited, DecidableEq

/-- The whole design. -/
structure DesignCert where
  sources  : Array SourceDesc
  nodes    : Array DenseNodeCert
  outputs  : Array OutputDesc
  flops    : Array FlopDesc
  memories : Array MemoryDesc
deriving Repr, Inhabited

--------------------------------------------------------------------------------
-- The slot space
--------------------------------------------------------------------------------

namespace DesignCert
variable (D : DesignCert)

@[reducible] def numSources : Nat := D.sources.size
@[reducible] def numNodes   : Nat := D.nodes.size
@[reducible] def numSlots   : Nat := D.sources.size + D.nodes.size

/-- Global slot of node `i`. -/
@[reducible] def slotOfNode (i : Nat) : Nat := D.sources.size + i

/-- Node occupying global slot `s`, if any.  O(1) — this is the dense payoff. -/
def nodeAt? (s : Nat) : Option DenseNodeCert :=
  if s < D.sources.size then none else D.nodes[s - D.sources.size]?

/-- Topological order: dense indices are already topological by construction.
Spelled as an explicit recursion rather than `(List.range n).map (S + ·)` so that
`mem_topo_iff` and `topo_nodup` are provable with core only — `GraphRefine` is
deliberately Mathlib-free and this file stays that way, so a generated design
pays no Mathlib import for the definitions it instantiates. -/
def slotsFrom (base : Nat) : Nat → List Nat
  | 0     => []
  | k + 1 => base :: slotsFrom (base + 1) k

def topo : List Nat := slotsFrom D.sources.size D.nodes.size

/-- Projection into the existing `GraphCert`, so B2's evaluator and every proof
in `GraphRefine` are reused rather than re-derived. -/
def toGraphCert : GraphCert where
  topo    := D.topo
  sources := List.range D.sources.size
  nodes   := fun s => (D.nodeAt? s).map fun c =>
    { nid := s, op := c.op, width := c.width, deps := c.deps.toList }

end DesignCert

--------------------------------------------------------------------------------
-- Slot-space lemmas
--------------------------------------------------------------------------------

namespace DesignCert
variable {D : DesignCert}

theorem nodeAt?_source {s : Nat} (h : s < D.numSources) : D.nodeAt? s = none := by
  simp [nodeAt?, h]

theorem nodeAt?_slotOfNode {i : Nat} : D.nodeAt? (D.slotOfNode i) = D.nodes[i]? := by
  simp [nodeAt?, slotOfNode, Nat.not_lt.mpr (Nat.le_add_right _ _)]

theorem mem_slotsFrom {s : Nat} : ∀ (k b : Nat),
    s ∈ slotsFrom b k ↔ (b ≤ s ∧ s < b + k) := by
  intro k
  induction k with
  | zero => intro b; simp [slotsFrom]
  | succ k ih =>
      intro b
      simp only [slotsFrom, List.mem_cons, ih (b + 1)]
      omega

theorem slotsFrom_nodup : ∀ (k b : Nat), (slotsFrom b k).Nodup := by
  intro k
  induction k with
  | zero => intro b; simp [slotsFrom]
  | succ k ih =>
      intro b
      refine List.nodup_cons.mpr ⟨?_, ih (b + 1)⟩
      intro hc
      have := (mem_slotsFrom k (b + 1)).mp hc
      omega

theorem mem_topo_iff {s : Nat} :
    s ∈ D.topo ↔ (D.numSources ≤ s ∧ s < D.numSlots) := by
  simpa [topo, numSources, numSlots] using
    (mem_slotsFrom (s := s) D.nodes.size D.sources.size)

theorem topo_nodup : D.topo.Nodup := slotsFrom_nodup _ _

/-- Node `i` is in topo when `i` is in range. -/
theorem slotOfNode_mem_topo {i : Nat} (h : i < D.numNodes) : D.slotOfNode i ∈ D.topo := by
  simp only [numNodes] at h
  exact mem_topo_iff.mpr ⟨Nat.le_add_right _ _, by simp only [numSlots, slotOfNode]; omega⟩

/-- Sources are never in topo. -/
theorem source_not_mem_topo {d : Nat} (h : d < D.numSources) : d ∉ D.topo := by
  intro hc; exact absurd (mem_topo_iff.mp hc).1 (by omega)

end DesignCert
end Compiler
