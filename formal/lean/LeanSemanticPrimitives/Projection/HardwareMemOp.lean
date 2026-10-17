/-
# Operator preservation for the ACTUAL `hwMemS` / `hwMemP`

The second half of the dispatch layer: `applyOpM` against the reference
`eval_op_cert`.  Three disjoint obligations, proved separately because they rest
on DIFFERENT premises:

* **delegation** — every `OpSupported` operator still runs the ORIGINAL
  `applyOp`.  No opcode of a supported operator is 26 or 28
  (`OpSupported_code_ne`, decided over the operator type), so both guards take
  their false branch and `applyOp_agree` is replayed by `lift`.  The scalar work
  behind it — SHL/SRA/Sext, the mux family, the comparison widths — is not
  re-proved and not re-stated.
* **`Op_MemRead` (26)** — the object primitive's bridge `evalPrim_memRead`
  composed with the three slot reads.  The result is a BIT VECTOR, matching
  `opKind`: only the write operators produce an image.
* **`Op_MemWriteBE` (28)** — `evalPrim_memWriteBE` composed with four slot
  reads and the certificate payload `byte_w`, then `denote_writeBE` for the
  reference equality.

## The premises, stated exactly

* `InRange m (bv_uint a)` is a REAL premise of the enabled write and is carried,
  never discharged silently.  Out of range the finite table is deliberately NOT
  reference-equal: `cert_mem_write_be` updates the function at any address,
  while the primitive REFUSES (`valMemWriteBE_refuses_outOfRange`).  A refusal
  is sound — it cannot produce a wrong answer — but it is not agreement, so the
  admission policy owes this bound.  The DISABLED write needs no range premise
  and is proved separately.
* The operand KINDS are premises, in the shape `DirectCheck.depIsMem` already
  fixes: position 0 of a memory operator is an image, every other position is a
  bit vector.  They are stated as equations on the slot list rather than assumed
  through a coercion, so a memory-valued operand in a scalar position cannot be
  silently read as a `BV`.
* Nothing here changes `OpSupported` or admits any design.
-/
import LeanSemanticPrimitives.Projection.HardwareMemSource
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. No supported operator collides with the two memory opcodes -/

theorem OpSupported_code_ne {op : LGraphOp} (h : OpSupported op = true) :
    opCode op ≠ 26 ∧ opCode op ≠ 28 := by
  cases op <;> simp_all [OpSupported, opCode]

/-- …and 27, the PLAIN write, which `applyOpM` now dispatches too.  Stated
separately so `OpSupported_code_ne` keeps its exact statement. -/
theorem OpSupported_code_ne27 {op : LGraphOp} (h : OpSupported op = true) :
    opCode op ≠ 27 := by
  cases op <;> simp_all [OpSupported, opCode]

/-! ## 2. Delegation: every supported operator still runs the original `applyOp` -/

theorem applyOpM_scalar {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {op : LGraphOp} {w : Nat} {deps : List Nat} {σ : SEnv} {eo ew ed ee en : SExp}
    (hsup : OpSupported op = true) (harity : ArityOK op deps)
    (hb : ∀ d ∈ deps, d < vals.length)
    (hop : SEval hwMemS σ eo (encOp op))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat deps))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en])
      (encBV (eval_op op w (deps.map arg))) := by
  obtain ⟨h26, h28⟩ := OpSupported_code_ne hsup
  have h27 := OpSupported_code_ne27 hsup
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit ?_) ?_
  · simp [evalPrim, encNat]
    omega
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit ?_) ?_
  · simp [evalPrim, encNat]
    omega
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit ?_) ?_
  · simp [evalPrim, encNat]
    omega
  exact lift (applyOp_agree hsv hsup harity hb (.ref rfl) (.ref rfl) (.ref rfl)
                (.ref rfl) (.ref rfl))

/-! ## 3. `Op_MemRead` -/

