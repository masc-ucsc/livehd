/-
# Node preservation in a mixed environment, and the checker-derived kinds

`evalNodeM` is `applyOpM` behind the node's `switch`, so this module is three
thin wrappers plus the part that matters: **where the operand kinds come from.**

## The kinds are DERIVED from `Direct.DesignSemWF`, not assumed

`checkDesign` already establishes `nodeKinds`:

```
∀ i c, D.nodes[i]? = some c → ∀ p d, c.deps[p]? = some d →
  slotIsMem D d = some (depIsMem c.op p)
```

so the operand kind at every dependency position is fixed by the CHECKER.  What
this module adds are the facts that connect it to the interpreter:

* `depIsMem_false_of_supported` — a supported (scalar) operator has NO memory
  operand at any position, decided over the operator type.  With `nodeKinds`
  that makes every dependency of a scalar node a bit-vector slot, which is
  exactly `SlotHere`'s content and hence `applyOpM_scalar_mixed`'s premise.
* `depIsMem_memRead` / `depIsMem_memWriteBE` — position 0 of each memory
  operator is a memory and every other position is a bit vector.
* `opKind_isMem_*` and `nodeSlot_slotIsMem` — the node's RESULT kind, at the
  slot the node occupies, so the invariant can be re-established after the step.

## Admission, and what is refused

`NodeAdmitted` is `OpSupported ∨ Op_MemRead ∨ Op_MemWriteBE _`.  **`Op_MemWrite`
(opcode 27) is refused**: there is no implementation bridge for it — no
primitive, no `applyOpM` case — and `NodeAdmitted_ne_memWrite` proves an
admitted node is never it.  Nothing is admitted by default; `OpSupported`,
`SourceSupported` and `memFree` are untouched.

## The write's range obligation

`WriteInRange` names it rather than assuming it away, and
`writeInRange_of_FullTable` discharges it from the admitted shape:
a `FullTable aw` image with an `aw`-bit address.  That is the real PicoRV32
shape (`aw = 5`, `dw = 32`), and `MemImg.writeBE_FullTable` shows it survives
the write, so a design stays admissible across cycles.  A ROM is deliberately
NOT a full table.
-/
import LeanSemanticPrimitives.Projection.HardwareMemTraversal
import LeanSemanticPrimitives.Projection.HardwareMemShapes

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw Projection.HwLocal

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. `evalNodeM` is `applyOpM` behind the node switch -/

theorem evalNodeM_scalar {vals : List Val} {arg : Nat → BV} {c : DenseNodeCert}
    {σ : SEnv} {end_ eenv en : SExp}
    (hsup : OpSupported c.op = true) (harity : ArityOK c.op c.deps.toList)
    (hb : ∀ d ∈ c.deps.toList, SlotHere vals arg d)
    (hnd : SEval hwMemS σ end_ (encNode c))
    (henv : SEval hwMemS σ eenv (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en])
      (encBV (eval_op c.op c.width (c.deps.toList.map arg))) := by
  refine SEval_call3 hnd henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  exact applyOpM_scalar_mixed hsup harity hb (.ref rfl) (.ref rfl) (.ref rfl)
    (.ref rfl) (.ref rfl)

theorem evalNodeM_memRead {vals : List Val} {c : DenseNodeCert}
    {σ : SEnv} {end_ eenv en : SExp} {d0 d1 d2 : Nat} {m : MemImg} {a enb : BV}
    (hop : c.op = .Op_MemRead) (hdeps : c.deps.toList = [d0, d1, d2])
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length) (hb2 : d2 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV enb)
    (hnd : SEval hwMemS σ end_ (encNode c))
    (henv : SEval hwMemS σ eenv (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en])
      (encBV (cert_mem_read c.width (MemImg.denote m) a enb)) := by
  refine SEval_call3 hnd henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine applyOpM_memRead hb0 hb1 hb2 hv0 hv1 hv2 ?_ (.ref rfl) ?_ (.ref rfl) (.ref rfl)
  · rw [← hop]; exact .ref rfl
  · refine .ref ?_
    simp [slookup, encArr, hdeps]

