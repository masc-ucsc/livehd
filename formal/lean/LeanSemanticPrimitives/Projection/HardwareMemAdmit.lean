/-
# Executable static memory admission over the ACTUAL `DesignCert`

A slot-type inference over the dense source/node order, and the checker built
from it.  `Direct.checkDesign` is RETAINED, not replaced: `admitsB` conjoins it,
so every kind, bound and clock fact the existing checker establishes still
holds, and this layer adds only what the memory route additionally needs.

## The slot types

```
SlotTy := bv w            -- a scalar of width w
        | tbl aw dw       -- a MUTABLE image: a full 2^aw table of dw-bit words
        | rom dw n        -- a READ-ONLY image: n ACTUAL words of dw bits
```

`tbl` and `rom` are deliberately distinct.  A `memConst` keeps its real
`contents.size` and its own default and is typed `rom`, so it can be READ but
never written: the write rule demands `tbl` at operand 0.

## The rules

* **`Op_MemRead [m, a, en]`** — `m` is an image (either kind), `a` and `en` are
  scalars; against a `tbl aw _` the address may not be wider than `aw`.  The
  result is `bv` at the NODE width, which `cert_mem_read_width` confirms.
* **`Op_MemWriteBE bw [m, a, d, be]`** — `m` must be `tbl aw dw`; the address is
  no wider than `aw`; the node width and the data width are exactly `dw`;
  `0 < bw`; and the mask must cover every data bit, `dw ≤ wb * bw`.  At
  `bw = 1` that is `dw ≤ wb` — PicoRV32's bit-enable shape at `aw = 5`,
  `dw = 32`, needing a 32-bit mask.  At `bw = 8` it is `dw ≤ 8 * wb`, the lane
  case, needing only 4 mask bits for 32-bit data.  The result propagates the
  image's own `tbl aw dw`, which is `WritePropagates`.
* **Any other operator** — admitted only when `OpSupported` holds and every
  dependency is a scalar; the result is `bv` at the node width.  `Op_MemWrite`
  and `Op_Sub` are not `OpSupported` and have no rule here, so they are
  REFUSED.
* **Every declared memory's `nextImg`** is checked against that memory's own
  `aw` and `dw`.

Nothing here weakens `SourceSupported`, `OpSupported` or `memFree`, and no
original predicate or theorem changes meaning.
-/
import LeanSemanticPrimitives.Projection.HardwareMemWidth

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. Slot types -/

inductive SlotTy where
  | bv  (w : Nat)
  | tbl (aw dw : Nat)
  | rom (dw n : Nat)
deriving DecidableEq, Repr, Inhabited

/-- A source's type, read straight off its descriptor.  A ROM keeps its ACTUAL
`contents.size`. -/
def srcTy : SourceDesc → SlotTy
  | .input _ w            => .bv w
  | .const w _            => .bv w
  | .flopQ _ w            => .bv w
  | .flopQAsync _ w _ _ _ => .bv w
  | .memImg _ aw dw       => .tbl aw dw
  | .memConst _ dw ct     => .rom dw ct.size

def isBvTy : Option SlotTy → Bool
  | some (.bv _) => true
  | _            => false

def bvWidthOf : Option SlotTy → Nat
  | some (.bv w) => w
  | _            => 0

/-- One node's type, from its dependencies' types. -/
def nodeTy (ty : Nat → Option SlotTy) (c : DenseNodeCert) : Option SlotTy :=
  match c.op, c.deps.toList with
  | .Op_MemRead, [m, a, en] =>
      if isBvTy (ty a) && isBvTy (ty en) then
        match ty m with
        | some (.tbl aw _) => if bvWidthOf (ty a) ≤ aw then some (.bv c.width) else none
        | some (.rom _ _)  => some (.bv c.width)
        | _                => none
      else none
  | .Op_MemWriteBE bw, [m, a, d, be] =>
      if isBvTy (ty a) && isBvTy (ty d) && isBvTy (ty be) then
        match ty m with
        | some (.tbl aw dw) =>
            if bvWidthOf (ty a) ≤ aw && c.width == dw && bvWidthOf (ty d) == dw
               && 0 < bw && dw ≤ bvWidthOf (ty be) * bw
            then some (.tbl aw dw) else none
        | _ => none
      else none
  | op, ds =>
      if OpSupported op && ds.all (fun d => isBvTy (ty d))
      then some (.bv c.width) else none

