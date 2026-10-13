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

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

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

/-- The result shape, for the extended typing. -/
theorem nodeTyW_result {ty : Nat → Option SlotTy} {c : DenseNodeCert} {t : SlotTy}
    (h : nodeTyW ty c = some t) : t = .bv c.width ∨ ∃ aw dw, t = .tbl aw dw := by
  by_cases hop : c.op = .Op_MemWrite
  · -- the plain write types a table
    rw [nodeTyW_memWrite hop] at h
    unfold nodeTyWBody at h
    split at h
    · rename_i tm ta td ten _
      split at h
      · split at h
        · split at h
          · rename_i aw' dw' _ _
            exact Or.inr ⟨aw', dw', (Option.some.inj h).symm⟩
          · simp at h
        · simp at h
      · simp at h
    · simp at h
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
    {w d0 d1 d2 d3 : Nat} (hdeps : c.deps.toList = [d0, d1, d2, d3])
    (h : nodeTyW ty c = some (.bv w)) : c.op ≠ .Op_MemWrite := by
  intro hop
  rcases nodeTyW_result h with h' | ⟨aw, dw, h'⟩
  · -- a plain write's body only ever answers `.tbl` or `none`
    rw [nodeTyW_memWrite hop] at h
    unfold nodeTyWBody at h
    rw [hdeps] at h
    simp only [List.map_cons, List.map_nil] at h
    split at h
    · split at h
      · split at h
        · simp at h
        · simp at h
      · simp at h
    · simp at h
  · simp at h'

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

/-! ## 4. Controls

The distinctions the plain write must keep, as executable checks on the
extended checker itself. -/

/-- A one-word table, written by an ordinary `Op_MemWrite` with a WIDE enable.
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

#print axioms nodeTy_memWrite_none
#print axioms nodeTyW_eq_of_ne
#print axioms nodeTyW_admits
#print axioms nodeTyW_result
#print axioms nodeTyW_bv_ne_memWrite
#print axioms nodeTyW_write_geom
#print axioms admitsWB_eq
#print axioms inferTysW_eq
#print axioms admitsB_tysOK
#print axioms evalPrim_memWrite
#print axioms evalPrim_memWrite_disabled
#print axioms applyOpM_memWrite
#print axioms applyOpM_memWrite_disabled
#print axioms evalNodeM_memWrite
#print axioms evalNodeM_memWrite_disabled
#print axioms memWrite_deps
#print axioms node_step_memWrite
#print axioms node_stepW

end HwMem
end Projection
