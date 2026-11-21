/-
# `lhdcert-ckpt v1` on the FP route

The FP runner (`dcert2_memroute`) keeps each memory as a full table of `2 ^ aw` words
(`MemImg`) inside a `StateM`. This module converts a checkpoint to that state and back:

* `toStateM c`: the checkpoint's flops, and each memory's words below `depth`, its default
  everywhere else in the table and outside it.
* `saveM D hash cycle exts st`: the checkpoint of the state `st` denotes (`StateM.denote`), so
  the FP route writes the same bytes as the IR and VC routes for the same state.

**T6** `toStateM_denote`: the state built from a checkpoint denotes `restore c`, the state the IR
and VC routes start from, when each memory's depth fits its table (`depth ≤ 2 ^ aw`, which a
DCERT4 certificate's `depthError` check and `validate`'s shape rule give).

Mathlib-free; the axioms are audited at the end.
-/
import LeanSemanticPrimitives.Compiler.CkptSound
import LeanSemanticPrimitives.Projection.MemImage

namespace Projection
namespace CkptStateM

open Compiler Compiler.Ckpt

/-- A checkpoint memory as the FP route's table of `2 ^ aw` words. -/
def memImgOf (m : MemCk) : MemImg :=
  let t := memTable m
  ⟨(List.range (2 ^ m.aw)).map (fun a => mk_bv m.dw ((t[a]?.getD m.dflt : Nat) : Int)),
   mk_bv m.dw (m.dflt : Int)⟩

/-- The FP route's state for a checkpoint. -/
def toStateM (c : Ckpt) : StateM :=
  { flops := c.flops.map fun (w, v) => mk_bv w (v : Int)
    mems  := c.mems.map memImgOf }

/-- The checkpoint of an FP state, through the state it denotes. -/
def saveM (D : DesignCert) (hash cycle : Nat) (exts : Array Ext) (st : StateM) : Ckpt :=
  save D hash cycle exts st.denote

theorem memImgOf_denote (m : MemCk) (h : m.depth ≤ 2 ^ m.aw) :
    (memImgOf m).denote = memImage m := by
  funext x
  simp only [MemImg.denote, MemImg.get, memImgOf, memImage]
  by_cases hx : 0 ≤ x
  · simp only [hx, ite_true]
    by_cases hlt : x.toNat < 2 ^ m.aw
    · rw [List.getElem?_map, List.getElem?_range hlt]
      rfl
    · have hn : (List.map (fun a => mk_bv m.dw (((memTable m)[a]?.getD m.dflt : Nat) : Int))
          (List.range (2 ^ m.aw)))[x.toNat]? = none := by
        rw [List.getElem?_eq_none]; simp; omega
      have ht : (memTable m)[x.toNat]? = none := by
        rw [Array.getElem?_eq_none]; rw [table_size]; omega
      rw [hn, ht]
      rfl
  · simp only [hx, ite_false]
    rfl

/-- **T6.** The FP state built from a checkpoint denotes `restore c`. -/
theorem toStateM_denote (c : Ckpt) (h : ∀ m ∈ c.mems, m.depth ≤ 2 ^ m.aw) :
    (toStateM c).denote = restore c := by
  simp only [StateM.denote, toStateM, restore, Array.map_map]
  congr 1
  apply Array.map_congr_left
  intro m hm
  exact memImgOf_denote m (h m hm)

#audit_axioms Projection.CkptStateM.toStateM_denote

end CkptStateM
end Projection