/-- One node's step: type it from the slots already inferred, then APPEND.
Structural in the node list, so the checker reduces in the kernel. -/
def inferStep (ts : Array SlotTy) (c : DenseNodeCert) : Option (Array SlotTy) :=
  (nodeTy (fun d => ts[d]?) c).map ts.push

/-- The inferred type of every slot, walked in dense order: the sources read
their descriptors, then each node consults only slots already inferred. -/
def inferTys (D : DesignCert) : Option (Array SlotTy) :=
  D.nodes.toList.foldl (fun acc c => acc.bind (fun ts => inferStep ts c))
    (some (D.sources.map srcTy))

def tyAt (D : DesignCert) (k : Nat) : Option SlotTy :=
  (inferTys D).bind (fun ts => ts[k]?)

/-- The geometry assignment the checker produces. -/
def geomAt (D : DesignCert) (k : Nat) : Option Nat :=
  match tyAt D k with
  | some (.tbl aw _) => some aw
  | _                => none

/-! ## 2. The executable checker -/

def admitsB (D : DesignCert) : Bool :=
  Direct.acceptsDesign D
  && (match inferTys D with
      | none    => false
      | some ts =>
          ts.size == D.numSlots
          && D.memories.toList.all (fun md =>
               match ts[md.nextImg]? with
               | some (.tbl aw dw) => aw == md.aw && dw == md.dw
               | _                 => false))

/-! ## 3. Soundness: what a successful check gives

Three of `mainM_adequate_resolved`'s antecedents are discharged here directly
from `admitsB`.  The remaining two — `NodeAdmitted`/arity and
`WritePropagates`, together with the slot-width induction that DERIVES
`WriteAddrFits` — need the node-type recurrence and are the next increment;
they are NOT claimed here. -/

theorem admitsB_acceptsDesign {D : DesignCert} (h : admitsB D = true) :
    Direct.acceptsDesign D = true := by
  unfold admitsB at h
  simp only [Bool.and_eq_true] at h
  exact h.1

/-- **`Direct.checkDesign` is retained**, so every kind, bound and clock fact it
establishes still holds of an admitted design. -/
theorem admits_semwf {D : DesignCert} (h : admitsB D = true) : Direct.DesignSemWF D := by
  have hacc := admitsB_acceptsDesign h
  cases hd : Direct.designErrors D with
  | none =>
      exact Direct.checkDesign_sound (u := ()) (by unfold Direct.checkDesign; rw [hd])
  | some e =>
      exfalso
      unfold Direct.acceptsDesign at hacc
      rw [hd] at hacc
      simp at hacc

theorem admits_wf {D : DesignCert} (h : admitsB D = true) : DesignCert.DesignCertWF D :=
  designCertWF_of_semwf (admits_semwf h)