theorem evalNodeM_memWriteBE {vals : List Val} {c : DenseNodeCert}
    {σ : SEnv} {end_ eenv en : SExp} {bw d0 d1 d2 d3 : Nat}
    {m : MemImg} {a dat be : BV}
    (hop : c.op = .Op_MemWriteBE bw) (hdeps : c.deps.toList = [d0, d1, d2, d3])
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV be)
    (hen : bv_nonzero be) (hr : MemImg.InRange m (bv_uint a))
    (hnd : SEval hwMemS σ end_ (encNode c))
    (henv : SEval hwMemS σ eenv (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en])
      (MemImg.encMemImg (MemImg.writeBE c.width m a dat be bw)) := by
  refine SEval_call3 hnd henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine applyOpM_memWriteBE hb0 hb1 hb2 hb3 hv0 hv1 hv2 hv3 hen hr
    ?_ (.ref rfl) ?_ (.ref rfl) (.ref rfl)
  · rw [← hop]; exact .ref rfl
  · refine .ref ?_
    simp [slookup, encArr, hdeps]

/-! ## 2. Checker-to-kind facts

These are what turn `Direct.DesignSemWF.nodeKinds` into the operand premises the
bridges need.  None of them is a hypothesis: each is decided over the operator
type or read off `Direct`'s own definitions. -/

/-- **A supported operator has no memory operand at any position.** -/
theorem depIsMem_false_of_supported {op : LGraphOp} (h : OpSupported op = true)
    (p : Nat) : Direct.depIsMem op p = false := by
  cases op <;> simp_all [OpSupported, Direct.depIsMem]

/-- `Op_MemRead`: position 0 is the image, 1 and 2 are bit vectors. -/
@[simp] theorem depIsMem_memRead_zero : Direct.depIsMem .Op_MemRead 0 = true := rfl

theorem depIsMem_memRead_succ (p : Nat) :
    Direct.depIsMem .Op_MemRead (p + 1) = false := rfl

/-- `Op_MemWriteBE`: position 0 is the image, 1..3 are bit vectors. -/
@[simp] theorem depIsMem_memWriteBE_zero (bw : Nat) :
    Direct.depIsMem (.Op_MemWriteBE bw) 0 = true := rfl

theorem depIsMem_memWriteBE_succ (bw p : Nat) :
    Direct.depIsMem (.Op_MemWriteBE bw) (p + 1) = false := rfl

/-- A supported operator's RESULT is a bit vector. -/
theorem opKind_isMem_of_supported {op : LGraphOp} (h : OpSupported op = true)
    (w : Nat) : (Direct.opKind op w).isMem = false := by
  cases op <;> simp_all [OpSupported, Direct.opKind, Direct.SlotKind.isMem]

@[simp] theorem opKind_isMem_memRead (w : Nat) :
    (Direct.opKind .Op_MemRead w).isMem = false := rfl

@[simp] theorem opKind_isMem_memWriteBE (bw w : Nat) :
    (Direct.opKind (.Op_MemWriteBE bw) w).isMem = true := rfl

/-- **The node's own slot carries its result kind.**  Read off `slotKind`'s
definition at a node index, so it is the checker's notion, not a parallel one. -/
theorem nodeSlot_slotIsMem {D : DesignCert} {j : Nat} {c : DenseNodeCert}
    (hc : D.nodes[j]? = some c) :
    Direct.slotIsMem D (D.sources.size + j) = some (Direct.opKind c.op c.width).isMem := by
  have hs : D.sources[D.sources.size + j]? = none := by
    rw [Array.getElem?_eq_none]; omega
  simp [Direct.slotIsMem, Direct.slotKind, hs, hc]

/-! ## 3. Admission, and what is refused -/

/-- What this route can discharge for one node.  `Op_MemWrite` is NOT here: it
has no primitive and no `applyOpM` case, so it is refused rather than given a
defined-and-unproved answer. -/
def NodeAdmitted (c : DenseNodeCert) : Prop :=
  OpSupported c.op = true ∨ c.op = .Op_MemRead ∨ ∃ bw, c.op = .Op_MemWriteBE bw

/-- The same policy as a decidable CHECK, so admission can be run, not just
stated.  `Op_MemWrite` falls through to `OpSupported`, which refuses it. -/
def nodeAdmittedB (c : DenseNodeCert) : Bool :=
  match c.op with
  | .Op_MemRead      => true
  | .Op_MemWriteBE _ => true
  | op               => OpSupported op

