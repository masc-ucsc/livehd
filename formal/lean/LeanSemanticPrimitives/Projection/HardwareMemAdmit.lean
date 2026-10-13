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

theorem isBvTy_elim {o : Option SlotTy} (h : isBvTy o = true) :
    o = some (.bv (bvWidthOf o)) := by
  cases o with
  | none   => simp [isBvTy] at h
  | some t => cases t <;> simp_all [isBvTy, bvWidthOf]

/-- One node's type, from its dependencies' types.  The lookup is consulted
ONLY through `c.deps.toList.map ty`, which makes congruence immediate. -/
def nodeTy (ty : Nat → Option SlotTy) (c : DenseNodeCert) : Option SlotTy :=
  if !nodeAdmittedB c then none else
  match c.op, c.deps.toList.map ty with
  | .Op_MemRead, [tm, ta, ten] =>
      if isBvTy ta && isBvTy ten then
        match tm with
        | some (.tbl aw _) => if bvWidthOf ta ≤ aw then some (.bv c.width) else none
        | some (.rom _ _)  => some (.bv c.width)
        | _                => none
      else none
  | .Op_MemWriteBE bw, [tm, ta, td, tbe] =>
      if isBvTy ta && isBvTy td && isBvTy tbe then
        match tm with
        | some (.tbl aw dw) =>
            if bvWidthOf ta ≤ aw && c.width == dw && bvWidthOf td == dw
               && 0 < bw && dw ≤ bvWidthOf tbe * bw
            then some (.tbl aw dw) else none
        | _ => none
      else none
  | op, tys =>
      if OpSupported op && tys.all isBvTy then some (.bv c.width) else none

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

/-! ### The per-node recurrence

`nodeTy` reads only its own dependencies, so the lookup it was given during the
walk and the finished array agree on everything it consults.  That is what
turns the fold into a per-node equation. -/