/-- A push never disturbs an index already present. -/
theorem inferStep_prefix {ts ts' : Array SlotTy} {c : DenseNodeCert}
    (h : inferStep ts c = some ts') :
    ts.size < ts'.size ∧ ∀ k, k < ts.size → ts'[k]? = ts[k]? := by
  unfold inferStep at h
  cases hn : nodeTy (fun d => ts[d]?) c with
  | none   => rw [hn] at h; simp at h
  | some t =>
      rw [hn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      subst h
      refine ⟨by simp, ?_⟩
      intro k hk
      simp [Array.getElem?_push, hk]
      omega

/-- Once the walk fails it stays failed. -/
theorem infer_foldl_none :
    ∀ (ns : List DenseNodeCert),
      ns.foldl (fun acc c => acc.bind (fun ts => inferStep ts c)) none = none
  | []      => rfl
  | _ :: cs => by simpa using infer_foldl_none cs

/-- **The source prefix survives the whole walk.** -/
theorem infer_foldl_prefix :
    ∀ (ns : List DenseNodeCert) (init ts : Array SlotTy),
      ns.foldl (fun acc c => acc.bind (fun t => inferStep t c)) (some init) = some ts →
      ∀ k, k < init.size → ts[k]? = init[k]?
  | [],      init, ts, h, k, _  => by
      simp only [List.foldl_nil, Option.some.injEq] at h
      subst h; rfl
  | c :: cs, init, ts, h, k, hk => by
      simp only [List.foldl_cons, Option.bind_some] at h
      cases hs : inferStep init c with
      | none   => rw [hs, infer_foldl_none] at h; simp at h
      | some t =>
          rw [hs] at h
          obtain ⟨hlt, hpre⟩ := inferStep_prefix hs
          rw [infer_foldl_prefix cs t ts h k (by omega)]
          exact hpre k hk

theorem tyAt_source {D : DesignCert} {k : Nat} (hk : k < D.sources.size)
    {ts : Array SlotTy} (hts : inferTys D = some ts) :
    tyAt D k = some (srcTy (D.sources[k]'hk)) := by
  unfold tyAt
  rw [hts]
  simp only [Option.bind_some]
  rw [infer_foldl_prefix D.nodes.toList _ ts hts k (by simpa using hk)]
  simp [Array.getElem?_eq_getElem hk]

/-- **The geometry assignment agrees with the sources**, which is
`mainM_adequate_resolved`'s `hGsrc`. -/
theorem admitsB_infer {D : DesignCert} (h : admitsB D = true) :
    ∃ ts, inferTys D = some ts := by
  unfold admitsB at h
  simp only [Bool.and_eq_true] at h
  cases hts : inferTys D with
  | none   => rw [hts] at h; simp at h
  | some t => exact ⟨t, rfl⟩

theorem admits_geom_src {D : DesignCert} (h : admitsB D = true) :
    ∀ k, k < D.sources.size → geomAt D k = srcGeomOf D k := by
  obtain ⟨ts, hts⟩ := admitsB_infer h
  intro k hk
  unfold geomAt srcGeomOf
  rw [tyAt_source hk hts, Array.getElem?_eq_getElem hk]
  cases D.sources[k]'hk <;> rfl

/-- **Every declared memory's next image has that memory's geometry**, which is
`NextImgGeomOK`. -/
theorem admits_nextImgGeom {D : DesignCert} (h : admitsB D = true) :
    NextImgGeomOK D (geomAt D) := by
  intro idx md hmd
  obtain ⟨ts, hts⟩ := admitsB_infer h
  have hmem : md ∈ D.memories.toList := by
    obtain ⟨h1, h2⟩ := List.getElem?_eq_some_iff.mp
      (show D.memories.toList[idx]? = some md by rw [← hmd]; simp)
    exact h2 ▸ List.getElem_mem h1
  unfold admitsB at h
  rw [hts] at h
  simp only [Bool.and_eq_true] at h
  have hthis := List.all_eq_true.mp h.2.2 md hmem
  unfold geomAt tyAt
  rw [hts]
  simp only [Option.bind_some]
  cases hty : ts[md.nextImg]? with
  | none => rw [hty] at hthis; simp at hthis
  | some t =>
      cases t with
      | bv w     => rw [hty] at hthis; simp at hthis
      | rom dw n => rw [hty] at hthis; simp at hthis
      | tbl aw dw =>
          rw [hty] at hthis
          simp only [Bool.and_eq_true, beq_iff_eq] at hthis
          rw [hthis.1]

/-! ## 4. The retained designs pass, and the refusals bite

`mixedDesign` (§63.36) and `twoMemDesign` (§63.37) go through the NEW checker
unchanged.  The rejection controls below are SMALL changed-source variants of
them, not re-runs of anything. -/

#guard admitsB mixedDesign
#guard admitsB twoMemDesign

/-- A `byte_w = 8` LANE design: 32-bit data needs only a 4-bit mask, because
`dw ≤ wb * bw` is `32 ≤ 4 * 8`.  Kept beside the `byte_w = 1` bit-enable case so
neither rule is quietly folded into the other. -/
def laneDesign : DesignCert :=
  { mixedDesign with
    sources := #[ .memImg 0 5 32, .input 0 5, .input 1 4, .input 2 1, .input 3 32 ]
    nodes   := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 1, 3] }
                , { op := .Op_And,          width := 32, deps := #[5, 4] }
                , { op := .Op_MemWriteBE 8, width := 32, deps := #[0, 1, 6, 2] } ] }

#guard admitsB laneDesign
/- …and the SAME design with `byte_w = 1` is refused: a 4-bit mask cannot cover
   32 data bits one bit at a time -/
#guard !(admitsB { laneDesign with
          nodes := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 1, 3] }
                    , { op := .Op_And,          width := 32, deps := #[5, 4] }
                    , { op := .Op_MemWriteBE 1, width := 32, deps := #[0, 1, 6, 2] } ] })

/-! ### Rejection controls -/

/- a ROM as the write target: operand 0 is a `memConst`, which types `rom` -/
#guard !(admitsB { mixedDesign with
          sources := #[ .memConst 5 32 #[1, 2, 3], .input 0 5, .input 1 32
                      , .input 2 1, .input 3 32 ] })

/- incompatible next-image geometry: the memory declares `aw = 6` while the
   write's image slot types `tbl 5 32` -/
#guard !(admitsB { mixedDesign with
          memories := #[ { aw := 6, dw := 32, nextImg := 7, clock := 0 } ] })

/- incompatible next-image WORD width -/
#guard !(admitsB { mixedDesign with
          memories := #[ { aw := 5, dw := 16, nextImg := 7, clock := 0 } ] })

/- an address WIDER than the table's address width -/
#guard !(admitsB { mixedDesign with
          sources := #[ .memImg 0 5 32, .input 0 8, .input 1 32
                      , .input 2 1, .input 3 32 ] })