theorem nodeAdmittedB_iff (c : DenseNodeCert) : nodeAdmittedB c = true ↔ NodeAdmitted c := by
  unfold nodeAdmittedB NodeAdmitted
  cases c.op <;> simp [OpSupported]

/- the checker refuses plain `Op_MemWrite` -/
#guard nodeAdmittedB { op := .Op_MemWrite, width := 32, deps := #[0,1,2,3], origin := 0 } = false
#guard nodeAdmittedB { op := .Op_MemRead, width := 32, deps := #[0,1,2], origin := 0 } = true
#guard nodeAdmittedB { op := .Op_MemWriteBE 1, width := 32, deps := #[0,1,2,3], origin := 0 } = true
#guard nodeAdmittedB { op := .Op_SRA, width := 32, deps := #[0,1], origin := 0 } = true
#guard nodeAdmittedB { op := .Op_Sub, width := 32, deps := #[0,1], origin := 0 } = false

/-- **`Op_MemWrite` is refused.** -/
theorem NodeAdmitted_ne_memWrite {c : DenseNodeCert} (h : NodeAdmitted c) :
    c.op ≠ .Op_MemWrite := by
  rcases h with h | h | ⟨bw, h⟩
  · intro hc; rw [hc] at h; simp [OpSupported] at h
  · intro hc; rw [hc] at h; exact absurd h (by simp)
  · intro hc; rw [hc] at h; exact absurd h (by simp)

/-- …and so are the five refused scalar operators, by `OpSupported`. -/
theorem NodeAdmitted_ne_sub {c : DenseNodeCert} (h : NodeAdmitted c) :
    c.op ≠ .Op_Sub := by
  rcases h with h | h | ⟨bw, h⟩ <;> intro hc <;> rw [hc] at h <;>
    first | simp [OpSupported] at h | exact absurd h (by simp)

/-! ## 4. The write's range obligation, named and discharged from shapes -/

/-- The obligation an admitted WRITE node carries: the image it updates holds
the address it is given.  Named here so it is visible, and discharged below
from the admitted table shape rather than assumed. -/
def WriteInRange (m : MemImg) (a : BV) : Prop := MemImg.InRange m (bv_uint a)

/-- **Discharged from the admitted shape**: a full `2 ^ aw` table and an
`aw`-bit address.  No condition on the address VALUE. -/
theorem writeInRange_of_FullTable {aw : Nat} {m : MemImg} (hfull : MemImg.FullTable aw m)
    {a : BV} (hw : a.width = aw) : WriteInRange m a :=
  MemImg.InRange_of_FullTable hfull hw

/-- …at PicoRV32's actual shape. -/
theorem writeInRange_pico (a : BV) (hw : a.width = 5) :
    WriteInRange MemImg.picoImg a :=
  writeInRange_of_FullTable MemImg.picoImg_FullTable hw

/-- …and the shape survives, so the next cycle is admissible too. -/
theorem writeInRange_after {aw : Nat} {m : MemImg} (hfull : MemImg.FullTable aw m)
    (w : Nat) (a0 d bev : BV) (bw : Nat) {a : BV} (hw : a.width = aw) :
    WriteInRange (MemImg.writeBE w m a0 d bev bw) a :=
  writeInRange_of_FullTable (MemImg.writeBE_FullTable hfull w a0 d bev bw) hw

/-- A ROM is NOT an admitted write target: `romNotFull_example` is the Pico-shaped
counterexample and the primitive refuses the write outright. -/
theorem rom_not_admitted_write : ¬ MemImg.FullTable 5 (MemImg.ofMemConst 32 #[11, 22, 33]) :=
  MemImg.romNotFull_example

#print axioms evalNodeM_scalar
#print axioms evalNodeM_memRead
#print axioms evalNodeM_memWriteBE
#print axioms depIsMem_false_of_supported
#print axioms opKind_isMem_of_supported
#print axioms nodeSlot_slotIsMem
#print axioms NodeAdmitted_ne_memWrite
#print axioms nodeAdmittedB_iff
#print axioms writeInRange_of_FullTable
#print axioms writeInRange_pico
#print axioms writeInRange_after

end HwMem
end Projection
