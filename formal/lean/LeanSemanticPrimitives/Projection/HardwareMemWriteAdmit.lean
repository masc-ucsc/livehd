/-
# The ordinary `Op_MemWrite` route: typing and admission, as an EXPLICIT EXTENSION

`HardwareMemWrite` proved the reference-level facts the plain write needs and
said what was still missing: "an object primitive, an `applyOpM` dispatch, the
checker's admission and typing, and the node/fold lemmas."  The primitive and
the dispatch landed with the object layer.  This module adds the CHECKER's
typing and admission.

## The original contract is preserved, not renamed away

`NodeAdmitted`, `nodeAdmittedB` and `NodeAdmitted_ne_memWrite` are untouched and
still refuse `Op_MemWrite`; `nodeTy` is untouched and still returns `none` for
it (`nodeTy_memWrite_none` below restates that as a theorem rather than leaving
it implicit).  The extension is a SEPARATE typing function `nodeTyW`, built so
that:

* on every node that is not an `Op_MemWrite`, `nodeTyW ty c` is `nodeTy ty c`
  -- definitionally, by delegation, so no case can silently drift
  (`nodeTyW_eq_of_ne`);
* on a design containing no plain write, the whole extended checker IS the
  original one (`inferTysW_eq`, `admitsWB_eq`), so nothing previously admitted
  or refused changes its answer.

## The plain write's own semantics are typed DIRECTLY

`Op_MemWriteBE` carries a lane geometry and its typing enforces one:
`0 < bw` and `dw ≤ bvWidthOf tbe * bw` relate the enable vector to the byte
lanes.  The plain write has NO lanes.  Its enable is read by `bv_nonzero` over
the WHOLE vector and its datum is stored RAW, so:

* the enable is required to be a bit vector and NOTHING else -- no width
  relation, because no width relation is true of it.  Demanding a one-bit
  enable, or reducing the write to a one-lane `Op_MemWriteBE`, would both state
  something the operator does not mean;
* the datum must be EXACTLY `dw` wide (`bvWidthOf td == dw`, not `≤`), because
  `cert_mem_write` stores it without resizing -- this is precisely the side
  condition `cert_mem_write_width` was left holding in `HardwareMemWrite`.
-/
import LeanSemanticPrimitives.Projection.HardwareMemWrite
import LeanSemanticPrimitives.Projection.HardwareMemSlotTy
import LeanSemanticPrimitives.Projection.MemProjection
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw
-- the specialization layer lives in `Projection.MemProjection`; §3i reuses it
open MemProjection

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. The extended typing -/

/-- `nodeTy` extended with the ordinary write.  Every other operator DELEGATES,
so the two agree by construction rather than by a parallel transcription. -/
def nodeTyWBody (ty : Nat → Option SlotTy) (c : DenseNodeCert) : Option SlotTy :=
  match c.deps.toList.map ty with
  | [tm, ta, td, ten] =>
      -- `ten` is only required to BE a bit vector: `bv_nonzero` reads all of it.
      if isBvTy ta && isBvTy td && isBvTy ten then
        match tm with
        | some (.tbl aw dw) =>
            if bvWidthOf ta ≤ aw && c.width == dw && bvWidthOf td == dw
            then some (.tbl aw dw) else none
        | _ => none
      else none
  | _ => none

/-- The delegation is an `if`, not a match arm, so "off the plain write this IS
`nodeTy`" is `if_neg` rather than a 30-case analysis. -/
def nodeTyW (ty : Nat → Option SlotTy) (c : DenseNodeCert) : Option SlotTy :=
  if c.op = .Op_MemWrite then nodeTyWBody ty c else nodeTy ty c

/-- The ORIGINAL typing refuses the plain write.  Stated, not left implicit. -/
theorem nodeTy_memWrite_none {ty : Nat → Option SlotTy} {c : DenseNodeCert}
    (h : c.op = .Op_MemWrite) : nodeTy ty c = none := by
  unfold nodeTy nodeAdmittedB
  rw [h]
  simp [OpSupported]

/-- Off the plain write, the extension IS the original. -/
theorem nodeTyW_eq_of_ne {ty : Nat → Option SlotTy} {c : DenseNodeCert}
    (h : c.op ≠ .Op_MemWrite) : nodeTyW ty c = nodeTy ty c := by
  unfold nodeTyW; rw [if_neg h]

theorem nodeTyW_memWrite {ty : Nat → Option SlotTy} {c : DenseNodeCert}
    (h : c.op = .Op_MemWrite) : nodeTyW ty c = nodeTyWBody ty c := by
  unfold nodeTyW; rw [if_pos h]

/-- …and a design with no plain write cannot tell the two apart. -/
theorem nodeTyW_eq_nodeTy_iff {ty : Nat → Option SlotTy} {c : DenseNodeCert} :
    nodeTyW ty c = nodeTy ty c ∨ c.op = .Op_MemWrite := by
  by_cases h : c.op = .Op_MemWrite
  · exact Or.inr h
  · exact Or.inl (nodeTyW_eq_of_ne h)

/-! ## 2. What a successful extended typing gives -/

/-- A typed node is admitted by the EXTENDED predicate. -/
theorem nodeTyW_admits {ty : Nat → Option SlotTy} {c : DenseNodeCert} {t : SlotTy}
    (h : nodeTyW ty c = some t) : NodeAdmittedW c := by
  by_cases hop : c.op = .Op_MemWrite
  · exact Or.inr hop
  · rw [nodeTyW_eq_of_ne hop] at h
    exact NodeAdmitted_toW (nodeTy_admits h)

/-- The plain write's body only ever answers a TABLE. -/
theorem nodeTyWBody_result {ty : Nat → Option SlotTy} {c : DenseNodeCert} {t : SlotTy}
    (h : nodeTyWBody ty c = some t) : ∃ aw dw, t = .tbl aw dw := by
  unfold nodeTyWBody at h
  split at h
  · split at h
    · split at h
      · split at h
        · rename_i aw' dw' _ _
          exact ⟨aw', dw', (Option.some.inj h).symm⟩
        · simp at h
      · simp at h
    · simp at h
  · simp at h

/-- The result shape, for the extended typing. -/
theorem nodeTyW_result {ty : Nat → Option SlotTy} {c : DenseNodeCert} {t : SlotTy}
    (h : nodeTyW ty c = some t) : t = .bv c.width ∨ ∃ aw dw, t = .tbl aw dw := by
  by_cases hop : c.op = .Op_MemWrite
  · rw [nodeTyW_memWrite hop] at h
    obtain ⟨aw, dw, ht⟩ := nodeTyWBody_result h
    exact Or.inr ⟨aw, dw, ht⟩
  · rw [nodeTyW_eq_of_ne hop] at h
    exact nodeTy_result h