theorem applyOpM_memRead {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w d0 d1 d2 : Nat} {m : MemImg} {a enb : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length) (hb2 : d2 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV enb)
    (hop : SEval hwMemS σ eo (encOp .Op_MemRead))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en])
      (encBV (cert_mem_read w (MemImg.denote m) a enb)) := by
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
  have hv1' : vals[d1]'hb1 = ofBV a := hv1
  have hv2' : vals[d2]'hb2 = ofBV enb := hv2
  refine .prim ?_ (MemImg.evalPrim_memRead w m a enb)
  refine .cons (.ref rfl) (.cons ?_ (.cons ?_ (.cons ?_ .nil)))
  · rw [← hv0]; exact lift (slot_agree hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
  · rw [← hv1']
    exact lift (slot_agree hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))
  · rw [← hv2']
    exact lift (slot_agree hb2 (.ref rfl) (.ref rfl)
      (SEval_hd (SEval_tl (SEval_tl (.ref rfl)))))

/-! ## 4. `Op_MemWriteBE`

The enabled case carries `InRange`; the disabled case does not need it.  Both
conclude with an ENCODED image whose `MemImg.denote` is the reference memory, so
the answer is a representable table, not a function postulated to exist. -/

theorem applyOpM_memWriteBE {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w bw d0 d1 d2 d3 : Nat} {m : MemImg} {a dat be : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV be)
    (hen : bv_nonzero be) (hr : MemImg.InRange m (bv_uint a))
    (hop : SEval hwMemS σ eo (encOp (.Op_MemWriteBE bw)))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2, d3]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en])
      (MemImg.encMemImg (MemImg.writeBE w m a dat be bw)) := by
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  -- past opcode 27 (the PLAIN write) as well
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
  have hv1' : vals[d1]'hb1 = ofBV a := hv1
  have hv2' : vals[d2]'hb2 = ofBV dat := hv2
  have hv3' : vals[d3]'hb3 = ofBV be := hv3
  refine .prim ?_ (MemImg.evalPrim_memWriteBE w m a dat be bw hen hr)
  refine .cons (.ref rfl)
    (.cons ?_ (.cons ?_ (.cons ?_ (.cons ?_ (.cons (.ref rfl) .nil)))))
  · rw [← hv0]; exact lift (slot_agree hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
  · rw [← hv1']
    exact lift (slot_agree hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))
  · rw [← hv2']
    exact lift (slot_agree hb2 (.ref rfl) (.ref rfl)
      (SEval_hd (SEval_tl (SEval_tl (.ref rfl)))))
  · rw [← hv3']
    exact lift (slot_agree hb3 (.ref rfl) (.ref rfl)
      (SEval_hd (SEval_tl (SEval_tl (SEval_tl (.ref rfl))))))

/-- **The reference equality for the enabled write.**  `denote_writeBE` is the
bridge; `InRange` is its premise and is the SAME premise the primitive demands. -/
theorem applyOpM_memWriteBE_denote {w bw : Nat} {m : MemImg} {a dat be : BV}
    (hr : MemImg.InRange m (bv_uint a)) :
    MemImg.denote (MemImg.writeBE w m a dat be bw)
      = cert_mem_write_be w (MemImg.denote m) a dat be bw :=
  MemImg.denote_writeBE w m a dat be bw hr

/-- A DISABLED byte-enabled write needs no range premise: the image is
unchanged, and so is the reference memory. -/
theorem applyOpM_memWriteBE_disabled {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w bw d0 d1 d2 d3 : Nat} {m : MemImg} {a dat be : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV be)
    (hen : ¬ bv_nonzero be)
    (hop : SEval hwMemS σ eo (encOp (.Op_MemWriteBE bw)))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2, d3]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) (MemImg.encMemImg m)
    ∧ MemImg.denote m = cert_mem_write_be w (MemImg.denote m) a dat be bw := by
  constructor
  · refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
    refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
    refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
    -- past opcode 27 (the PLAIN write) as well
    refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
    refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
    have hp : evalPrim .memWriteBE
        [.int (Int.ofNat w), MemImg.encMemImg m, ofBV a, ofBV dat, ofBV be,
         .int (Int.ofNat bw)]
        = .ok (MemImg.encMemImg m) := by
      simp [evalPrim, widthOf, MemImg.valMemWriteBE_disabled w m a dat be bw hen]
    have hv1' : vals[d1]'hb1 = ofBV a := hv1
    have hv2' : vals[d2]'hb2 = ofBV dat := hv2
    have hv3' : vals[d3]'hb3 = ofBV be := hv3
    refine .prim ?_ hp
    refine .cons (.ref rfl)
      (.cons ?_ (.cons ?_ (.cons ?_ (.cons ?_ (.cons (.ref rfl) .nil)))))
    · rw [← hv0]; exact lift (slot_agree hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
    · rw [← hv1']
      exact lift (slot_agree hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))
    · rw [← hv2']
      exact lift (slot_agree hb2 (.ref rfl) (.ref rfl)
        (SEval_hd (SEval_tl (SEval_tl (.ref rfl)))))
    · rw [← hv3']
      exact lift (slot_agree hb3 (.ref rfl) (.ref rfl)
        (SEval_hd (SEval_tl (SEval_tl (SEval_tl (.ref rfl))))))
  · simp [cert_mem_write_be, hen]


/-! ## 5. The three cases against the SINGLE reference function

`eval_op_cert` is the reference's one operator semantics over `CertVal`, so the
three dispatch cases are restated against it rather than against three separate
helpers.  `SrcRel` carries the KIND: the read answers a bit vector, the write
answers an encoded image whose `MemImg.denote` is the reference memory. -/

theorem OpSupported_not_mem {op : LGraphOp} (h : OpSupported op = true) :
    ∀ b, op ≠ .Op_MemRead ∧ op ≠ .Op_MemWrite ∧ op ≠ .Op_MemWriteBE b := by
  intro b; cases op <;> simp_all [OpSupported]

theorem applyOpM_scalar_cert {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {op : LGraphOp} {w : Nat} {deps : List Nat} {σ : SEnv} {eo ew ed ee en : SExp}
    (hsup : OpSupported op = true) (harity : ArityOK op deps)
    (hb : ∀ d ∈ deps, d < vals.length)
    (hop : SEval hwMemS σ eo (encOp op))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat deps))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    ∃ v, SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) v
       ∧ SrcRel v (eval_op_cert op w ((deps.map arg).map CertVal.bv)) := by
  refine ⟨_, applyOpM_scalar hsv hsup harity hb hop hw hdv henv hlen, ?_⟩
  rw [eval_op_cert_bv op w (deps.map arg) (OpSupported_not_mem hsup)]
  rfl

theorem applyOpM_memRead_cert {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w d0 d1 d2 : Nat} {m : MemImg} {a enb : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length) (hb2 : d2 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV enb)
    (hop : SEval hwMemS σ eo (encOp .Op_MemRead))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    ∃ v, SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) v
       ∧ SrcRel v (eval_op_cert .Op_MemRead w
                     [.mem (MemImg.denote m), .bv a, .bv enb]) :=
  ⟨_, applyOpM_memRead hb0 hb1 hb2 hv0 hv1 hv2 hop hw hdv henv hlen, rfl⟩

/-- The enabled byte-enabled write, against the reference.  `InRange` is carried
and is the SAME premise the primitive itself enforces. -/
theorem applyOpM_memWriteBE_cert {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w bw d0 d1 d2 d3 : Nat} {m : MemImg} {a dat be : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hb2 : d2 < vals.length) (hb3 : d3 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV a)
    (hv2 : vals[d2]'hb2 = encBV dat)
    (hv3 : vals[d3]'hb3 = encBV be)
    (hen : bv_nonzero be) (hr : MemImg.InRange m (bv_uint a))
    (hop : SEval hwMemS σ eo (encOp (.Op_MemWriteBE bw)))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2, d3]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    ∃ v, SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) v
       ∧ SrcRel v (eval_op_cert (.Op_MemWriteBE bw) w
                     [.mem (MemImg.denote m), .bv a, .bv dat, .bv be]) :=
  ⟨_, applyOpM_memWriteBE hb0 hb1 hb2 hb3 hv0 hv1 hv2 hv3 hen hr hop hw hdv henv hlen,
   MemImg.writeBE w m a dat be bw, rfl, applyOpM_memWriteBE_denote hr⟩

/-! ## 6. Execution

`HardwareMemChecks` already runs both memory opcodes through `hwMemP`, including
the out-of-range refusal and the malformed image.  What it does not run is
OPERATOR DELEGATION, so that is pinned here: a scalar opcode through `applyOpM`
reaches the original `applyOp` and answers what it always answered. -/

private def applyOpMId : Nat := (idOf "applyOpM").getD 999

/- Op_And (code 7) on two scalar slots, through the memory-aware dispatch -/
#guard (match evalFuel 40000 hwMemP []
          (.call applyOpMId
             [ .lit (.ctor tagOp [.int 7, .nil]), .lit (.int 8)
             , .lit (encListG encNat [0, 1])
             , .lit (.cons (encBV (mk_bv 8 0x0F)) (.cons (encBV (mk_bv 8 0x3C)) .nil))
             , .lit (.int 2) ]) with
        | .value v => v == encBV (mk_bv 8 0x0C) | _ => false)