/- a DATA width that is not the memory's word width -/
#guard !(admitsB { mixedDesign with
          nodes := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 1, 3] }
                    , { op := .Op_And,          width := 16, deps := #[5, 4] }
                    , { op := .Op_MemWriteBE 1, width := 32, deps := #[0, 1, 6, 2] } ] })

/- a NODE width that is not the memory's word width -/
#guard !(admitsB { mixedDesign with
          nodes := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 1, 3] }
                    , { op := .Op_And,          width := 32, deps := #[5, 4] }
                    , { op := .Op_MemWriteBE 1, width := 16, deps := #[0, 1, 6, 2] } ] })

/- the unimplemented plain `Op_MemWrite` -/
#guard !(admitsB { mixedDesign with
          nodes := #[ { op := .Op_MemRead,  width := 32, deps := #[0, 1, 3] }
                    , { op := .Op_And,      width := 32, deps := #[5, 4] }
                    , { op := .Op_MemWrite, width := 32, deps := #[0, 1, 6, 2] } ] })

/- the refused `Op_Sub` -/
#guard !(admitsB { mixedDesign with
          nodes := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 1, 3] }
                    , { op := .Op_Sub,          width := 32, deps := #[5, 4] }
                    , { op := .Op_MemWriteBE 1, width := 32, deps := #[0, 1, 6, 2] } ] })

/- a memory operand where a SCALAR is required: the `Op_And` reads the image -/
#guard !(admitsB { mixedDesign with
          nodes := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 1, 3] }
                    , { op := .Op_And,          width := 32, deps := #[0, 4] }
                    , { op := .Op_MemWriteBE 1, width := 32, deps := #[0, 1, 6, 2] } ] })

/- an out-of-range next-image slot -/
#guard !(admitsB { mixedDesign with
          memories := #[ { aw := 5, dw := 32, nextImg := 99, clock := 0 } ] })

/- an undeclared clock ordinal -- `Direct.checkDesign` catches this, and
   `admitsB` retains it -/
#guard !(admitsB { mixedDesign with
          memories := #[ { aw := 5, dw := 32, nextImg := 7, clock := 9 } ] })

/-! ### The geometry the checker produces -/

#guard geomAt mixedDesign 0 == some 5
#guard geomAt mixedDesign 7 == some 5
#guard geomAt mixedDesign 1 == none
#guard geomAt twoMemDesign 0 == some 5
#guard geomAt twoMemDesign 1 == some 5
#guard geomAt twoMemDesign 8 == some 5
#guard geomAt twoMemDesign 9 == some 5

/-! ### Soundness, and where it is NOT yet applied

The soundness theorems above are general and proved.  Applying them to a
CONCRETE design needs `admitsB D = true` as a theorem, and neither `decide` nor
`rfl` reduces `admitsB` to a literal — the fold over the node list does not
evaluate in the kernel here.  So the concrete designs are currently carried by
`#guard` (compiler evaluation), which is executed evidence but **not** a
theorem.  Closing that is the first item of the next increment; it is not
claimed now, and no theorem below depends on it.

`#guard` evidence for the retained designs is above; the geometry the checker
produces is below. -/

#print axioms admits_semwf
#print axioms admits_wf
#print axioms admits_geom_src
#print axioms admits_nextImgGeom

end HwMem
end Projection
