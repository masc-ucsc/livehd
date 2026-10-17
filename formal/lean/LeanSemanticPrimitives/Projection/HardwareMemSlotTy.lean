/-
# The inferred slot type agrees with the reference slot value

The missing link between the static checker and the runtime condition
`WriteAddrFits`.

## Why this is not circular

`sourcesThenNodes` and `evalNodesM_agree` ASSUME `WriteAddrFits`, so neither can
be used to establish the facts that derive it.  Nothing here touches them.  The
agreement is proved directly from

* `slotValM_source` — below `D.sources.size` the reference slot value IS
  `Compiler.sourceValue` of the descriptor, and
* `nodeValM_rec` — at a node the reference slot value IS `eval_op_cert` of the
  dependencies,

both of which need only `DesignCertWF`, together with §63.40's width facts and
the checker's own inference.

## Why no induction is needed

A node's RESULT width is its declared width whatever its operands are:
`eval_op_cert_width_nonMem` holds for every admitted operator at every argument
list.  So the agreement at a slot does not depend on the agreement at its
dependencies, and the dense-order induction collapses to a case split on
"source or node".  The topological bound is still used — through
`admits_node_ty` — to know WHICH node a slot belongs to.
-/
import LeanSemanticPrimitives.Projection.HardwareMemAdmit
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. `eval_op_cert` returns the node width for every admitted operator -/

theorem eval_op_cert_memRead_width (w : Nat) (vs : List CertVal) :
    (eval_op_cert .Op_MemRead w vs).asBV.width = w := by
  match vs with
  | [_, _, _]                 => exact cert_mem_read_width _ _ _ _
  | []                        => rfl
  | [_]                       => rfl
  | [_, _]                    => rfl
  | _ :: _ :: _ :: _ :: _     => rfl

/-- The two WRITE operators return a `.mem`, so they are excluded; every other
operator returns a `.bv` of the node width, at ANY argument list. -/
theorem eval_op_cert_width_nonMem (op : LGraphOp) (w : Nat) (vs : List CertVal)
    (h1 : op ≠ .Op_MemWrite) (h2 : ∀ bw, op ≠ .Op_MemWriteBE bw) :
    (eval_op_cert op w vs).asBV.width = w := by
  cases op
  case Op_MemRead      => exact eval_op_cert_memRead_width w vs
  case Op_MemWrite     => exact absurd rfl h1
  case Op_MemWriteBE bw => exact absurd rfl (h2 bw)
  all_goals (simp only [eval_op_cert, CertVal.asBV]; exact eval_op_width _ _ _)

/-! ## 2. A scalar source returns its declared width -/

theorem srcTy_bv_width {sd : SourceDesc} {w : Nat} (h : srcTy sd = .bv w)
    (i : RuntimeInput) (s : RuntimeState) :
    (sourceValue i s sd).asBV.width = w := by
  cases sd with
  | input idx w' =>
      have hw : w = w' := by simpa [srcTy] using h.symm
      subst hw; exact sourceValue_width_input i s idx w
  | const w' v =>
      have hw : w = w' := by simpa [srcTy] using h.symm
      subst hw; exact sourceValue_width_const i s w v
  | flopQ idx w' =>
      have hw : w = w' := by simpa [srcTy] using h.symm
      subst hw; exact sourceValue_width_flopQ i s idx w
  | flopQAsync idx w' ri rv al =>
      have hw : w = w' := by simpa [srcTy] using h.symm
      subst hw; exact sourceValue_width_flopQAsync i s idx w ri rv al
  | memImg idx aw dw  => simp [srcTy] at h
  | memConst aw dw ct => simp [srcTy] at h

/-! ## 3. A node that types `.bv` types it at the NODE width -/

theorem nodeTy_result {ty : Nat → Option SlotTy} {c : DenseNodeCert} {t : SlotTy}
    (h : nodeTy ty c = some t) : t = .bv c.width ∨ ∃ aw dw, t = .tbl aw dw := by
  unfold nodeTy at h
  split at h
  · simp at h
  · split at h
    · -- Op_MemRead
      split at h
      · split at h
        · split at h
          · exact Or.inl (Option.some.inj h).symm
          · simp at h
        · exact Or.inl (Option.some.inj h).symm
        · simp at h
      · simp at h
    · -- Op_MemWriteBE
      split at h
      · split at h
        · split at h
          · rename_i aw' dw' _ _
            exact Or.inr ⟨aw', dw', (Option.some.inj h).symm⟩
          · simp at h
        · simp at h
      · simp at h
    · -- everything else
      split at h
      · exact Or.inl (Option.some.inj h).symm
      · simp at h