theorem nodeTyW_bv_width {ty : Nat → Option SlotTy} {c : DenseNodeCert} {w : Nat}
    (h : nodeTyW ty c = some (.bv w)) : w = c.width := by
  rcases nodeTyW_result h with h' | ⟨aw, dw, h'⟩
  · exact (SlotTy.bv.inj h')
  · simp at h'

/-- A node typing `.bv` is NOT a plain write -- derived from the typing, exactly
as the byte-enabled write's exclusion is, rather than from an exclusion
theorem.  This is what lets the extended route keep `NodeAdmitted_ne_memWrite`
untouched: the scalar-width agreement never needed the exclusion, only the
fact that a write types a table. -/
theorem nodeTyW_bv_ne_memWrite {ty : Nat → Option SlotTy} {c : DenseNodeCert}
    {w : Nat} (h : nodeTyW ty c = some (.bv w)) : c.op ≠ .Op_MemWrite := by
  intro hop
  rw [nodeTyW_memWrite hop] at h
  obtain ⟨aw, dw, ht⟩ := nodeTyWBody_result h
  simp at ht

/-- The plain write's geometry, read off a successful typing.  The ENABLE is
deliberately absent from the conclusion: there is no width relation to report,
because `bv_nonzero` imposes none. -/
theorem nodeTyW_write_geom {ty : Nat → Option SlotTy} {c : DenseNodeCert}
    {d0 d1 d2 d3 : Nat} {t : SlotTy}
    (hop : c.op = .Op_MemWrite) (hdeps : c.deps.toList = [d0, d1, d2, d3])
    (h : nodeTyW ty c = some t) :
    ∃ aw dw wa, t = .tbl aw dw ∧ ty d0 = some (.tbl aw dw)
              ∧ ty d1 = some (.bv wa) ∧ wa ≤ aw
              ∧ ty d2 = some (.bv dw) ∧ c.width = dw := by
  rw [nodeTyW_memWrite hop] at h
  unfold nodeTyWBody at h
  rw [hdeps] at h
  simp only [List.map_cons, List.map_nil] at h
  split at h
  · rename_i hguard
    split at h
    · rename_i aw' dw' heq
      split at h
      · rename_i hcond
        simp only [Bool.and_eq_true, decide_eq_true_eq] at hguard hcond
        simp only [Option.some.injEq] at h
        -- `&&` is LEFT-associative, so the guard is `(ta ∧ td) ∧ ten`
        obtain ⟨⟨hta, htd⟩, hten⟩ := hguard
        refine ⟨aw', dw', bvWidthOf (ty d1), h.symm, heq, isBvTy_elim hta, hcond.1.1, ?_, ?_⟩
        · -- the datum types `.bv dw`, because its width is checked EQUAL to dw
          have hdw : bvWidthOf (ty d2) = dw' := by
            have := hcond.2; simpa using this
          have := isBvTy_elim htd
          rw [← hdw]; exact this
        · have := hcond.1.2; simpa using this
      · simp at h
    · simp at h
  · simp at h

/-! ## 3. The extended checker

Same shape as `inferTys`/`admitsB`, over `nodeTyW`.  The two agree on every
design with no plain write (`inferTysW_eq`, `admitsWB_eq`), so the extension
cannot change an existing answer. -/

def inferStepW (ts : Array SlotTy) (c : DenseNodeCert) : Option (Array SlotTy) :=
  (nodeTyW (fun d => ts[d]?) c).map ts.push

def inferTysW (D : DesignCert) : Option (Array SlotTy) :=
  D.nodes.toList.foldl (fun acc c => acc.bind (fun ts => inferStepW ts c))
    (some (D.sources.map srcTy))

def tyAtW (D : DesignCert) (k : Nat) : Option SlotTy :=
  (inferTysW D).bind (fun ts => ts[k]?)

def geomAtW (D : DesignCert) (k : Nat) : Option Nat :=
  match tyAtW D k with
  | some (.tbl aw _) => some aw
  | _                => none

/-- `admitsB`'s tail, named once.  A `match` written twice elaborates to two
DIFFERENT auxiliary functions, so comparing two inline copies is not an `rfl`
even when they read identically -- `admitsB_tysOK` below pins the original's
tail to this one, and the extended checker then reuses it. -/
def tysOK (D : DesignCert) : Option (Array SlotTy) → Bool
  | none    => false
  | some ts =>
      ts.size == D.numSlots
      && D.memories.toList.all (fun md =>
           match ts[md.nextImg]? with
           | some (.tbl aw dw) => aw == md.aw && dw == md.dw
           | _                 => false)

theorem admitsB_tysOK (D : DesignCert) :
    admitsB D = (Direct.acceptsDesign D && tysOK D (inferTys D)) := by
  unfold admitsB tysOK
  cases inferTys D <;> rfl

def admitsWB (D : DesignCert) : Bool :=
  Direct.acceptsDesign D && tysOK D (inferTysW D)

/-! ### The extension changes no existing answer -/

theorem inferStepW_eq_of_ne {ts : Array SlotTy} {c : DenseNodeCert}
    (h : c.op ≠ .Op_MemWrite) : inferStepW ts c = inferStep ts c := by
  unfold inferStepW inferStep
  rw [nodeTyW_eq_of_ne h]

theorem infer_foldlW_congr :
    ∀ (ns : List DenseNodeCert), (∀ c ∈ ns, c.op ≠ .Op_MemWrite) →
      ∀ (a : Option (Array SlotTy)),
        ns.foldl (fun acc c => acc.bind (fun ts => inferStepW ts c)) a
          = ns.foldl (fun acc c => acc.bind (fun ts => inferStep ts c)) a
  | [],      _, _ => rfl
  | c :: cs, h, a => by
      simp only [List.foldl_cons]
      have hc : c.op ≠ .Op_MemWrite := h c (List.mem_cons_self ..)
      have : (a.bind fun ts => inferStepW ts c) = (a.bind fun ts => inferStep ts c) := by
        cases a with
        | none    => rfl
        | some ts => simp only [Option.bind_some]; exact inferStepW_eq_of_ne hc
      rw [this]
      exact infer_foldlW_congr cs (fun x hx => h x (List.mem_cons_of_mem _ hx)) _

/-- **A design with no plain write gets the SAME inference.** -/
theorem inferTysW_eq {D : DesignCert} (h : ∀ c ∈ D.nodes.toList, c.op ≠ .Op_MemWrite) :
    inferTysW D = inferTys D := by
  unfold inferTysW inferTys
  exact infer_foldlW_congr D.nodes.toList h _

/-- **…and therefore the same admission answer.**  Nothing previously admitted
becomes refused, and nothing previously refused becomes admitted, except by
containing the operator the extension is about. -/
theorem admitsWB_eq {D : DesignCert} (h : ∀ c ∈ D.nodes.toList, c.op ≠ .Op_MemWrite) :
    admitsWB D = admitsB D := by
  rw [admitsB_tysOK, admitsWB, inferTysW_eq h]

/-! ### Soundness of the extended walk

The same four facts the original walk provides, re-proved for `inferStepW`.
They are structural in the node list and do not look inside the typing, so the
plain write rides along without a special case. -/

theorem inferStepW_prefix {ts ts' : Array SlotTy} {c : DenseNodeCert}
    (h : inferStepW ts c = some ts') :
    ts.size < ts'.size ∧ ∀ k, k < ts.size → ts'[k]? = ts[k]? := by
  unfold inferStepW at h
  cases hn : nodeTyW (fun d => ts[d]?) c with
  | none   => rw [hn] at h; simp at h
  | some t =>
      rw [hn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      subst h
      refine ⟨by simp, ?_⟩
      intro k hk
      simp [Array.getElem?_push, hk]
      omega

theorem infer_foldlW_none :
    ∀ (ns : List DenseNodeCert),
      ns.foldl (fun acc c => acc.bind (fun ts => inferStepW ts c)) none = none
  | []      => rfl
  | _ :: cs => by simpa using infer_foldlW_none cs

theorem infer_foldlW_prefix :
    ∀ (ns : List DenseNodeCert) (init ts : Array SlotTy),
      ns.foldl (fun acc c => acc.bind (fun t => inferStepW t c)) (some init) = some ts →
      ∀ k, k < init.size → ts[k]? = init[k]?
  | [],      init, ts, h, k, _  => by
      simp only [List.foldl_nil, Option.some.injEq] at h
      subst h; rfl
  | c :: cs, init, ts, h, k, hk => by
      simp only [List.foldl_cons, Option.bind_some] at h
      cases hs : inferStepW init c with
      | none   => rw [hs, infer_foldlW_none] at h; simp at h
      | some t =>
          rw [hs] at h
          obtain ⟨hlt, hpre⟩ := inferStepW_prefix hs
          rw [infer_foldlW_prefix cs t ts h k (by omega)]
          exact hpre k hk

theorem tyAtW_source {D : DesignCert} {k : Nat} (hk : k < D.sources.size)
    {ts : Array SlotTy} (hts : inferTysW D = some ts) :
    tyAtW D k = some (srcTy (D.sources[k]'hk)) := by
  unfold tyAtW
  rw [hts]
  simp only [Option.bind_some]
  rw [infer_foldlW_prefix D.nodes.toList _ ts hts k (by simpa using hk)]
  simp [Array.getElem?_eq_getElem hk]

theorem admitsWB_infer {D : DesignCert} (h : admitsWB D = true) :
    ∃ ts, inferTysW D = some ts := by
  unfold admitsWB at h
  simp only [Bool.and_eq_true] at h
  cases hts : inferTysW D with
  | none   => rw [hts] at h; simp [tysOK] at h
  | some t => exact ⟨t, rfl⟩

theorem admitsWB_acceptsDesign {D : DesignCert} (h : admitsWB D = true) :
    Direct.acceptsDesign D = true := by
  unfold admitsWB at h
  simp only [Bool.and_eq_true] at h
  exact h.1

/-! ## 3b. The object-level bridges for the plain write

`evalPrim_memWriteBE` has the two statements the byte-enabled write needs; these
are the same two for the plain one.  The argument list is FOUR entries -- image,
address, datum, enable -- with no width and no payload, because the plain write
takes neither: that is the operator's own shape, not a byte-enabled write with
the lane arguments dropped. -/

theorem evalPrim_memWrite (m : MemImg) (a d en : BV)
    (hen : bv_nonzero en) (hr : MemImg.InRange m (bv_uint a)) :
    evalPrim .memWrite [MemImg.encMemImg m, ofBV a, ofBV d, ofBV en]
      = .ok (MemImg.encMemImg (MemImg.writeEn m a d en)) := by
  simp [evalPrim, MemImg.valMemWrite_eq m a d en hen hr]

theorem evalPrim_memWrite_disabled (m : MemImg) (a d en : BV)
    (hen : ¬ bv_nonzero en) :
    evalPrim .memWrite [MemImg.encMemImg m, ofBV a, ofBV d, ofBV en]
      = .ok (MemImg.encMemImg m) := by
  simp [evalPrim, MemImg.valMemWrite_disabled m a d en hen]

/-! ## 3c. `applyOpM` on opcode 27

The dispatch reaches the plain write one `ite` EARLIER than the byte-enabled
one: past 26, then taken at 27. -/

theorem applyOpM_memWrite {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w d0 d1 d2 d3 : Nat} {m : MemImg} {a dat enb : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV enb)
    (hen : bv_nonzero enb) (hr : MemImg.InRange m (bv_uint a))
    (hop : SEval hwMemS σ eo (encOp .Op_MemWrite))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2, d3]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en])
      (MemImg.encMemImg (MemImg.writeEn m a dat enb)) := by
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
  have hv1' : vals[d1]'hb1 = ofBV a := hv1
  have hv2' : vals[d2]'hb2 = ofBV dat := hv2
  have hv3' : vals[d3]'hb3 = ofBV enb := hv3
  refine .prim ?_ (evalPrim_memWrite m a dat enb hen hr)
  refine .cons ?_ (.cons ?_ (.cons ?_ (.cons ?_ .nil)))
  · rw [← hv0]; exact lift (slot_agree hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
  · rw [← hv1']
    exact lift (slot_agree hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))
  · rw [← hv2']
    exact lift (slot_agree hb2 (.ref rfl) (.ref rfl)
      (SEval_hd (SEval_tl (SEval_tl (.ref rfl)))))
  · rw [← hv3']
    exact lift (slot_agree hb3 (.ref rfl) (.ref rfl)
      (SEval_hd (SEval_tl (SEval_tl (SEval_tl (.ref rfl))))))

/-- A DISABLED plain write: no range premise, and the image comes back EXACTLY.
The second conjunct is the reference-side counterpart, from
`cert_mem_write_disabled`. -/
theorem applyOpM_memWrite_disabled {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w d0 d1 d2 d3 : Nat} {m : MemImg} {a dat enb : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV enb)
    (hen : ¬ bv_nonzero enb)
    (hop : SEval hwMemS σ eo (encOp .Op_MemWrite))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2, d3]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) (MemImg.encMemImg m)
    ∧ MemImg.denote m = cert_mem_write (MemImg.denote m) a dat enb := by
  constructor
  · refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
    refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
    refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
    refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
    have hv1' : vals[d1]'hb1 = ofBV a := hv1
    have hv2' : vals[d2]'hb2 = ofBV dat := hv2
    have hv3' : vals[d3]'hb3 = ofBV enb := hv3
    refine .prim ?_ (evalPrim_memWrite_disabled m a dat enb hen)
    refine .cons ?_ (.cons ?_ (.cons ?_ (.cons ?_ .nil)))
    · rw [← hv0]; exact lift (slot_agree hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
    · rw [← hv1']
      exact lift (slot_agree hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))
    · rw [← hv2']
      exact lift (slot_agree hb2 (.ref rfl) (.ref rfl)
        (SEval_hd (SEval_tl (SEval_tl (.ref rfl)))))
    · rw [← hv3']
      exact lift (slot_agree hb3 (.ref rfl) (.ref rfl)
        (SEval_hd (SEval_tl (SEval_tl (SEval_tl (.ref rfl))))))
  · exact (cert_mem_write_disabled hen).symm

/-! ## 3d. The node level -/

theorem evalNodeM_memWrite {vals : List Val} {c : DenseNodeCert}
    {σ : SEnv} {end_ eenv en : SExp} {d0 d1 d2 d3 : Nat}
    {m : MemImg} {a dat enb : BV}
    (hop : c.op = .Op_MemWrite) (hdeps : c.deps.toList = [d0, d1, d2, d3])
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV enb)
    (hen : bv_nonzero enb) (hr : MemImg.InRange m (bv_uint a))
    (hnd : SEval hwMemS σ end_ (encNode c))
    (henv : SEval hwMemS σ eenv (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en])
      (MemImg.encMemImg (MemImg.writeEn m a dat enb)) := by
  refine SEval_call3 hnd henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine applyOpM_memWrite (w := c.width) hb0 hb1 hb2 hb3 hv0 hv1 hv2 hv3 hen hr
    ?_ (.ref rfl) ?_ (.ref rfl) (.ref rfl)
  · rw [← hop]; exact .ref rfl
  · refine .ref ?_
    simp [slookup, encArr, hdeps]

theorem evalNodeM_memWrite_disabled {vals : List Val} {c : DenseNodeCert}
    {σ : SEnv} {end_ eenv en : SExp} {d0 d1 d2 d3 : Nat}
    {m : MemImg} {a dat enb : BV}
    (hop : c.op = .Op_MemWrite) (hdeps : c.deps.toList = [d0, d1, d2, d3])
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV enb)
    (hen : ¬ bv_nonzero enb)
    (hnd : SEval hwMemS σ end_ (encNode c))
    (henv : SEval hwMemS σ eenv (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "evalNodeM" [end_, eenv, en]) (MemImg.encMemImg m) := by
  refine SEval_call3 hnd henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine (applyOpM_memWrite_disabled (w := c.width)
    hb0 hb1 hb2 hb3 hv0 hv1 hv2 hv3 hen ?_ (.ref rfl) ?_ (.ref rfl) (.ref rfl)).1
  · rw [← hop]; exact .ref rfl
  · refine .ref ?_
    simp [slookup, encArr, hdeps]

/-! ## 3e. The fold

`WritePropagates` and `WriteAddrFits` are stated for `Op_MemWriteBE` and keep
their exact meanings.  The extended versions CONJOIN the plain write's own
obligation rather than generalising the originals, so a caller holding the
original still holds exactly what it held.

The address obligation is the same one, because it is about the ADDRESS, which
both writes have.  The ENABLE condition is the plain write's own
`bv_nonzero` over the whole operand -- not a lane-mask bit. -/

def WritePropagatesW (D : DesignCert) (G : Nat → Option Nat) (j : Nat)
    (c : DenseNodeCert) : Prop :=
  WritePropagates D G j c
  ∧ (c.op = .Op_MemWrite → ∀ d0, c.deps[0]? = some d0 →
       G (D.sources.size + j) = G d0)

def WriteAddrFitsW (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (G : Nat → Option Nat) (c : DenseNodeCert) : Prop :=
  WriteAddrFits D i st G c
  ∧ (c.op = .Op_MemWrite → ∀ d0 d1 d3,
       c.deps[0]? = some d0 → c.deps[1]? = some d1 → c.deps[3]? = some d3 →
       bv_nonzero (slotValM D i st d3).asBV = true →
       ∃ aw, G d0 = some aw ∧ (slotValM D i st d1).asBV.width ≤ aw)

/-- `HardwareMemFold`'s copy is `private`, so the same statement is proved here
rather than reached into. -/
theorem list_len_four' {α : Type} : ∀ {l : List α}, l.length = 4 →
    ∃ a b c d, l = [a, b, c, d]
  | [_, _, _, _], _ => ⟨_, _, _, _, rfl⟩

theorem memWrite_deps {D : DesignCert} (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hop : c.op = .Op_MemWrite) :
    ∃ d0 d1 d2 d3, c.deps.toList = [d0, d1, d2, d3] := by
  have h := opArity_of_semwf hsem hc
  rw [hop] at h
  simp only [Direct.opArity, beq_iff_eq] at h
  exact list_len_four' (by simpa using h)

/-- One ordinary-write node, enabled or disabled.  The same two branches the
byte-enabled write has: ENABLED consumes `InRange`, derived from the actual
witness's admitted geometry; DISABLED consumes nothing and returns the image
EXACTLY. -/
theorem node_step_memWrite {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert}
    (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hop : c.op = .Op_MemWrite)
    (hprop : c.op = .Op_MemWrite → ∀ d0, c.deps[0]? = some d0 →
               G (D.sources.size + j) = G d0)
    (hfits : c.op = .Op_MemWrite → ∀ d0 d1 d3,
               c.deps[0]? = some d0 → c.deps[1]? = some d1 → c.deps[3]? = some d3 →
               bv_nonzero (slotValM D i st d3).asBV = true →
               ∃ aw, G d0 = some aw ∧ (slotValM D i st d1).asBV.width ≤ aw)
    {ss : List SlotV} (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
    ∃ v : SlotV, NodeStepOK D i st G j c ss v := by
  obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWrite_deps hsem hc hop
  have hm0 : d0 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm1 : d1 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm2 : d2 ∈ c.deps.toList := by rw [hdeps]; simp
  have hm3 : d3 ∈ c.deps.toList := by rw [hdeps]; simp
  have hb0 := deps_bound hwf hc hpre d0 hm0
  have hb1 := deps_bound hwf hc hpre d1 hm1
  have hb2 := deps_bound hwf hc hpre d2 hm2
  have hb3 := deps_bound hwf hc hpre d3 hm3
  have hp0 : c.deps[0]? = some d0 := by rw [deps_get c hdeps 0]; rfl
  have hp1 : c.deps[1]? = some d1 := by rw [deps_get c hdeps 1]; rfl
  have hp2 : c.deps[2]? = some d2 := by rw [deps_get c hdeps 2]; rfl
  have hp3 : c.deps[3]? = some d3 := by rw [deps_get c hdeps 3]; rfl
  have hk0 : Direct.slotIsMem D d0 = some true := by
    have := dep_kind_of_semwf hsem hc hp0; rwa [hop] at this
  have hk1 : Direct.slotIsMem D d1 = some false := by
    have := dep_kind_of_semwf hsem hc hp1; rwa [hop] at this
  have hk2 : Direct.slotIsMem D d2 = some false := by
    have := dep_kind_of_semwf hsem hc hp2; rwa [hop] at this
  have hk3 : Direct.slotIsMem D d3 = some false := by
    have := dep_kind_of_semwf hsem hc hp3; rwa [hop] at this
  obtain ⟨img, hslot0, henc0, hden0⟩ := dep_mem hpre hb0 hk0
  have henc1 := dep_scalar hpre hb1 hk1
  have henc2 := dep_scalar hpre hb2 hk2
  have henc3 := dep_scalar hpre hb3 hk3
  have hrec := nodeValM_rec (i := i) (st := st) hwf hj hc
  rw [hop, hdeps] at hrec
  have hkindnode : Direct.slotIsMem D (D.sources.size + j) = some true := by
    rw [nodeSlot_slotIsMem hc, hop]; rfl
  have hGout : G (D.sources.size + j) = G d0 := hprop hop d0 hp0
  by_cases hen : bv_nonzero (slotValM D i st d3).asBV
  · obtain ⟨aw, hG0, hwid⟩ := hfits hop d0 d1 d3 hp0 hp1 hp3 hen
    have hfull : MemImg.FullTable aw img := hpre.geom d0 hb0 img hslot0 aw hG0
    have hr : MemImg.InRange img (bv_uint (slotValM D i st d1).asBV) :=
      MemImg.InRange_of_FullTable_le hfull hwid
    refine ⟨.img (MemImg.writeEn img (slotValM D i st d1).asBV
        (slotValM D i st d2).asBV (slotValM D i st d3).asBV), ?_, ?_, ?_, ?_⟩
    · rw [← hrec, SlotV.denote, MemImg.denote_writeEn _ _ _ _ hr, hden0]
      rfl
    · rw [hkindnode]; rfl
    · intro g hg aw' hGaw'
      simp only [SlotV.img.injEq] at hg
      rw [hGout, hG0] at hGaw'
      have : aw' = aw := (Option.some.inj hGaw').symm
      subst this
      rw [← hg]
      exact writeEn_FullTable hfull _ _ _
    · intro σ end_ eenv en hnd henv hlen
      exact evalNodeM_memWrite hop hdeps (by simpa using hb0) (by simpa using hb1)
        (by simpa using hb2) (by simpa using hb3) henc0 henc1 henc2 henc3 hen hr
        hnd henv (by simpa using hlen)
  · refine ⟨.img img, ?_, ?_, ?_, ?_⟩
    · rw [← hrec, SlotV.denote, hden0]
      show CertVal.mem (slotValM D i st d0).asMem
         = CertVal.mem (cert_mem_write (slotValM D i st d0).asMem
             (slotValM D i st d1).asBV (slotValM D i st d2).asBV
             (slotValM D i st d3).asBV)
      simp [cert_mem_write, hen]
    · rw [hkindnode]; rfl
    · intro g hg aw' hGaw'
      simp only [SlotV.img.injEq] at hg
      rw [← hg]
      rw [hGout] at hGaw'
      exact hpre.geom d0 hb0 img hslot0 aw' hGaw'
    · intro σ end_ eenv en hnd henv hlen
      exact evalNodeM_memWrite_disabled hop hdeps (by simpa using hb0)
        (by simpa using hb1) (by simpa using hb2) (by simpa using hb3)
        henc0 henc1 henc2 henc3 hen hnd henv (by simpa using hlen)

/-- **One admitted node on the EXTENDED route.**  The original `node_step` is
reused untouched for everything it already covered; only the plain write is
new. -/
theorem node_stepW {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) (hj : j < D.nodes.size)
    (hadm : NodeAdmittedW c) (harity : ArityOK c.op c.deps.toList)
    (hprop : WritePropagatesW D G j c) (hfits : WriteAddrFitsW D i st G c)
    {ss : List SlotV} (hpre : PrefixRelM D i st G (D.sources.size + j) ss) :
    ∃ v : SlotV, NodeStepOK D i st G j c ss v := by
  rcases hadm with hadm | hop
  · exact node_step hwf hsem hc hj hadm harity hprop.1 hfits.1 hpre
  · exact node_step_memWrite hwf hsem hc hj hop hprop.2 hfits.2 hpre

/-! ## 3f. Soundness of the extended walk, and the static premises

The same recurrence `HardwareMemAdmit` proves for `nodeTy`/`inferStep`, for
`nodeTyW`/`inferStepW`.  None of it looks inside the typing, so the plain write
rides along; what differs is only that the conclusions name `NodeAdmittedW`,
`WritePropagatesW` and `WriteAddrFitsW`. -/

theorem nodeTyW_congr {ty ty' : Nat → Option SlotTy} {c : DenseNodeCert}
    (h : ∀ d ∈ c.deps.toList, ty d = ty' d) : nodeTyW ty c = nodeTyW ty' c := by
  unfold nodeTyW nodeTyWBody
  rw [List.map_congr_left h, nodeTy_congr h]

theorem inferStepW_size {ts ts' : Array SlotTy} {c : DenseNodeCert}
    (h : inferStepW ts c = some ts') : ts'.size = ts.size + 1 := by
  unfold inferStepW at h
  cases hn : nodeTyW (fun d => ts[d]?) c with
  | none   => rw [hn] at h; simp at h
  | some t =>
      rw [hn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      subst h; simp

theorem infer_foldlW_size :
    ∀ (ns : List DenseNodeCert) (init ts : Array SlotTy),
      ns.foldl (fun acc c => acc.bind (fun t => inferStepW t c)) (some init) = some ts →
      ts.size = init.size + ns.length
  | [],      init, ts, h => by
      simp only [List.foldl_nil, Option.some.injEq] at h; subst h; simp
  | c :: cs, init, ts, h => by
      simp only [List.foldl_cons, Option.bind_some] at h
      cases hs : inferStepW init c with
      | none   => rw [hs, infer_foldlW_none] at h; simp at h
      | some t =>
          rw [hs] at h
          have hsz := inferStepW_size hs
          rw [infer_foldlW_size cs t ts h, hsz, List.length_cons]
          omega

theorem infer_foldlW_node :
    ∀ (ns : List DenseNodeCert) (init ts : Array SlotTy),
      ns.foldl (fun acc c => acc.bind (fun t => inferStepW t c)) (some init) = some ts →
      ∀ (j : Nat) (c : DenseNodeCert), ns[j]? = some c →
        (∀ d ∈ c.deps.toList, d < init.size + j) →
        nodeTyW (fun d => ts[d]?) c = ts[init.size + j]?
  | [],       _,    _,  _, j, _, hj, _ => by simp at hj
  | c0 :: cs, init, ts, h, j, c, hj, hb => by
      simp only [List.foldl_cons, Option.bind_some] at h
      cases hs : inferStepW init c0 with
      | none   => rw [hs, infer_foldlW_none] at h; simp at h
      | some t =>
          rw [hs] at h
          have hsz := inferStepW_size hs
          have hpre := infer_foldlW_prefix cs t ts h
          cases j with
          | zero =>
              have hc0 : c0 = c := by simpa using hj
              subst hc0
              have hx : nodeTyW (fun d => init[d]?) c0 = t[init.size]? := by
                unfold inferStepW at hs
                cases hn : nodeTyW (fun d => init[d]?) c0 with
                | none   => rw [hn] at hs; simp at hs
                | some x =>
                    rw [hn] at hs
                    simp only [Option.map_some, Option.some.injEq] at hs
                    subst hs
                    simp
              have hcong : nodeTyW (fun d => ts[d]?) c0 = nodeTyW (fun d => init[d]?) c0 := by
                refine nodeTyW_congr ?_
                intro d hd
                have hdlt : d < init.size := by simpa using hb d hd
                rw [hpre d (by omega), (inferStepW_prefix hs).2 d hdlt]
              rw [hcong, hx, Nat.add_zero]
              exact (hpre init.size (by omega)).symm
          | succ q =>
              have hq : cs[q]? = some c := by simpa using hj
              have hb' : ∀ d ∈ c.deps.toList, d < t.size + q := by
                intro d hd
                have := hb d hd
                omega
              have := infer_foldlW_node cs t ts h q c hq hb'
              rw [this]
              congr 1
              omega

theorem admits_node_tyW {D : DesignCert} {ts : Array SlotTy}
    (hts : inferTysW D = some ts) (hwf : DesignCert.DesignCertWF D)
    {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) :
    nodeTyW (fun d => ts[d]?) c = ts[D.sources.size + j]? := by
  have hlist : D.nodes.toList[j]? = some c := by rw [← hc]; simp
  have hb : ∀ d ∈ c.deps.toList, d < (D.sources.map srcTy).size + j := by
    intro d hd
    simpa using hwf.depsBounded j c hc d hd
  simpa using infer_foldlW_node D.nodes.toList _ ts hts j c hlist hb

theorem admitsWB_size {D : DesignCert} {ts : Array SlotTy}
    (h : admitsWB D = true) (hts : inferTysW D = some ts) : ts.size = D.numSlots := by
  unfold admitsWB at h
  rw [hts] at h
  simp only [tysOK, Bool.and_eq_true, beq_iff_eq] at h
  exact h.2.1

/-! ### The static premises, from one Boolean check

`DesignSemWF` and `DesignCertWF` come from `Direct.acceptsDesign`, which the
extended checker demands exactly as the original does, so those two are the
original proofs applied to the extended hypothesis. -/

theorem admitsW_semwf {D : DesignCert} (h : admitsWB D = true) : Direct.DesignSemWF D := by
  have hacc := admitsWB_acceptsDesign h
  cases hd : Direct.designErrors D with
  | none =>
      exact Direct.checkDesign_sound (u := ()) (by unfold Direct.checkDesign; rw [hd])
  | some e =>
      exfalso
      unfold Direct.acceptsDesign at hacc
      rw [hd] at hacc
      simp at hacc

theorem admitsW_wf {D : DesignCert} (h : admitsWB D = true) : DesignCert.DesignCertWF D :=
  designCertWF_of_semwf (admitsW_semwf h)

theorem admitsW_geom_src {D : DesignCert} (h : admitsWB D = true) :
    ∀ k, k < D.sources.size → geomAtW D k = srcGeomOf D k := by
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  intro k hk
  unfold geomAtW srcGeomOf
  rw [tyAtW_source hk hts, Array.getElem?_eq_getElem hk]
  cases D.sources[k]'hk <;> rfl

theorem admitsW_nextImgGeom {D : DesignCert} (h : admitsWB D = true) :
    NextImgGeomOK D (geomAtW D) := by
  intro idx md hmd
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  have hmem : md ∈ D.memories.toList := by
    obtain ⟨h1, h2⟩ := List.getElem?_eq_some_iff.mp
      (show D.memories.toList[idx]? = some md by rw [← hmd]; simp)
    exact h2 ▸ List.getElem_mem h1
  unfold admitsWB at h
  rw [hts] at h
  simp only [tysOK, Bool.and_eq_true] at h
  have hthis := List.all_eq_true.mp h.2.2 md hmem
  unfold geomAtW tyAtW
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

/-- **Every node of an extended-admitted design is admitted by the EXTENDED
predicate.**  Note what this does NOT say: it does not say `NodeAdmitted`, and
the original theorem continues to say that for the original checker. -/
theorem admitsW_nodeAdmitted {D : DesignCert} (h : admitsWB D = true) :
    ∀ c ∈ D.nodes.toList, NodeAdmittedW c := by
  intro c hc
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  obtain ⟨j, hj⟩ := array_mem_getElem? hc
  have hjlt : j < D.nodes.size := Direct.lt_of_getElem?_some hj
  have hlt : D.sources.size + j < ts.size := by
    rw [admitsWB_size h hts]; unfold DesignCert.numSlots; omega
  have hty := admits_node_tyW hts (admitsW_wf h) hj
  rw [Array.getElem?_eq_getElem hlt] at hty
  exact nodeTyW_admits hty

theorem admitsW_arity {D : DesignCert} (h : admitsWB D = true) :
    ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList := by
  intro c hc
  obtain ⟨j, hj⟩ := array_mem_getElem? hc
  have hsh := opArity_of_semwf (admitsW_semwf h) hj
  refine arityOK_of_opArity ?_
  simpa using hsh

/-- Both writes propagate their image's geometry to their output slot.  The
byte-enabled half is the original obligation, unchanged. -/
theorem admitsW_writePropagates {D : DesignCert} (h : admitsWB D = true) :
    ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
      WritePropagatesW D (geomAtW D) j c := by
  intro j c hc
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  have hsem := admitsW_semwf h
  have hjlt : j < D.nodes.size := Direct.lt_of_getElem?_some hc
  have hlt : D.sources.size + j < ts.size := by
    rw [admitsWB_size h hts]; unfold DesignCert.numSlots; omega
  have hty := admits_node_tyW hts (admitsW_wf h) hc
  rw [Array.getElem?_eq_getElem hlt] at hty
  constructor
  · -- the ORIGINAL byte-enabled obligation
    intro bw hop d0 hd0
    obtain ⟨e0, e1, e2, e3, hdeps⟩ := memWriteBE_deps hsem hc hop
    have hne : c.op ≠ .Op_MemWrite := by rw [hop]; simp
    rw [nodeTyW_eq_of_ne hne] at hty
    obtain ⟨aw, dw, wa, hres, hop0, -, -⟩ := nodeTy_write_geom hop hdeps hty
    have he0 : e0 = d0 := by
      have : c.deps[0]? = some e0 := by rw [deps_get c hdeps 0]; rfl
      rw [this] at hd0
      exact (Option.some.inj hd0)
    subst he0
    unfold geomAtW tyAtW
    rw [hts]
    simp only [Option.bind_some]
    rw [hop0, Array.getElem?_eq_getElem hlt, hres]
  · -- the PLAIN write's own obligation
    intro hop d0 hd0
    obtain ⟨e0, e1, e2, e3, hdeps⟩ := memWrite_deps hsem hc hop
    obtain ⟨aw, dw, wa, hres, hop0, -, -, -, -⟩ := nodeTyW_write_geom hop hdeps hty
    have he0 : e0 = d0 := by
      have : c.deps[0]? = some e0 := by rw [deps_get c hdeps 0]; rfl
      rw [this] at hd0
      exact (Option.some.inj hd0)
    subst he0
    unfold geomAtW tyAtW
    rw [hts]
    simp only [Option.bind_some]
    rw [hop0, Array.getElem?_eq_getElem hlt, hres]

/-! ## 3g. The runtime width agreement, the derived write-fit, and the entry

The same two steps the byte-enabled route takes: a `.bv` typing means a
reference value of that width, and from that the ENABLED write's address fits
the table it updates.  Nothing is assigned by hand. -/

theorem admits_slot_widthW {D : DesignCert} (h : admitsWB D = true)
    (i : RuntimeInput) (st : StateM) :
    ∀ (k w : Nat), k < D.numSlots → tyAtW D k = some (.bv w) →
      (slotValM D i st k).asBV.width = w := by
  intro k w hk hty
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  by_cases hs : k < D.sources.size
  · rw [tyAtW_source hs hts] at hty
    rw [slotValM_source hs]
    exact srcTy_bv_width (Option.some.inj hty) i _
  · have hj : k - D.sources.size < D.nodes.size := by
      unfold DesignCert.numSlots at hk; omega
    have hk' : k = D.sources.size + (k - D.sources.size) := by omega
    obtain ⟨c, hc⟩ : ∃ c, D.nodes[k - D.sources.size]? = some c :=
      ⟨D.nodes[k - D.sources.size], by rw [Array.getElem?_eq_getElem hj]⟩
    have hlt : D.sources.size + (k - D.sources.size) < ts.size := by
      rw [admitsWB_size h hts]; unfold DesignCert.numSlots; omega
    have hnode := admits_node_tyW hts (admitsW_wf h) hc
    rw [Array.getElem?_eq_getElem hlt] at hnode
    have htyk : tyAtW D (D.sources.size + (k - D.sources.size)) = some (.bv w) := by
      rw [← hk']; exact hty
    unfold tyAtW at htyk
    rw [hts] at htyk
    simp only [Option.bind_some] at htyk
    rw [Array.getElem?_eq_getElem hlt] at htyk
    have hres : nodeTyW (fun d => ts[d]?) c = some (.bv w) := by
      rw [hnode]; exact htyk
    have hw : w = c.width := nodeTyW_bv_width hres
    -- BOTH writes are excluded by the TYPING, not by an exclusion theorem:
    -- each answers a table, and `.bv` is not a table.
    have hne1 : c.op ≠ .Op_MemWrite := nodeTyW_bv_ne_memWrite hres
    have hne2 : ∀ bw, c.op ≠ .Op_MemWriteBE bw := by
      intro bw hbw
      rw [nodeTyW_eq_of_ne hne1] at hres
      obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWriteBE_deps (admitsW_semwf h) hc hbw
      obtain ⟨aw', dw', wa', hr, -, -, -⟩ := nodeTy_write_geom hbw hdeps hres
      simp at hr
    rw [hk', ← nodeValM_rec (i := i) (st := st) (admitsW_wf h) hj hc, hw]
    exact eval_op_cert_width_nonMem c.op c.width _ hne1 hne2

theorem admits_writeAddrFitsW {D : DesignCert} (h : admitsWB D = true)
    (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ D.nodes.toList, WriteAddrFitsW D i st (geomAtW D) c := by
  intro c hc
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  obtain ⟨j, hj⟩ := array_mem_getElem? hc
  have hsem := admitsW_semwf h
  have hjlt : j < D.nodes.size := Direct.lt_of_getElem?_some hj
  have hlt : D.sources.size + j < ts.size := by
    rw [admitsWB_size h hts]; unfold DesignCert.numSlots; omega
  have hty := admits_node_tyW hts (admitsW_wf h) hj
  rw [Array.getElem?_eq_getElem hlt] at hty
  -- one shared tail: the address slot's width, from the checker's own typing
  have tail : ∀ (e0 e1 : Nat) (aw wa : Nat),
      c.deps.toList.length = 4 → e1 ∈ c.deps.toList →
      ts[D.sources.size + j]? = some (.tbl aw wa) →
      (fun d => ts[d]?) e0 = some (.tbl aw wa) →
      (fun d => ts[d]?) e1 = some (.bv wa) → True := fun _ _ _ _ _ _ _ _ _ => trivial
  constructor
  · -- the ORIGINAL byte-enabled obligation
    intro bw hop d0 d1 d3 hd0 hd1 hd3 _hen
    obtain ⟨e0, e1, e2, e3, hdeps⟩ := memWriteBE_deps hsem hj hop
    have hne : c.op ≠ .Op_MemWrite := by rw [hop]; simp
    rw [nodeTyW_eq_of_ne hne] at hty
    obtain ⟨aw, dw, wa, hres, hop0, hop1, hwle⟩ := nodeTy_write_geom hop hdeps hty
    have he0 : e0 = d0 := by
      have hx : c.deps[0]? = some e0 := by rw [deps_get c hdeps 0]; rfl
      rw [hx] at hd0; exact Option.some.inj hd0
    have he1 : e1 = d1 := by
      have hx : c.deps[1]? = some e1 := by rw [deps_get c hdeps 1]; rfl
      rw [hx] at hd1; exact Option.some.inj hd1
    subst he0; subst he1
    refine ⟨aw, ?_, ?_⟩
    · unfold geomAtW tyAtW
      rw [hts]
      simp only [Option.bind_some]
      rw [hop0]
    · have hmem1 : e1 ∈ c.deps.toList := by rw [hdeps]; simp
      have hd1lt : e1 < D.numSlots := by
        have hb := (admitsW_wf h).depsBounded j c hj e1 hmem1
        unfold DesignCert.numSlots; omega
      have hty1 : tyAtW D e1 = some (.bv wa) := by
        unfold tyAtW
        rw [hts]
        simp only [Option.bind_some]
        exact hop1
      rw [admits_slot_widthW h i st e1 wa hd1lt hty1]
      exact hwle
  · -- the PLAIN write's own obligation, with the SAME address rule
    intro hop d0 d1 d3 hd0 hd1 hd3 _hen
    obtain ⟨e0, e1, e2, e3, hdeps⟩ := memWrite_deps hsem hj hop
    obtain ⟨aw, dw, wa, hres, hop0, hop1, hwle, -, -⟩ := nodeTyW_write_geom hop hdeps hty
    have he0 : e0 = d0 := by
      have hx : c.deps[0]? = some e0 := by rw [deps_get c hdeps 0]; rfl
      rw [hx] at hd0; exact Option.some.inj hd0
    have he1 : e1 = d1 := by
      have hx : c.deps[1]? = some e1 := by rw [deps_get c hdeps 1]; rfl
      rw [hx] at hd1; exact Option.some.inj hd1
    subst he0; subst he1
    refine ⟨aw, ?_, ?_⟩
    · unfold geomAtW tyAtW
      rw [hts]
      simp only [Option.bind_some]
      rw [hop0]
    · have hmem1 : e1 ∈ c.deps.toList := by rw [hdeps]; simp
      have hd1lt : e1 < D.numSlots := by
        have hb := (admitsW_wf h).depsBounded j c hj e1 hmem1
        unfold DesignCert.numSlots; omega
      have hty1 : tyAtW D e1 = some (.bv wa) := by
        unfold tyAtW
        rw [hts]
        simp only [Option.bind_some]
        exact hop1
      rw [admits_slot_widthW h i st e1 wa hd1lt hty1]
      exact hwle

/-- **Admission to the ACTUAL resolved entry, on the extended route.**

The same conditional shape as `admits_mainM_resolved`: the remaining conditions
are ORDINARY RUNTIME ones — the incoming state's table geometry, the source
input/flop bounds, the clock ordinals and the flop count — and they are listed,
not hidden.  `StateShapeOK`/`FullTable` are still LENGTH-ONLY, so this is a
conditional admission-to-main theorem, not full production admission. -/
theorem admitsW_mainM_resolved {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (h : admitsWB D = true)
    (hstate : StateShapeOK D st)
    (hin : ∀ sd ∈ D.sources.toList, SourceInputBound sd i.size)
    (hstb : ∀ sd ∈ D.sources.toList, SourceFlopBound sd st.flops.size)
    (hfclk : ∀ f ∈ D.flops.toList, f.clock < e.size)
    (hmclk : ∀ md ∈ D.memories.toList, md.clock < e.size)
    (hfsize : D.flops.size ≤ st.flops.size) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ StateShapeOK D r.nextState := by
  have hwf   := admitsW_wf h
  have hsem  := admitsW_semwf h
  have hgsrc := admitsW_geom_src h
  have hadm  := admitsW_nodeAdmitted h
  have harit := admitsW_arity h
  have hprop := admitsW_writePropagates h
  have hpol  := admitsW_nextImgGeom h
  have hfits := admits_writeAddrFitsW h i st
  obtain ⟨r, hev, hden, hpres⟩ :=
    mainM_adequate_resolved_gen (G := geomAtW D) hwf hsem hstate hgsrc
      (fun j c hcj hj hmemc ss hpre =>
        node_stepW hwf hsem hcj hj (hadm _ hmemc) (harit _ hmemc)
          (hprop j _ hcj) (hfits _ hmemc) hpre)
      hin hstb hfclk hmclk hfsize
  exact ⟨r, hev, hden, hpres hpol⟩

/-! ## 3h. The FULL runtime policy on the extended route

`admits_run*` are thin over `admits_mainM_resolved` plus the executable policy,
and everything in that policy — `stateOKB`, `memImgOKB`, `flopShapeOKB`, the
codec — is already stated without reference to a checker, so it is REUSED
verbatim.  What needs a new proof is the one place that reads the typing: word
width preservation through a write. -/

/-- `HardwareMemRuntime`'s copy is `private`, so the same one-liner is proved
here rather than reached into. -/
theorem mem_lt_of_getElem?' {α : Type _} {a : Array α} {idx : Nat} {v : α}
    (h : a[idx]? = some v) : idx < a.size := by
  rcases Nat.lt_or_ge idx a.size with hlt | hge
  · exact hlt
  · rw [Array.getElem?_eq_none hge] at h; exact absurd h (by simp)

/-- A TABLE result means the operator is a write — now EITHER write. -/
theorem nodeTyW_tbl_op {ty : Nat → Option SlotTy} {c : DenseNodeCert} {aw dw : Nat}
    (h : nodeTyW ty c = some (.tbl aw dw)) :
    (∃ bw, c.op = .Op_MemWriteBE bw) ∨ c.op = .Op_MemWrite := by
  by_cases hop : c.op = .Op_MemWrite
  · exact Or.inr hop
  · rw [nodeTyW_eq_of_ne hop] at h
    exact Or.inl (nodeTy_tbl_op h)

/-- **Every word of every table slot has the declared data width**, on the
extended route.

The plain write's branch is where `cert_mem_write_width`'s side condition is
finally discharged: the reference write stores its datum RAW, so the image only
stays `dw`-wide because the CHECKER demanded `bvWidthOf td == dw`, and
`admits_slot_widthW` turns that demand into the runtime width.  This is the
obligation `HardwareMemWrite` recorded and left open. -/
theorem admits_slot_memwidthW {D : DesignCert} (h : admitsWB D = true)
    {st : StateM} (hW : StateWordOK D st) (i : RuntimeInput) :
    ∀ (k : Nat), k < D.numSlots → ∀ (t : SlotTy) (dw : Nat),
      tyAtW D k = some t → tyDW t = some dw →
      ∀ x : Int, ((slotValM D i st k).asMem x).width = dw := by
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  intro k
  induction k using Nat.strongRecOn with
  | _ k ih =>
    intro hk t dw hty hdw x
    by_cases hs : k < D.sources.size
    · -- a SOURCE: identical to the original route, since sources do not differ
      rw [tyAtW_source hs hts] at hty
      rw [slotValM_source hs]
      have hsd : D.sources[k]? = some (D.sources[k]'hs) := by
        rw [Array.getElem?_eq_getElem hs]
      have hty' : srcTy (D.sources[k]'hs) = t := Option.some.inj hty
      cases hsrc : D.sources[k]'hs with
      | input _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | const _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | flopQ _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | flopQAsync _ _ _ _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | memImg idx aw dw' =>
          rw [hsrc] at hty'
          have hdw' : dw' = dw := by
            rw [← hty'] at hdw; simpa [srcTy, tyDW] using hdw
          subst hdw'
          rw [hsrc] at hsd
          obtain ⟨md, hmd, -, hmddw⟩ := memDesc_dw_of_sourceOK (admitsW_semwf h) hsd
          obtain ⟨m, hm, hshape⟩ := hW idx md hmd
          have : (StateM.denote st).mems[idx]? = some m.denote := by
            simp [StateM.denote, Array.getElem?_map, hm]
          simp only [Compiler.sourceValue, this, Option.getD_some, CertVal.asMem]
          rw [← hmddw]; exact memShape_denote_width hshape x
      | memConst aw dw' ct =>
          rw [hsrc] at hty'
          have hdw' : dw' = dw := by
            rw [← hty'] at hdw; simpa [srcTy, tyDW] using hdw
          subst hdw'
          simp only [Compiler.sourceValue, CertVal.asMem]
          split <;> simp [mk_bv]
    · -- a NODE
      have hj : k - D.sources.size < D.nodes.size := by
        unfold DesignCert.numSlots at hk; omega
      have hk' : k = D.sources.size + (k - D.sources.size) := by omega
      obtain ⟨c, hc⟩ : ∃ c, D.nodes[k - D.sources.size]? = some c :=
        ⟨D.nodes[k - D.sources.size], by rw [Array.getElem?_eq_getElem hj]⟩
      have hlt : D.sources.size + (k - D.sources.size) < ts.size := by
        rw [admitsWB_size h hts]; unfold DesignCert.numSlots; omega
      have hnode := admits_node_tyW hts (admitsW_wf h) hc
      rw [Array.getElem?_eq_getElem hlt] at hnode
      have htyk : tyAtW D (D.sources.size + (k - D.sources.size)) = some t := by
        rw [← hk']; exact hty
      unfold tyAtW at htyk
      rw [hts] at htyk
      simp only [Option.bind_some] at htyk
      rw [Array.getElem?_eq_getElem hlt] at htyk
      have hres : nodeTyW (fun d => ts[d]?) c = some t := by rw [hnode]; exact htyk
      obtain ⟨aw0, dw0, hteq⟩ : ∃ aw0 dw0, t = .tbl aw0 dw0 := by
        rcases nodeTyW_result hres with hbv | hx
        · exfalso; rw [hbv] at hdw; simp [tyDW] at hdw
        · exact hx
      subst hteq
      have hdw0 : dw0 = dw := by simpa [tyDW] using hdw
      rcases nodeTyW_tbl_op hres with ⟨bw, hop⟩ | hop
      · -- the BYTE-ENABLED write: the original argument, unchanged
        have hne : c.op ≠ .Op_MemWrite := by rw [hop]; simp
        rw [nodeTyW_eq_of_ne hne] at hres
        obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWriteBE_deps (admitsW_semwf h) hc hop
        obtain ⟨aw1, dw1, htbl, hd0ty, hwid⟩ := nodeTy_write_dw hop hdeps hres
        have hx : aw0 = aw1 ∧ dw0 = dw1 := by simpa [SlotTy.tbl.injEq] using htbl
        have hwid' : c.width = dw := by rw [hwid, ← hx.2]; exact hdw0
        have hd0lt : d0 < D.sources.size + (k - D.sources.size) :=
          (admitsW_wf h).depsBounded _ _ hc d0 (by rw [hdeps]; simp)
        have hd0k : d0 < k := by omega
        have hd0num : d0 < D.numSlots := by unfold DesignCert.numSlots; omega
        have hd0tyAt : tyAtW D d0 = some (.tbl aw1 dw1) := by
          unfold tyAtW; rw [hts]; simpa using hd0ty
        have hIH := ih d0 hd0k hd0num (.tbl aw1 dw1) dw1 hd0tyAt (by simp [tyDW])
        have hIH' : ∀ y : Int, ((slotValM D i st d0).asMem y).width = dw := by
          intro y; rw [← hdw0, hx.2]; exact hIH y
        rw [hk', ← nodeValM_rec (i := i) (st := st) (admitsW_wf h) hj hc, hop]
        simp only [hdeps, List.map_cons, List.map_nil, eval_op_cert, CertVal.asMem]
        exact cert_mem_write_be_width hwid' hIH' x
      · -- the PLAIN write
        obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWrite_deps (admitsW_semwf h) hc hop
        obtain ⟨aw1, dw1, wa, htbl, hd0ty, hd1ty, hwle, hd2ty, hwid⟩ :=
          nodeTyW_write_geom hop hdeps hres
        have hx : aw0 = aw1 ∧ dw0 = dw1 := by simpa [SlotTy.tbl.injEq] using htbl
        -- the IMAGE operand, by the induction hypothesis
        have hd0lt : d0 < D.sources.size + (k - D.sources.size) :=
          (admitsW_wf h).depsBounded _ _ hc d0 (by rw [hdeps]; simp)
        have hd0k : d0 < k := by omega
        have hd0num : d0 < D.numSlots := by unfold DesignCert.numSlots; omega
        have hd0tyAt : tyAtW D d0 = some (.tbl aw1 dw1) := by
          unfold tyAtW; rw [hts]; simpa using hd0ty
        have hIH := ih d0 hd0k hd0num (.tbl aw1 dw1) dw1 hd0tyAt (by simp [tyDW])
        have hIH' : ∀ y : Int, ((slotValM D i st d0).asMem y).width = dw := by
          intro y; rw [← hdw0, hx.2]; exact hIH y
        -- the DATUM, from the checker's EQUALITY demand on its width
        have hd2lt : d2 < D.sources.size + (k - D.sources.size) :=
          (admitsW_wf h).depsBounded _ _ hc d2 (by rw [hdeps]; simp)
        have hd2num : d2 < D.numSlots := by unfold DesignCert.numSlots; omega
        have hd2tyAt : tyAtW D d2 = some (.bv dw1) := by
          unfold tyAtW; rw [hts]; simpa using hd2ty
        have hd2w : (slotValM D i st d2).asBV.width = dw := by
          rw [← hdw0, hx.2]; exact admits_slot_widthW h i st d2 dw1 hd2num hd2tyAt
        rw [hk', ← nodeValM_rec (i := i) (st := st) (admitsW_wf h) hj hc, hop]
        simp only [hdeps, List.map_cons, List.map_nil, eval_op_cert, CertVal.asMem]
        exact cert_mem_write_width hIH' hd2w x

theorem admitsW_nextImgTy {D : DesignCert} (h : admitsWB D = true)
    {idx : Nat} {md : MemoryDesc} (hmd : D.memories[idx]? = some md) :
    tyAtW D md.nextImg = some (.tbl md.aw md.dw) ∧ md.nextImg < D.numSlots := by
  obtain ⟨ts, hts⟩ := admitsWB_infer h
  have hmem : md ∈ D.memories.toList := by
    obtain ⟨h1, h2⟩ := List.getElem?_eq_some_iff.mp
      (show D.memories.toList[idx]? = some md by rw [← hmd]; simp)
    exact h2 ▸ List.getElem_mem h1
  have hsz := admitsWB_size h hts
  unfold admitsWB at h
  rw [hts] at h
  simp only [tysOK, Bool.and_eq_true] at h
  have hthis := List.all_eq_true.mp h.2.2 md hmem
  cases hty : ts[md.nextImg]? with
  | none   => rw [hty] at hthis; simp at hthis
  | some t =>
      cases t with
      | bv w     => rw [hty] at hthis; simp at hthis
      | rom dw n => rw [hty] at hthis; simp at hthis
      | tbl aw dw =>
          rw [hty] at hthis
          simp only [Bool.and_eq_true, beq_iff_eq] at hthis
          constructor
          · unfold tyAtW; rw [hts]
            simp only [Option.bind_some, hty, hthis.1, hthis.2]
          · rw [← hsz]; exact mem_lt_of_getElem?' hty

/-- One step under the length-level policy. -/
theorem admits_runW {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsWB D = true) (hR : runtimeOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ StateShapeOK D r.nextState := by
  unfold runtimeOKB at hR
  simp only [Bool.and_eq_true] at hR
  obtain ⟨⟨⟨hst, hin⟩, hfb⟩, hed⟩ := hR
  exact admitsW_mainM_resolved hA (stateOKB_shape hst) (inputOKB_sound hin)
    (flopBoundOKB_sound hfb) (edgesOKB_flops hed) (edgesOKB_mems hed)
    (stateOKB_flopSize hst)

/-- **The word/default policy is preserved**, through an ordinary write too. -/
theorem admits_runW_pres {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (hA : admitsWB D = true) (hR : runtimeOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateOKB D r.nextState = true := by
  have hst : stateOKB D st = true := by
    unfold runtimeOKB at hR
    simp only [Bool.and_eq_true] at hR
    exact hR.1.1.1
  obtain ⟨r, hev, hden, hsh⟩ := admits_runW hA hR
  refine ⟨r, hev, hden, ?_⟩
  have hfl : r.nextState.flops.size = D.flops.size := by
    have hx := congrArg (fun z => z.nextState.flops.size) hden
    simpa [StateM.ResultM.denote, StateM.denote, Compiler.interpretDesign] using hx
  refine stateOKB_complete (by omega) ?_
  intro idx md hmd
  obtain ⟨m, hm, hfull⟩ := hsh idx md hmd
  refine ⟨m, hm, memShape_of_denote hfull ?_⟩
  intro x
  have hidx : idx < D.memories.size := mem_lt_of_getElem?' hmd
  have hlhs : (StateM.denote r.nextState).mems[idx]? = some m.denote := by
    simp [StateM.denote, Array.getElem?_map, hm]
  have hrhs : (Compiler.interpretDesign D e i (StateM.denote st)).nextState.mems[idx]?
      = some (Compiler.srcMemNext (slotValM D i st) e (StateM.denote st) idx md) := by
    have hmd' : D.memories[idx] = md := by
      rw [Array.getElem?_eq_getElem hidx] at hmd; exact Option.some.inj hmd
    unfold Compiler.interpretDesign
    simp [hidx, hmd']
    all_goals (first | rfl | (funext k; rfl))
  have hx := congrArg (fun z => z.nextState.mems[idx]?) hden
  simp only [StateM.ResultM.denote] at hx
  rw [hlhs, hrhs] at hx
  have hmden : m.denote
      = Compiler.srcMemNext (slotValM D i st) e (StateM.denote st) idx md :=
    Option.some.inj hx
  rw [hmden]
  unfold Compiler.srcMemNext
  by_cases hf : fires e md.clock
  · simp only [hf, if_true]
    obtain ⟨hty, hnum⟩ := admitsW_nextImgTy hA hmd
    exact admits_slot_memwidthW hA (stateOKB_word hst) i md.nextImg hnum
      (.tbl md.aw md.dw) md.dw hty (by simp [tyDW]) x
  · -- the QUIET memory: its image comes from the incoming state, untouched
    simp only [hf, Bool.false_eq_true, if_false]
    obtain ⟨m0, hm0, hsh0⟩ := stateOKB_word hst idx md hmd
    have hq : (StateM.denote st).mems[idx]? = some m0.denote := by
      simp [StateM.denote, Array.getElem?_map, hm0]
    rw [hq, Option.getD_some]
    exact memShape_denote_width hsh0 x

/-- **The strengthened policy**, including declared flop widths. -/
theorem admits_runW_full {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (hA : admitsWB D = true)
    (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true := by
  unfold runtimeFullOKB at hR
  simp only [Bool.and_eq_true] at hR
  obtain ⟨hR0, hflop⟩ := hR
  obtain ⟨r, hev, hden, hok⟩ := admits_runW_pres hA hR0
  refine ⟨r, hev, hden, ?_⟩
  unfold stateFullOKB
  simp only [Bool.and_eq_true]
  refine ⟨hok, flopShapeOKB_complete ?_⟩
  intro idx f hf
  have hlt : idx < D.flops.size := mem_lt_of_getElem?' hf
  have hf' : D.flops[idx] = f := by
    rw [Array.getElem?_eq_getElem hlt] at hf; exact Option.some.inj hf
  have hx := congrArg (fun z => z.nextState.flops) hden
  simp only [StateM.ResultM.denote] at hx
  have hfl : r.nextState.flops
      = D.flops.mapIdx (fun idx f =>
          Compiler.srcFlopNext (slotValM D i st) e (StateM.denote st) idx f) := by
    have : (StateM.denote r.nextState).flops = r.nextState.flops := rfl
    rw [← this, hx]
    unfold Compiler.interpretDesign
    simp
    all_goals (first | rfl | (funext k; rfl))
  refine ⟨Compiler.srcFlopNext (slotValM D i st) e (StateM.denote st) idx f, ?_, ?_⟩
  · rw [hfl, Array.getElem?_eq_getElem (by simpa using hlt), Array.getElem_mapIdx, hf']
  · refine srcFlopNext_width ?_
    intro b hb
    obtain ⟨b', hb', hw⟩ := flopShapeOKB_sound hflop idx f hf
    have : b = b' := by
      have hd : (StateM.denote st).flops = st.flops := rfl
      rw [hd, hb'] at hb; exact (Option.some.inj hb).symm
    rw [this]; exact hw

/-- **Two steps, the second on the state the first RETURNED**, decoded through
the codec and still passing the strengthened policy. -/
theorem admits_runW_full_two {D : DesignCert} {i₁ i₂ : RuntimeInput} {st : StateM}
    {e₁ e₂ : ClockEdges} (hA : admitsWB D = true)
    (hR : runtimeFullOKB D i₁ st e₁ = true) :
    ∃ r₁ : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e₁, encInput i₁, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r₁)
      ∧ r₁.denote = Compiler.interpretDesign D e₁ i₁ (StateM.denote st)
      ∧ (inputOKB D i₂.size = true → edgesOKB D e₂ = true →
          -- the returned state DECODES, and the decoded one is what step two runs on
          ∃ st₁, StateM.decStateM (StateM.encStateM r₁.nextState) = some st₁
            ∧ StateM.denote st₁ = StateM.denote r₁.nextState
            ∧ ∃ r₂ : StateM.ResultM,
                Eval hwMemP []
                  (.call hwMemP.entry
                    ([encDesign D, encEdges e₂, encInput i₂,
                      StateM.encStateM st₁].map Term.lit))
                  (StateM.encResultM r₂)
                ∧ r₂.denote
                    = Compiler.interpretDesign D e₂ i₂ (StateM.denote st₁)
                ∧ stateFullOKB D r₂.nextState = true) := by
  obtain ⟨r₁, hev, hden, hok⟩ := admits_runW_full hA hR
  refine ⟨r₁, hev, hden, ?_⟩
  intro hin hed
  have hok' : stateOKB D r₁.nextState = true ∧ flopShapeOKB D r₁.nextState = true := by
    unfold stateFullOKB at hok; simpa only [Bool.and_eq_true] using hok
  obtain ⟨st₁, hdec, hok₁, hden₁⟩ := stateOKB_codec hok'.1
  refine ⟨st₁, hdec, hden₁, ?_⟩
  have hflop₁ : flopShapeOKB D st₁ = true := by
    have : st₁ = r₁.nextState := by
      have hx := StateM.decStateM_encStateM r₁.nextState
      rw [hx] at hdec; exact (Option.some.inj hdec).symm
    rw [this]; exact hok'.2
  refine admits_runW_full hA ?_
  unfold runtimeFullOKB runtimeOKB
  simp only [Bool.and_eq_true]
  refine ⟨⟨⟨⟨hok₁, hin⟩, ?_⟩, hed⟩, hflop₁⟩
  exact flopBoundOKB_of_flopSize (admitsW_semwf hA) (stateOKB_flopSize hok₁)

/-! ## 3i. The CHECKED SPECIALIZATION, on the extended route

`projectMemDesignAt` is REUSED UNCHANGED: it is `mixDriver sf wf hwMemAP
[encDesign D]`, i.e. the design STATIC and exactly the edge vector, the input
vector and the state DYNAMIC, and it never consulted a checker.  What was
keyed on `admitsB` is the BUNDLE — `MemSimWFAt`, `mkMemSimAt`, `mkMemSimOf` —
and the correctness theorems that consume it.  Those get extended forms here;
the production residual checker `checkResidual` and the arity-3 residual it
checks are untouched, and execution is still at the bound it returns. -/

/-- The memory entry's answer is unique, on the extended route. -/
theorem memEntry_uniqueW {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (hA : admitsWB D = true)
    (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval hwMemP []
            (.call hwMemP.entry
              ([encDesign D, encEdges e, encInput i,
                StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hev, hden, hok⟩ := admits_runW_full hA hR
  exact ⟨r, hden, hok, fun v => ⟨fun hv => Eval_det hv hev, fun hv => hv ▸ hev⟩⟩

/-- **The first Futamura projection on the memory entry, for a design with
ordinary writes.**  Same specialization function, same division. -/
theorem projectMemDesignAtW_correct {sf wf : Nat} {D : DesignCert} {R : Program}
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hproj : projectMemDesignAt sf wf D = .ok R)
    (hA : admitsWB D = true) (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval R [] (.call R.entry
            ([encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hden, hok, hiff⟩ := memEntry_uniqueW hA hR
  exact ⟨r, hden, hok,
    fun v => specializeFour_correct_of hwMemAP_entry hwMemAP_erases hiff hproj v⟩

/-- The honest bundle, on the extended route.  The ONLY field that differs from
`MemSimWFAt` is which checker admitted the design; the residual and the bound
are the same artefacts, checked by the same `checkResidual`. -/
structure MemSimWFAtW (sf wf : Nat) (sim : MemSimulator) : Prop where
  proj  : projectMemDesignAt sf wf sim.design = .ok sim.prog
  chk   : checkResidual sim.prog = some sim.bound
  admit : admitsWB sim.design = true

/-- An originally-honest bundle is honest on the extended route too — the
extension never un-admits anything. -/
theorem MemSimWFAt_toW {sf wf : Nat} {sim : MemSimulator} (h : MemSimWFAt sf wf sim)
    (hne : ∀ c ∈ sim.design.nodes.toList, c.op ≠ .Op_MemWrite) :
    MemSimWFAtW sf wf sim :=
  ⟨h.proj, h.chk, by rw [admitsWB_eq hne]; exact h.admit⟩

def mkMemSimAtW (sf wf : Nat) (D : DesignCert) : Option MemSimulator :=
  if admitsWB D then
    match projectMemDesignAt sf wf D with
    | .error _ => none
    | .ok R    =>
        match checkResidual R with
        | none   => none
        | some b => some ⟨D, R, b⟩
  else none

/-- Bind an ALREADY-PRODUCED residual, so a driver's "check" metric is a check
and not a second specialization. -/
def mkMemSimOfW (D : DesignCert) (R : Program) : Option MemSimulator :=
  if admitsWB D then
    match checkResidual R with
    | none   => none
    | some b => some ⟨D, R, b⟩
  else none

theorem mkMemSimAtW_design {sf wf : Nat} {D : DesignCert} {sim : MemSimulator}
    (h : mkMemSimAtW sf wf D = some sim) : sim.design = D := by
  unfold mkMemSimAtW at h
  split at h
  · split at h <;> try simp at h
    split at h <;> try simp at h
    cases h; rfl
  · simp at h

theorem mkMemSimAtW_wf {sf wf : Nat} {D : DesignCert} {sim : MemSimulator}
    (h : mkMemSimAtW sf wf D = some sim) : MemSimWFAtW sf wf sim := by
  unfold mkMemSimAtW at h
  split at h
  · rename_i hadm
    split at h <;> try simp at h
    rename_i R hR
    split at h <;> try simp at h
    rename_i b hb
    cases h
    exact ⟨hR, hb, hadm⟩
  · simp at h

theorem mkMemSimOfW_parts {D : DesignCert} {R : Program} {sim : MemSimulator}
    (h : mkMemSimOfW D R = some sim) :
    sim.design = D ∧ sim.prog = R
      ∧ checkResidual R = some sim.bound ∧ admitsWB D = true := by
  unfold mkMemSimOfW at h
  split at h
  · rename_i hadm
    split at h <;> try simp at h
    rename_i b hb
    cases h
    exact ⟨rfl, rfl, hb, hadm⟩
  · simp at h

theorem mkMemSimOfW_wf {sf wf : Nat} {D : DesignCert} {R : Program}
    {sim : MemSimulator} (hproj : projectMemDesignAt sf wf D = .ok R)
    (h : mkMemSimOfW D R = some sim) : MemSimWFAtW sf wf sim := by
  obtain ⟨hd, hp, hchk, hadm⟩ := mkMemSimOfW_parts h
  exact ⟨by rw [hd, hp]; exact hproj, by rw [hp]; exact hchk, by rw [hd]; exact hadm⟩

/-- **At the returned bound.**  For an honest extended bundle on a state, input
and edge vector the runtime policy accepts, the residual's run TERMINATES
within `sim.bound` — `.outOfFuel` is unreachable — decodes through the real
result codec, agrees with the shared reference semantics, and leaves a state
that passes the same policy, so the next cycle needs no new state check.

`runMemSim` itself is REUSED: the execution function does not change, only the
proof that this design's residual is one it may be applied to. -/
theorem runMemSimW_correct {sf wf : Nat} {sim : MemSimulator}
    (hw : MemSimWFAtW sf wf sim)
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hR : runtimeFullOKB sim.design i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSim sim e i st = some r
      ∧ r.denote = Compiler.interpretDesign sim.design e i (StateM.denote st)
      ∧ stateFullOKB sim.design r.nextState = true := by
  obtain ⟨r, hden, hok, hiff⟩ := projectMemDesignAtW_correct hw.proj hw.admit hR
  have hev : Eval sim.prog [] (.call sim.prog.entry
      [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)])
      (StateM.encResultM r) := (hiff _).mpr rfl
  have hfuel := checkResidual_complete hw.chk hev
  refine ⟨r, ?_, hden, hok⟩
  unfold runMemSim
  rw [hfuel]
  exact StateM.decResultM_encResultM r

/-! ## 4. Controls

The distinctions the plain write must keep, as executable checks on the
extended checker itself. -/

/-- A LOCAL NODE-TYPING fixture only.

Its `tbl` lookup is supplied BY HAND in the `#guard`s below, so these exercise
`nodeTyW` at one node and **say nothing about whole-design admission**.  Its
source 0 is a `memConst`, i.e. a ROM, which types `.rom` and NOT `.tbl` — so
the whole design built from it is REJECTED, and `plainRomWriteDesign` below
records that rejection rather than leaving it implicit.  `plainRamDesign` is
the real mutable-memory positive fixture.

`byte_w` does not appear anywhere: there are no lanes. -/
def plainWriteDesign : DesignCert :=
  { sources  := #[ .memConst 1 8 #[7, 9]      -- 0 : the table, aw 1, dw 8
                 , .input 0 1                  -- 1 : address
                 , .input 1 8                  -- 2 : datum, EXACTLY dw
                 , .input 2 4 ]                -- 3 : enable, FOUR bits wide
    nodes    := #[ { op := .Op_MemWrite, width := 8, deps := #[0,1,2,3], origin := 0 } ]
    outputs  := #[]
    flops    := #[]
    memories := #[]
    clocks   := #[] }

/- The ORIGINAL checker still refuses it -- the contract is unchanged. -/
#guard nodeAdmittedB plainWriteDesign.nodes[0]! = false
#guard nodeTy (fun _ => some (.tbl 1 8)) plainWriteDesign.nodes[0]! = none

/- The EXTENDED typing accepts it, and types it as the table it writes. -/
#guard nodeTyW (fun d => if d == 0 then some (.tbl 1 8)
                         else if d == 1 then some (.bv 1)
                         else if d == 2 then some (.bv 8)
                         else some (.bv 4)) plainWriteDesign.nodes[0]!
       = some (.tbl 1 8)

/- A FOUR-BIT enable is accepted: `bv_nonzero` reads the whole vector, so no
   one-bit enable is demanded and no lane geometry is imposed. -/
#guard nodeTyW (fun d => if d == 0 then some (.tbl 1 8)
                         else if d == 1 then some (.bv 1)
                         else if d == 2 then some (.bv 8)
                         else some (.bv 32)) plainWriteDesign.nodes[0]!
       = some (.tbl 1 8)

/- A datum NARROWER than `dw` is REFUSED: `cert_mem_write` stores it raw, so an
   image whose words must all be `dw` wide would stop being one. -/
#guard nodeTyW (fun d => if d == 0 then some (.tbl 1 8)
                         else if d == 1 then some (.bv 1)
                         else if d == 2 then some (.bv 4)
                         else some (.bv 4)) plainWriteDesign.nodes[0]!
       = none

/- A datum WIDER than `dw` is refused for the same reason -- the check is
   equality, not `≤`. -/
#guard nodeTyW (fun d => if d == 0 then some (.tbl 1 8)
                         else if d == 1 then some (.bv 1)
                         else if d == 2 then some (.bv 16)
                         else some (.bv 4)) plainWriteDesign.nodes[0]!
       = none

/- An address wider than the table's is refused. -/
#guard nodeTyW (fun d => if d == 0 then some (.tbl 1 8)
                         else if d == 1 then some (.bv 5)
                         else if d == 2 then some (.bv 8)
                         else some (.bv 4)) plainWriteDesign.nodes[0]!
       = none

/- A MEMORY enable is refused: the enable must be a bit vector. -/
#guard nodeTyW (fun d => if d == 0 then some (.tbl 1 8)
                         else if d == 1 then some (.bv 1)
                         else if d == 2 then some (.bv 8)
                         else some (.tbl 1 8)) plainWriteDesign.nodes[0]!
       = none

/- The extension is EXACTLY the plain write: the five refused scalar operators
   are still refused by the extended typing too. -/
#guard nodeTyW (fun _ => some (.bv 8))
         { op := .Op_Sub, width := 8, deps := #[1,2], origin := 0 } = none
#guard nodeTyW (fun _ => some (.bv 8))
         { op := .Op_UDiv, width := 8, deps := #[1,2], origin := 0 } = none

/- …and an admitted scalar operator types identically on both routes. -/
#guard nodeTyW (fun _ => some (.bv 8))
         { op := .Op_SRA, width := 8, deps := #[1,2], origin := 0 }
       = nodeTy (fun _ => some (.bv 8))
         { op := .Op_SRA, width := 8, deps := #[1,2], origin := 0 }

/-! ## 5. A WHOLE DESIGN with an ordinary write, through the real checker

`plainWriteDesign` above exercises the typing at one node.  This is a complete
certificate with a DECLARED memory whose next image IS the plain write, so the
extended checker's memory clause is exercised too, and the answers are decided
in the kernel. -/

def plainRamDesign : DesignCert :=
  { sources  := #[ .memImg 0 2 8      -- 0 : the mutable image, aw 2, dw 8
                 , .input 0 2         -- 1 : address
                 , .input 1 8         -- 2 : datum, EXACTLY dw
                 , .input 2 4 ]       -- 3 : enable, FOUR bits -- no lanes
    nodes    := #[ { op := .Op_MemWrite, width := 8, deps := #[0,1,2,3], origin := 0 } ]
    outputs  := #[]
    flops    := #[]
    memories := #[ { aw := 2, dw := 8, nextImg := 4, clock := 0 } ]
    clocks   := #[ { name := "clk" } ] }

/- The ORIGINAL checker refuses the whole design, and says so for the right
   reason: its node is not an admitted node. -/
#guard admitsB plainRamDesign = false
#guard nodeAdmittedB plainRamDesign.nodes[0]! = false

/- The EXTENDED checker admits it, in the kernel. -/
theorem plainRamDesign_admitsW : admitsWB plainRamDesign = true := by decide +kernel

/- …and the geometry it produces is the declared one. -/
#guard geomAtW plainRamDesign 4 = some 2
#guard tyAtW plainRamDesign 4 = some (.tbl 2 8)

/- A DATUM of the wrong width sinks the whole design, not just the node. -/
def plainRamDesign_badDatum : DesignCert :=
  { plainRamDesign with sources := #[ .memImg 0 2 8, .input 0 2, .input 1 4, .input 2 4 ] }
#guard admitsWB plainRamDesign_badDatum = false

/- An ADDRESS wider than the table's sinks it too. -/
def plainRamDesign_badAddr : DesignCert :=
  { plainRamDesign with sources := #[ .memImg 0 2 8, .input 0 5, .input 1 8, .input 2 4 ] }
#guard admitsWB plainRamDesign_badAddr = false

/- A declared memory whose geometry disagrees with the write's table is refused
   by the memory clause, even though the node itself types. -/
def plainRamDesign_badMem : DesignCert :=
  { plainRamDesign with memories := #[ { aw := 3, dw := 8, nextImg := 4, clock := 0 } ] }
#guard admitsWB plainRamDesign_badMem = false

/- **A whole design that writes to a ROM is REJECTED.**  `plainWriteDesign`'s
   `tbl` lookup is supplied by hand; here the source table is consulted for
   real, a `memConst` types `.rom`, and the extended checker refuses it.  This
   is the rejection `plainWriteDesign` must not be mistaken for admitting. -/
def plainRomWriteDesign : DesignCert :=
  { plainRamDesign with
      sources := #[ .memConst 2 8 #[7, 9, 11, 13], .input 0 2, .input 1 8, .input 2 4 ] }
#guard admitsWB plainRomWriteDesign = false
#guard admitsB plainRomWriteDesign = false
/- and it is refused for the ROM reason: the source types `.rom`, not `.tbl` -/
#guard srcTy (.memConst 2 8 #[7, 9, 11, 13]) = .rom 8 4
#guard nodeTyW (fun d => if d == 0 then some (.rom 8 4)
                         else if d == 1 then some (.bv 2)
                         else if d == 2 then some (.bv 8)
                         else some (.bv 4)) plainRamDesign.nodes[0]!
       = none

/-! ### Disabled and quiet preservation, at the image

`writeEn_disabled_eq` proves a disabled write returns the EXACT image.  These
run it. -/

/- enabled: the word changes, and only that word -/
#guard (MemImg.writeEn (MemImg.ofMemConst 8 #[11, 22, 33, 44])
          (mk_bv 2 1) (mk_bv 8 99) (mk_bv 4 8)).words.map (fun v => v.value)
       = [11, 99, 33, 44]

/- **DISABLED: the WHOLE image survives, not merely its words.**  The previous
   control compared `words` only, which cannot see the default or the widths.
   This compares the ENCODED images, which carry both. -/
private def dImg : MemImg := ⟨[mk_bv 8 11, mk_bv 8 22, mk_bv 8 33, mk_bv 8 44], mk_bv 8 7⟩
#guard MemImg.encMemImg (MemImg.writeEn dImg (mk_bv 2 1) (mk_bv 8 99) (mk_bv 4 0))
       == MemImg.encMemImg dImg
/- a NONZERO default is carried through an ENABLED write too -/
#guard (MemImg.writeEn dImg (mk_bv 2 1) (mk_bv 8 99) (mk_bv 4 1)).dflt.value = 7
#guard (MemImg.writeEn dImg (mk_bv 2 1) (mk_bv 8 99) (mk_bv 4 1)).words.map (fun v => v.value)
       = [11, 99, 33, 44]

/- the WHOLE enable operand is read: value 8 is zero in bit 0 but nonzero as a
   vector, so the write fires.  A one-bit-enable reading would have dropped it. -/
#guard bv_nonzero (mk_bv 4 8) = true
#guard bv_nonzero (mk_bv 4 0) = false

/- **The datum is stored RAW.**  `mk_bv 8 255` is ALREADY normalized, so it
   cannot tell a raw store from a resizing one -- both answer 255.  This datum
   has width 8 and raw value 300, which is OUTSIDE that width: a raw store
   keeps 300, while any `mk_bv`/`bv_resize` on the way in would answer
   `300 % 256 = 44`.  The control requires 300. -/
private def rawDatum : BV := { width := 8, value := 300 }
#guard rawDatum.width = 8
#guard bv_uint rawDatum = 44          -- what a RESIZE would have produced
#guard (MemImg.writeEn (MemImg.ofMemConst 8 #[11, 22, 33, 44])
          (mk_bv 2 3) rawDatum (mk_bv 4 1)).words.map (fun v => v.value)
       = [11, 22, 33, 300]
/- …and the stored word still has the DECLARED width, which is what the runtime
   policy reads -- so a raw value does not break `MemShapeOK`. -/
#guard (MemImg.writeEn (MemImg.ofMemConst 8 #[11, 22, 33, 44])
          (mk_bv 2 3) rawDatum (mk_bv 4 1)).words.map (fun v => v.width)
       = [8, 8, 8, 8]
/- the same raw datum through the PRIMITIVE, not just the image function -/
#guard (match evalPrim .memWrite
              [MemImg.encMemImg (MemImg.ofMemConst 8 #[11, 22, 33, 44]),
               ofBV (mk_bv 2 3), ofBV rawDatum, ofBV (mk_bv 4 1)] with
        | .ok v => v == MemImg.encMemImg (MemImg.writeEn
                     (MemImg.ofMemConst 8 #[11, 22, 33, 44]) (mk_bv 2 3) rawDatum (mk_bv 4 1))
        | _     => false)

/- an ENABLED write out of range is REFUSED by the primitive, not dropped -/
#guard (evalPrim .memWrite
          [MemImg.encMemImg (MemImg.ofMemConst 8 #[11, 22]), ofBV (mk_bv 4 9),
           ofBV (mk_bv 8 1), ofBV (mk_bv 4 1)]).isOk = false

/- …while a DISABLED out-of-range write is admitted and changes nothing, which
   is what `valMemWrite_disabled` says and the quiet branch needs -/
#guard (evalPrim .memWrite
          [MemImg.encMemImg (MemImg.ofMemConst 8 #[11, 22]), ofBV (mk_bv 4 9),
           ofBV (mk_bv 8 1), ofBV (mk_bv 4 0)]).isOk = true

/-! ## 6. TWO STEPS on `plainRamDesign`, decoded, with a complete comparison

The soundness theorem instantiated at the real mutable-memory fixture, with the
runtime policy decided rather than assumed, and the resulting images compared
COMPLETELY — length, every word, every word's width, and the default — against
the reference the write is supposed to produce.

Step 1 fires the clock with a WIDE enable (`mk_bv 4 8`, zero in bit 0).
Step 2 runs on the state step 1 RETURNED, with the clock QUIET, so the image
must come back exactly. -/

/-- A state with a NONZERO default, so default preservation is observable. -/
def pwState : StateM :=
  ⟨#[], #[⟨[mk_bv 8 11, mk_bv 8 22, mk_bv 8 33, mk_bv 8 44], mk_bv 8 7⟩]⟩

/-- address 1, datum 99, enable 8 — wide, and zero in bit 0. -/
def pwIn : RuntimeInput := #[mk_bv 2 1, mk_bv 8 99, mk_bv 4 8]
/-- the same, with the enable ZERO. -/
def pwInOff : RuntimeInput := #[mk_bv 2 1, mk_bv 8 99, mk_bv 4 0]

def pwActive : ClockEdges := #[true]
def pwQuiet  : ClockEdges := #[false]

/- the policy ACCEPTS this state, input and edge vector -/
#guard stateFullOKB plainRamDesign pwState = true
#guard runtimeFullOKB plainRamDesign pwIn pwState pwActive = true
#guard runtimeFullOKB plainRamDesign pwIn pwState pwQuiet = true

/-- **The chain, instantiated.**  Hypotheses discharged by the checker and the
policy, not assumed: a two-step run whose second step is on the state the first
returned, decoded through the codec, both steps agreeing with
`interpretDesign`, and the strengthened policy holding at the end. -/
theorem plainRamDesign_two_steps :
    ∃ r₁ : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign plainRamDesign, encEdges pwActive, encInput pwIn,
            StateM.encStateM pwState].map Term.lit))
        (StateM.encResultM r₁)
      ∧ r₁.denote = Compiler.interpretDesign plainRamDesign pwActive pwIn
                      (StateM.denote pwState)
      ∧ (inputOKB plainRamDesign pwIn.size = true → edgesOKB plainRamDesign pwQuiet = true →
          ∃ st₁, StateM.decStateM (StateM.encStateM r₁.nextState) = some st₁
            ∧ StateM.denote st₁ = StateM.denote r₁.nextState
            ∧ ∃ r₂ : StateM.ResultM,
                Eval hwMemP []
                  (.call hwMemP.entry
                    ([encDesign plainRamDesign, encEdges pwQuiet, encInput pwIn,
                      StateM.encStateM st₁].map Term.lit))
                  (StateM.encResultM r₂)
                ∧ r₂.denote = Compiler.interpretDesign plainRamDesign pwQuiet pwIn
                                (StateM.denote st₁)
                ∧ stateFullOKB plainRamDesign r₂.nextState = true) :=
  admits_runW_full_two plainRamDesign_admitsW (by decide +kernel)

/-! ### The COMPLETE comparison, executed

`interpretDesign` is the reference.  These compare the committed image against
what the ordinary write is supposed to produce, in full: the array length,
every word's VALUE, every word's WIDTH, and the DEFAULT.  Nothing is sampled. -/

/-- The image the reference commits for memory 0 after one step. -/
private def refImg (e : ClockEdges) (i : RuntimeInput) (st : StateM) : MemImg :=
  match (plainRamDesign.memories[0]?) with
  | none    => ⟨[], mk_bv 0 0⟩
  | some md =>
      if fires e md.clock then
        MemImg.writeEn (st.mems[0]!) (i[0]!) (i[1]!) (i[2]!)
      else st.mems[0]!

/- ENABLED step: exactly one word changes, widths and default survive -/
#guard (refImg pwActive pwIn pwState).words.length = 4
#guard (refImg pwActive pwIn pwState).words.map (fun v => v.value) = [11, 99, 33, 44]
#guard (refImg pwActive pwIn pwState).words.map (fun v => v.width) = [8, 8, 8, 8]
#guard (refImg pwActive pwIn pwState).dflt.value = 7
#guard (refImg pwActive pwIn pwState).dflt.width = 8

/- QUIET step on the state the enabled step returned: the WHOLE image, encoded,
   comes back identical — words, widths and default together -/
#guard MemImg.encMemImg (refImg pwQuiet pwIn ⟨#[], #[refImg pwActive pwIn pwState]⟩)
       == MemImg.encMemImg (refImg pwActive pwIn pwState)

/- DISABLED step with the clock FIRING: the enable is zero, so the image is
   returned exactly — a different reason from the quiet clock, and both hold -/
#guard MemImg.encMemImg (refImg pwActive pwInOff pwState) == MemImg.encMemImg pwState.mems[0]!

/- and the policy still accepts the committed image, so the run composes -/
#guard memImgOKB { aw := 2, dw := 8, nextImg := 4, clock := 0 }
         (refImg pwActive pwIn pwState) = true
#guard stateFullOKB plainRamDesign ⟨#[], #[refImg pwActive pwIn pwState]⟩ = true

#audit_axioms plainRamDesign_two_steps

/-! ### Executed: the ordinary write, through the RESIDUAL

`plainRamDesign` specialized, checked by the UNCHANGED production
`checkResidual`, and run at the bound it returned. -/

def plainRamSim : Option MemSimulator := mkMemSimAtW 20000 200 plainRamDesign

#guard plainRamSim.isSome
/- the production residual is the arity-3 one, unchanged -/
#guard (match plainRamSim with
        | some sim => sim.prog.funs.map FunDef.arity == [3]
        | none     => false)
/- it is bound to THIS design -/
#guard (match plainRamSim with
        | some sim => sim.design == plainRamDesign
        | none     => false)

/- ENABLED, through the residual: word 1 becomes 99 and nothing else moves -/
#guard (match plainRamSim with
        | some sim =>
            (match runMemSim sim pwActive pwIn pwState with
             | some r => (r.nextState.mems[0]!).words.map (fun v => v.value) == [11, 99, 33, 44]
             | none   => false)
        | none => false)
/- …the default survives, and so do the word widths -/
#guard (match plainRamSim with
        | some sim =>
            (match runMemSim sim pwActive pwIn pwState with
             | some r => (r.nextState.mems[0]!).dflt.value == 7
                         && (r.nextState.mems[0]!).words.map (fun v => v.width) == [8,8,8,8]
             | none   => false)
        | none => false)
/- DISABLED enable, clock firing: the WHOLE encoded image comes back identical -/
#guard (match plainRamSim with
        | some sim =>
            (match runMemSim sim pwActive pwInOff pwState with
             | some r => MemImg.encMemImg (r.nextState.mems[0]!)
                         == MemImg.encMemImg pwState.mems[0]!
             | none   => false)
        | none => false)
/- QUIET clock: likewise, for the other reason -/
#guard (match plainRamSim with
        | some sim =>
            (match runMemSim sim pwQuiet pwIn pwState with
             | some r => MemImg.encMemImg (r.nextState.mems[0]!)
                         == MemImg.encMemImg pwState.mems[0]!
             | none   => false)
        | none => false)
/- TWO STEPS through the residual, the second on the state the first returned:
   the write lands, then the quiet step preserves it exactly -/
#guard (match plainRamSim with
        | some sim =>
            (match runMemSim sim pwActive pwIn pwState with
             | some r₁ =>
                 (match runMemSim sim pwQuiet pwIn r₁.nextState with
                  | some r₂ => MemImg.encMemImg (r₂.nextState.mems[0]!)
                               == MemImg.encMemImg (r₁.nextState.mems[0]!)
                               && (r₂.nextState.mems[0]!).words.map (fun v => v.value)
                                  == [11, 99, 33, 44]
                  | none => false)
             | none => false)
        | none => false)

/-- **The whole production shape, as a theorem**: specialize, check, run — the
design static and exactly edges/input/state dynamic, at the checked bound. -/
theorem plainRamSim_runs {sim : MemSimulator} (h : plainRamSim = some sim)
    (i : RuntimeInput) (st : StateM) (e : ClockEdges)
    (hR : runtimeFullOKB plainRamDesign i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSim sim e i st = some r
      ∧ r.denote = Compiler.interpretDesign plainRamDesign e i (StateM.denote st)
      ∧ stateFullOKB plainRamDesign r.nextState = true := by
  have hd : sim.design = plainRamDesign := mkMemSimAtW_design h
  have hw : MemSimWFAtW 20000 200 sim := mkMemSimAtW_wf h
  have := runMemSimW_correct hw (i := i) (st := st) (e := e) (by rw [hd]; exact hR)
  rw [hd] at this
  exact this

#audit_axioms plainRamDesign_admitsW
#audit_axioms nodeTy_memWrite_none
#audit_axioms nodeTyW_eq_of_ne
#audit_axioms nodeTyW_admits
#audit_axioms nodeTyW_result
#audit_axioms nodeTyW_bv_ne_memWrite
#audit_axioms nodeTyW_write_geom
#audit_axioms admitsWB_eq
#audit_axioms inferTysW_eq
#audit_axioms admitsB_tysOK
#audit_axioms evalPrim_memWrite
#audit_axioms evalPrim_memWrite_disabled
#audit_axioms applyOpM_memWrite
#audit_axioms applyOpM_memWrite_disabled
#audit_axioms evalNodeM_memWrite
#audit_axioms evalNodeM_memWrite_disabled
#audit_axioms memWrite_deps
#audit_axioms node_step_memWrite
#audit_axioms node_stepW
#audit_axioms admits_node_tyW
#audit_axioms admitsW_nodeAdmitted
#audit_axioms admitsW_writePropagates
#audit_axioms admits_slot_widthW
#audit_axioms admits_writeAddrFitsW
#audit_axioms admitsW_mainM_resolved
#audit_axioms nodeTyW_tbl_op
#audit_axioms admits_slot_memwidthW
#audit_axioms admitsW_nextImgTy
#audit_axioms admits_runW
#audit_axioms admits_runW_pres
#audit_axioms admits_runW_full
#audit_axioms admits_runW_full_two
#audit_axioms memEntry_uniqueW
#audit_axioms projectMemDesignAtW_correct
#audit_axioms mkMemSimAtW_wf
#audit_axioms mkMemSimOfW_wf
#audit_axioms MemSimWFAt_toW
#audit_axioms runMemSimW_correct
#audit_axioms plainRamSim_runs

end HwMem
end Projection