/- Op_SRA (code 20), the operator whose correctness repair must not be lost -/
#guard (match evalFuel 40000 hwMemP []
          (.call applyOpMId
             [ .lit (.ctor tagOp [.int 20, .nil]), .lit (.int 8)
             , .lit (encListG encNat [0, 1])
             , .lit (.cons (encBV (mk_bv 8 1)) (.cons (encBV (mk_bv 8 0x80)) .nil))
             , .lit (.int 2) ]) with
        | .value v => v == encBV (eval_op .Op_SRA 8 [mk_bv 8 0x80, mk_bv 8 1])
        | _ => false)

/-! ## 7. Axiom audit — no NEW unsafe axiom (no `sorryAx`, no
`Lean.ofReduceBool`, no `Lean.trustCompiler`); `propext`/`Quot.sound`/
`Classical.choice` are Lean's own. -/

#audit_axioms OpSupported_code_ne
#audit_axioms OpSupported_not_mem
#audit_axioms applyOpM_scalar
#audit_axioms applyOpM_scalar_cert
#audit_axioms applyOpM_memRead
#audit_axioms applyOpM_memRead_cert
#audit_axioms applyOpM_memWriteBE
#audit_axioms applyOpM_memWriteBE_denote
#audit_axioms applyOpM_memWriteBE_cert
#audit_axioms applyOpM_memWriteBE_disabled

end HwMem
end Projection