theorem nodeTy_bv_width {ty : Nat → Option SlotTy} {c : DenseNodeCert} {w : Nat}
    (h : nodeTy ty c = some (.bv w)) : w = c.width := by
  rcases nodeTy_result h with h' | ⟨aw, dw, h'⟩
  · exact (SlotTy.bv.inj h')
  · simp at h'

/-! ## 4. The agreement: inferred `.bv w` means a reference value of width `w` -/

theorem admits_slot_width {D : DesignCert} (h : admitsB D = true)
    (i : RuntimeInput) (st : StateM) :
    ∀ (k w : Nat), k < D.numSlots → tyAt D k = some (.bv w) →
      (slotValM D i st k).asBV.width = w := by
  intro k w hk hty
  obtain ⟨ts, hts⟩ := admitsB_infer h
  by_cases hs : k < D.sources.size
  · -- a SOURCE: the reference value is `sourceValue` of the descriptor
    rw [tyAt_source hs hts] at hty
    rw [slotValM_source hs]
    exact srcTy_bv_width (Option.some.inj hty) i _
  · -- a NODE: the reference value is `eval_op_cert` at the node width
    have hj : k - D.sources.size < D.nodes.size := by
      unfold DesignCert.numSlots at hk; omega
    have hk' : k = D.sources.size + (k - D.sources.size) := by omega
    obtain ⟨c, hc⟩ : ∃ c, D.nodes[k - D.sources.size]? = some c :=
      ⟨D.nodes[k - D.sources.size], by rw [Array.getElem?_eq_getElem hj]⟩
    have hlt : D.sources.size + (k - D.sources.size) < ts.size := by
      rw [admitsB_size h hts]; unfold DesignCert.numSlots; omega
    have hnode := admits_node_ty hts (admits_wf h) hc
    rw [Array.getElem?_eq_getElem hlt] at hnode
    have htyk : tyAt D (D.sources.size + (k - D.sources.size)) = some (.bv w) := by
      rw [← hk']; exact hty
    unfold tyAt at htyk
    rw [hts] at htyk
    simp only [Option.bind_some] at htyk
    rw [Array.getElem?_eq_getElem hlt] at htyk
    have hres : nodeTy (fun d => ts[d]?) c = some (.bv w) := by
      rw [hnode]; exact htyk
    have hw : w = c.width := nodeTy_bv_width hres
    -- the operator is admitted, and a `.bv` result excludes both writes
    have hadm := nodeTy_admits hres
    have hne1 : c.op ≠ .Op_MemWrite := NodeAdmitted_ne_memWrite hadm
    have hne2 : ∀ bw, c.op ≠ .Op_MemWriteBE bw := by
      intro bw hbw
      rcases nodeTy_result hres with h' | ⟨aw, dw, h'⟩
      · -- a write cannot type `.bv`
        obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWriteBE_deps (admits_semwf h) hc hbw
        obtain ⟨aw', dw', wa', hr, -, -, -⟩ := nodeTy_write_geom hbw hdeps hres
        simp at hr
      · simp at h'
    rw [hk', ← nodeValM_rec (i := i) (st := st) (admits_wf h) hj hc, hw]
    exact eval_op_cert_width_nonMem c.op c.width _ hne1 hne2

/-! ## 5. `WriteAddrFits`, DERIVED from the checked inference

No hand-written geometry assignment, no assumed correspondence: the address
width comes from the checker's own `bv` typing and the agreement above, and the
table's address width from the `tbl` typing of operand 0.  The condition is
produced only for an ENABLED write — the enable hypothesis is consumed and
never used, which is exactly the "enabled-only" discipline. -/

theorem admits_writeAddrFits {D : DesignCert} (h : admitsB D = true)
    (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ D.nodes.toList, WriteAddrFits D i st (geomAt D) c := by
  intro c hc bw hop d0 d1 d3 hd0 hd1 hd3 _hen
  obtain ⟨ts, hts⟩ := admitsB_infer h
  obtain ⟨j, hj⟩ := array_mem_getElem? hc
  have hsem := admits_semwf h
  obtain ⟨e0, e1, e2, e3, hdeps⟩ := memWriteBE_deps hsem hj hop
  have hjlt : j < D.nodes.size := Direct.lt_of_getElem?_some hj
  have hlt : D.sources.size + j < ts.size := by
    rw [admitsB_size h hts]; unfold DesignCert.numSlots; omega
  have hty := admits_node_ty hts (admits_wf h) hj
  rw [Array.getElem?_eq_getElem hlt] at hty
  obtain ⟨aw, dw, wa, hres, hop0, hop1, hwle⟩ := nodeTy_write_geom hop hdeps hty
  have he0 : e0 = d0 := by
    have hx : c.deps[0]? = some e0 := by rw [deps_get c hdeps 0]; rfl
    rw [hx] at hd0; exact Option.some.inj hd0
  have he1 : e1 = d1 := by
    have hx : c.deps[1]? = some e1 := by rw [deps_get c hdeps 1]; rfl
    rw [hx] at hd1; exact Option.some.inj hd1
  subst he0; subst he1
  refine ⟨aw, ?_, ?_⟩
  · unfold geomAt tyAt
    rw [hts]
    simp only [Option.bind_some]
    rw [hop0]
  · have hmem1 : e1 ∈ c.deps.toList := by rw [hdeps]; simp
    have hd1lt : e1 < D.numSlots := by
      have hb := (admits_wf h).depsBounded j c hj e1 hmem1
      unfold DesignCert.numSlots; omega
    have hty1 : tyAt D e1 = some (.bv wa) := by
      unfold tyAt
      rw [hts]
      simp only [Option.bind_some]
      exact hop1
    rw [admits_slot_width h i st e1 wa hd1lt hty1]
    exact hwle

/-! ## 6. Admission to the ACTUAL resolved entry

The static bundle and the derived write-fit, composed with
`mainM_adequate_resolved`.  The conditions that remain are ORDINARY RUNTIME
ones — the incoming state's table geometry, the source input/flop bounds, the
clock ordinals and the flop count — and they are listed, not hidden.

`StateShapeOK`/`FullTable` are still LENGTH-ONLY; this is a conditional
admission-to-main theorem, not full production admission. -/

theorem admits_mainM_resolved {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (h : admitsB D = true)
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
  obtain ⟨hwf, hsem, hgsrc, hadm, harity, hprop, hpol⟩ := admits_static_premises h
  obtain ⟨r, hev, hden, hpres⟩ :=
    mainM_adequate_resolved (G := geomAt D) hwf hsem hstate hgsrc hadm harity hprop
      (admits_writeAddrFits h i st) hin hstb hfclk hmclk hfsize
  exact ⟨r, hev, hden, hpres hpol⟩

/-! ## 6. The checked geometry IS the hand-written one

`mixedG` (HardwareMemFold) and `twoMemG` (HardwareMemCommit) were written by
hand, and each carried its OWN `WriteAddrFits` proof by case analysis over the
fixture's nodes.  Those two modules sit below this one, so their proofs cannot
call the generic theorem directly -- but `geomAt` agrees with both assignments
EVERYWHERE, not merely at the guarded slots, so the hand proofs are superfluous:
the statements they prove are re-derived here from `admits_writeAddrFits`
alone, and the instantiated fold is re-derived from `admits_static_premises`
alone.  The hand-written assignments survive below only as the independent
controls they now are.  The full fixture, which sits ABOVE this module, no
longer has a hand write-fit at all. -/

theorem geomAt_out {D : DesignCert} {ts : Array SlotTy}
    (hts : inferTys D = some ts) {k : Nat} (hk : ts.size ≤ k) :
    geomAt D k = none := by
  unfold geomAt tyAt
  rw [hts]
  simp [Array.getElem?_eq_none hk]

theorem geomAt_mixed : geomAt mixedDesign = mixedG := by
  obtain ⟨ts, hts⟩ := admitsB_infer mixedDesign_admits
  have hsz : ts.size = 8 := by
    rw [admitsB_size mixedDesign_admits hts]; rfl
  funext k
  rcases k with _|_|_|_|_|_|_|_|n
  all_goals first
    | decide +kernel
    | (rw [geomAt_out hts (by omega)]; rfl)

theorem geomAt_twoMem : geomAt twoMemDesign = twoMemG := by
  obtain ⟨ts, hts⟩ := admitsB_infer twoMemDesign_admits
  have hsz : ts.size = 10 := by
    rw [admitsB_size twoMemDesign_admits hts]; rfl
  funext k
  rcases k with _|_|_|_|_|_|_|_|_|_|n
  all_goals first
    | decide +kernel
    | (rw [geomAt_out hts (by omega)]; rfl)

/-- `mixedG_fits`, with the hand case analysis deleted. -/
theorem mixed_fits_checked (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ mixedDesign.nodes.toList, WriteAddrFits mixedDesign i st mixedG c := by
  rw [← geomAt_mixed]
  exact admits_writeAddrFits mixedDesign_admits i st

/-- `twoMemG_fits`, with the hand case analysis deleted. -/
theorem twoMem_fits_checked (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ twoMemDesign.nodes.toList, WriteAddrFits twoMemDesign i st twoMemG c := by
  rw [← geomAt_twoMem]
  exact admits_writeAddrFits twoMemDesign_admits i st

/-- `mixed_fold_resolved`, with EVERY premise taken from the one Boolean check:
no `mixedG_src`, no `mixedDesign_admitted`, no `mixedDesign_arity`, no
`mixedG_prop`, no `mixedG_fits`. -/
theorem mixed_fold_checked (i : RuntimeInput) (st : StateM)
    (hst : StateShapeOK mixedDesign st) :
    ∃ ss' : List SlotV,
      PrefixRelM mixedDesign i st mixedG
        (mixedDesign.sources.size + mixedDesign.nodes.size) ss'
      ∧ Eval hwMemP []
          (.call 47
             ([ encListG encNode mixedDesign.nodes.toList
              , objEnv ((mixedDesign.sources.toList.map (srcSlotV i st)).map SlotV.enc)
              , Val.int (Int.ofNat mixedDesign.sources.size) ].map Term.lit))
          (objEnv (ss'.map SlotV.enc)) := by
  rw [← geomAt_mixed]
  obtain ⟨hwf, hsem, hgsrc, hadm, harity, hprop, -⟩ :=
    admits_static_premises mixedDesign_admits
  exact sourcesThenNodes_resolved hwf hsem hst hgsrc hadm harity hprop
    (admits_writeAddrFits mixedDesign_admits i st)

/-- The two-memory fold, likewise checker-derived. -/
theorem twoMem_fold_checked (i : RuntimeInput) :
    ∃ ss' : List SlotV,
      PrefixRelM twoMemDesign i twoMemState twoMemG
        (twoMemDesign.sources.size + twoMemDesign.nodes.size) ss'
      ∧ Eval hwMemP []
          (.call 47
             ([ encListG encNode twoMemDesign.nodes.toList
              , objEnv ((twoMemDesign.sources.toList.map (srcSlotV i twoMemState)).map
                          SlotV.enc)
              , Val.int (Int.ofNat twoMemDesign.sources.size) ].map Term.lit))
          (objEnv (ss'.map SlotV.enc)) := by
  rw [← geomAt_twoMem]
  obtain ⟨hwf, hsem, hgsrc, hadm, harity, hprop, -⟩ :=
    admits_static_premises twoMemDesign_admits
  exact sourcesThenNodes_resolved hwf hsem twoMemState_shape hgsrc hadm harity hprop
    (admits_writeAddrFits twoMemDesign_admits i twoMemState)

#audit_axioms geomAt_mixed
#audit_axioms geomAt_twoMem
#audit_axioms mixed_fits_checked
#audit_axioms twoMem_fits_checked
#audit_axioms mixed_fold_checked
#audit_axioms twoMem_fold_checked

#audit_axioms admits_writeAddrFits
#audit_axioms admits_mainM_resolved
#audit_axioms eval_op_cert_width_nonMem
#audit_axioms srcTy_bv_width
#audit_axioms nodeTy_result
#audit_axioms admits_slot_width

end HwMem
end Projection