theorem nodeTy_congr {ty ty' : Nat → Option SlotTy} {c : DenseNodeCert}
    (h : ∀ d ∈ c.deps.toList, ty d = ty' d) : nodeTy ty c = nodeTy ty' c := by
  unfold nodeTy
  rw [List.map_congr_left h]

theorem inferStep_size {ts ts' : Array SlotTy} {c : DenseNodeCert}
    (h : inferStep ts c = some ts') : ts'.size = ts.size + 1 := by
  unfold inferStep at h
  cases hn : nodeTy (fun d => ts[d]?) c with
  | none   => rw [hn] at h; simp at h
  | some t =>
      rw [hn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      subst h; simp

theorem infer_foldl_size :
    ∀ (ns : List DenseNodeCert) (init ts : Array SlotTy),
      ns.foldl (fun acc c => acc.bind (fun t => inferStep t c)) (some init) = some ts →
      ts.size = init.size + ns.length
  | [],      init, ts, h => by
      simp only [List.foldl_nil, Option.some.injEq] at h; subst h; simp
  | c :: cs, init, ts, h => by
      simp only [List.foldl_cons, Option.bind_some] at h
      cases hs : inferStep init c with
      | none   => rw [hs, infer_foldl_none] at h; simp at h
      | some t =>
          rw [hs] at h
          have hsz := inferStep_size hs
          rw [infer_foldl_size cs t ts h, hsz, List.length_cons]
          omega

/-- **The per-node equation.**  Node `j`'s inferred type is `nodeTy` applied to
it against the FINISHED array, provided its dependencies name earlier slots. -/
theorem infer_foldl_node :
    ∀ (ns : List DenseNodeCert) (init ts : Array SlotTy),
      ns.foldl (fun acc c => acc.bind (fun t => inferStep t c)) (some init) = some ts →
      ∀ (j : Nat) (c : DenseNodeCert), ns[j]? = some c →
        (∀ d ∈ c.deps.toList, d < init.size + j) →
        nodeTy (fun d => ts[d]?) c = ts[init.size + j]?
  | [],       _,    _,  _, j, _, hj, _ => by simp at hj
  | c0 :: cs, init, ts, h, j, c, hj, hb => by
      simp only [List.foldl_cons, Option.bind_some] at h
      cases hs : inferStep init c0 with
      | none   => rw [hs, infer_foldl_none] at h; simp at h
      | some t =>
          rw [hs] at h
          have hsz := inferStep_size hs
          have hpre := infer_foldl_prefix cs t ts h
          cases j with
          | zero =>
              have hc0 : c0 = c := by simpa using hj
              subst hc0
              -- the pushed entry IS `nodeTy` against the accumulator
              have hx : nodeTy (fun d => init[d]?) c0 = t[init.size]? := by
                unfold inferStep at hs
                cases hn : nodeTy (fun d => init[d]?) c0 with
                | none   => rw [hn] at hs; simp at hs
                | some x =>
                    rw [hn] at hs
                    simp only [Option.map_some, Option.some.injEq] at hs
                    subst hs
                    simp
              have hcong : nodeTy (fun d => ts[d]?) c0 = nodeTy (fun d => init[d]?) c0 := by
                refine nodeTy_congr ?_
                intro d hd
                have hdlt : d < init.size := by simpa using hb d hd
                rw [hpre d (by omega), (inferStep_prefix hs).2 d hdlt]
              rw [hcong, hx, Nat.add_zero]
              exact (hpre init.size (by omega)).symm
          | succ q =>
              have hq : cs[q]? = some c := by simpa using hj
              have hb' : ∀ d ∈ c.deps.toList, d < t.size + q := by
                intro d hd
                have := hb d hd
                omega
              have := infer_foldl_node cs t ts h q c hq hb'
              rw [this]
              congr 1
              omega

/-- …specialised to the real design: `init` is the source types, so the index
is the node's own slot. -/
theorem admits_node_ty {D : DesignCert} {ts : Array SlotTy}
    (hts : inferTys D = some ts) (hwf : DesignCert.DesignCertWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) :
    nodeTy (fun d => ts[d]?) c = ts[D.sources.size + j]? := by
  have hlist : D.nodes.toList[j]? = some c := by rw [← hc]; simp
  have hb : ∀ d ∈ c.deps.toList, d < (D.sources.map srcTy).size + j := by
    intro d hd
    simpa using hwf.depsBounded j c hc d hd
  simpa using infer_foldl_node D.nodes.toList _ ts hts j c hlist hb

/-! ### From a successful type to the static premises -/

/-- A node that TYPES is an admitted node — `nodeTy` refuses anything
`nodeAdmittedB` refuses, which is `Op_MemWrite`, `Op_Sub` and every other
operator without an implementation. -/
theorem nodeTy_admits {ty : Nat → Option SlotTy} {c : DenseNodeCert} {t : SlotTy}
    (h : nodeTy ty c = some t) : NodeAdmitted c := by
  cases hb : nodeAdmittedB c with
  | false =>
      exfalso
      unfold nodeTy at h
      rw [hb] at h
      simp at h
  | true => exact (nodeAdmittedB_iff c).mp hb

/-- A WRITE that types consumed a MUTABLE image at operand 0 and returns that
image's own geometry — which is what `WritePropagates` needs. -/
theorem nodeTy_write_geom {ty : Nat → Option SlotTy} {c : DenseNodeCert}
    {bw d0 d1 d2 d3 : Nat} {t : SlotTy}
    (hop : c.op = .Op_MemWriteBE bw) (hdeps : c.deps.toList = [d0, d1, d2, d3])
    (h : nodeTy ty c = some t) :
    ∃ aw dw wa, t = .tbl aw dw ∧ ty d0 = some (.tbl aw dw)
              ∧ ty d1 = some (.bv wa) ∧ wa ≤ aw := by
  have hb : nodeAdmittedB c = true := by
    cases hbb : nodeAdmittedB c with
    | false => exfalso; unfold nodeTy at h; rw [hbb] at h; simp at h
    | true  => rfl
  unfold nodeTy at h
  rw [hb] at h
  simp only [Bool.not_true, Bool.false_eq_true, if_false] at h
  rw [hop, hdeps] at h
  simp only [List.map_cons, List.map_nil] at h
  split at h
  · rename_i hguard
    split at h
    · rename_i aw' dw' heq
      split at h
      · rename_i hcond
        simp only [Bool.and_eq_true, decide_eq_true_eq] at hguard hcond
        simp only [Option.some.injEq] at h
        refine ⟨aw', dw', bvWidthOf (ty d1), h.symm, heq, isBvTy_elim hguard.1.1, ?_⟩
        exact hcond.1.1.1.1
      · simp at h
    · simp at h
  · simp at h

/-! ### The general checker-to-static-premises theorems -/

theorem admitsB_size {D : DesignCert} {ts : Array SlotTy}
    (h : admitsB D = true) (hts : inferTys D = some ts) : ts.size = D.numSlots := by
  unfold admitsB at h
  rw [hts] at h
  simp only [Bool.and_eq_true, beq_iff_eq] at h
  exact h.2.1

/-- **Every node of an admitted design is an admitted node.** -/
theorem admits_nodeAdmitted {D : DesignCert} (h : admitsB D = true) :
    ∀ c ∈ D.nodes.toList, NodeAdmitted c := by
  intro c hc
  obtain ⟨ts, hts⟩ := admitsB_infer h
  obtain ⟨j, hj⟩ := array_mem_getElem? hc
  have hjlt : j < D.nodes.size := Direct.lt_of_getElem?_some hj
  have hlt : D.sources.size + j < ts.size := by
    rw [admitsB_size h hts]; unfold DesignCert.numSlots; omega
  have hty := admits_node_ty hts (admits_wf h) hj
  rw [Array.getElem?_eq_getElem hlt] at hty
  exact nodeTy_admits hty

/-- **A write's output slot inherits the image's geometry**, which is
`WritePropagates` for the geometry the checker itself produced. -/
theorem admits_writePropagates {D : DesignCert} (h : admitsB D = true) :
    ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
      WritePropagates D (geomAt D) j c := by
  intro j c hc bw hop d0 hd0
  obtain ⟨ts, hts⟩ := admitsB_infer h
  have hsem := admits_semwf h
  obtain ⟨e0, e1, e2, e3, hdeps⟩ := memWriteBE_deps hsem hc hop
  have hjlt : j < D.nodes.size := Direct.lt_of_getElem?_some hc
  have hlt : D.sources.size + j < ts.size := by
    rw [admitsB_size h hts]; unfold DesignCert.numSlots; omega
  have hty := admits_node_ty hts (admits_wf h) hc
  rw [Array.getElem?_eq_getElem hlt] at hty
  obtain ⟨aw, dw, wa, hres, hop0, -, -⟩ := nodeTy_write_geom hop hdeps hty
  have he0 : e0 = d0 := by
    have : c.deps[0]? = some e0 := by rw [deps_get c hdeps 0]; rfl
    rw [this] at hd0
    exact (Option.some.inj hd0)
  subst he0
  unfold geomAt tyAt
  rw [hts]
  simp only [Option.bind_some]
  rw [hop0, Array.getElem?_eq_getElem hlt, hres]

/-- **The arity the interpreter requires follows from the arity the CHECKER
already verified.**  `Direct.opArity` and `Hw.RequiredArity` agree wherever the
latter demands anything, which is the nine fixed-arity operators. -/
theorem arityOK_of_opArity {op : LGraphOp} {ds : List Nat}
    (h : Direct.opArity op ds.length = true) : ArityOK op ds := by
  unfold ArityOK
  cases op <;> simp_all [Direct.opArity, RequiredArity]

theorem admits_arity {D : DesignCert} (h : admitsB D = true) :
    ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList := by
  intro c hc
  obtain ⟨j, hj⟩ := array_mem_getElem? hc
  have hsh := opArity_of_semwf (admits_semwf h) hj
  refine arityOK_of_opArity ?_
  simpa using hsh

/-- Everything `mainM_adequate_resolved` needs that is STATIC.  Only
`WriteAddrFits` is absent: it is a runtime condition, derived from the inferred
widths in the next increment and not asserted here. -/
def StaticPremises (D : DesignCert) : Prop :=
  DesignCert.DesignCertWF D
  ∧ Direct.DesignSemWF D
  ∧ (∀ k, k < D.sources.size → geomAt D k = srcGeomOf D k)
  ∧ (∀ c ∈ D.nodes.toList, NodeAdmitted c)
  ∧ (∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
  ∧ (∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
       WritePropagates D (geomAt D) j c)
  ∧ NextImgGeomOK D (geomAt D)

/-- **The static premises, from one Boolean check.** -/
theorem admits_static_premises {D : DesignCert} (h : admitsB D = true) :
    StaticPremises D :=
  ⟨admits_wf h, admits_semwf h, admits_geom_src h, admits_nodeAdmitted h,
   admits_arity h, admits_writePropagates h, admits_nextImgGeom h⟩

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

/-! ### Soundness, APPLIED to the retained designs

`admitsB` DOES reduce in the kernel — `decide +kernel` evaluates the structural
fold.  An earlier version of this module said the fold "does not evaluate in
the kernel"; that was wrong.  What had failed was default `decide`/`rfl`, which
is a different thing and does not license the stronger claim. -/

theorem mixedDesign_admits : admitsB mixedDesign = true := by decide +kernel
theorem twoMemDesign_admits : admitsB twoMemDesign = true := by decide +kernel
theorem laneDesign_admits : admitsB laneDesign = true := by decide +kernel

/-- The general soundness lemmas, applied — these are theorems about the
designs, not `#guard` evaluations. -/
theorem mixedDesign_semwf' : Direct.DesignSemWF mixedDesign :=
  admits_semwf mixedDesign_admits

theorem mixedDesign_nextImgGeom : NextImgGeomOK mixedDesign (geomAt mixedDesign) :=
  admits_nextImgGeom mixedDesign_admits

theorem mixedDesign_geom_src :
    ∀ k, k < mixedDesign.sources.size → geomAt mixedDesign k = srcGeomOf mixedDesign k :=
  admits_geom_src mixedDesign_admits

theorem twoMemDesign_semwf' : Direct.DesignSemWF twoMemDesign :=
  admits_semwf twoMemDesign_admits

theorem twoMemDesign_nextImgGeom : NextImgGeomOK twoMemDesign (geomAt twoMemDesign) :=
  admits_nextImgGeom twoMemDesign_admits

theorem twoMemDesign_geom_src :
    ∀ k, k < twoMemDesign.sources.size → geomAt twoMemDesign k = srcGeomOf twoMemDesign k :=
  admits_geom_src twoMemDesign_admits

theorem laneDesign_semwf : Direct.DesignSemWF laneDesign :=
  admits_semwf laneDesign_admits

theorem laneDesign_nextImgGeom : NextImgGeomOK laneDesign (geomAt laneDesign) :=
  admits_nextImgGeom laneDesign_admits

/-- **The whole static bundle, instantiated.**  Not a `#guard`: these are the
premises `mainM_adequate_resolved` consumes, obtained from the checker. -/
theorem mixedDesign_static : StaticPremises mixedDesign :=
  admits_static_premises mixedDesign_admits
theorem twoMemDesign_static : StaticPremises twoMemDesign :=
  admits_static_premises twoMemDesign_admits
theorem laneDesign_static : StaticPremises laneDesign :=
  admits_static_premises laneDesign_admits

#print axioms admits_semwf
#print axioms admits_wf
#print axioms admits_geom_src
#print axioms admits_nextImgGeom
#print axioms nodeTy_congr
#print axioms infer_foldl_node
#print axioms admits_node_ty
#print axioms nodeTy_admits
#print axioms nodeTy_write_geom
#print axioms admits_nodeAdmitted
#print axioms admits_writePropagates
#print axioms arityOK_of_opArity
#print axioms admits_arity
#print axioms admits_static_premises
#print axioms mixedDesign_static
#print axioms twoMemDesign_static
#print axioms laneDesign_static
#print axioms mixedDesign_admits
#print axioms twoMemDesign_admits
#print axioms laneDesign_admits
#print axioms mixedDesign_nextImgGeom
#print axioms mixedDesign_geom_src
#print axioms twoMemDesign_nextImgGeom
#print axioms laneDesign_nextImgGeom

end HwMem
end Projection
