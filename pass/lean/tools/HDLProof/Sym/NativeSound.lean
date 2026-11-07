import HDLProof.Sym.Native

/-! Soundness lemmas for native symbolic operations. These are quantified over
every consistent assignment; executable comparisons are kept in the tests.
-/

open Projection Compiler Std.Tactic.BVDecide
namespace HDLProof.Sym.Native

theorem constVal_sound (σ : Asg) (w : Nat) (v : Int) :
    SRel σ (constVal w v) (ofBV (mk_bv w v)) := .conc

theorem resize_sound {σ : Asg} {w : Nat} {a result : SVal} {x : BV}
    (ha : SRel σ a (ofBV x)) (h : resize w a = some result) :
    SRel σ result (ofBV (bv_resize w x)) := by
  apply symBvResize_sound h (.cons .conc (.cons ha .nil))
  simp [evalPrim, widthOf, asBV_ofBV]

theorem select_sound {σ : Asg} {w : Nat} {c a b result : SVal} {sel x y : BV}
    (hc : SRel σ c (ofBV sel)) (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (h : select w c a b = some result) :
    SRel σ result (ofBV (if bv_nonzero sel then bv_resize w x else bv_resize w y)) := by
  simp only [select, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨wc, C⟩, hC, ⟨wa, A⟩, hA, ⟨wb, B⟩, hB, rfl⟩ := h
  obtain ⟨sc, hsc, _, uc⟩ := asSBV_sound hc hC
  obtain ⟨xa, hxa, _, ua⟩ := asSBV_sound ha hA
  obtain ⟨yb, hyb, _, ub⟩ := asSBV_sound hb hB
  rw [asBV_ofBV] at hsc hxa hyb
  cases hsc; cases hxa; cases hyb
  apply SRel_ofBV
  · split <;> rfl
  · rw [eval_muxE, bit_nzBit uc]
    split
    · rw [resize_rel ua, eval_zextE]
    · rw [resize_rel ub, eval_zextE]

theorem not_sound {σ : Asg} {w : Nat} {a result : SVal} {x : BV}
    (ha : SRel σ a (ofBV x))
    (h : symBvNot [widthVal w, a] = some result) :
    SRel σ result (ofBV (bv_not w x)) := by
  apply symBvNot_sound h (.cons .conc (.cons ha .nil))
  simp [evalPrim, widthOf, asBV_ofBV]

theorem sra_sound {σ : Asg} {w : Nat} {a b result : SVal} {x y : BV}
    (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (h : symBvSra [widthVal w, a, b] = some result) :
    SRel σ result (ofBV (bv_sra w x y)) := by
  apply symBvSra_sound h (.cons .conc (.cons ha (.cons hb .nil)))
  simp [evalPrim, widthOf, asBV_ofBV]

theorem shl_sound {σ : Asg} {w : Nat} {a b result : SVal} {x y : BV}
    (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (h : symBvShl [widthVal w, a, b] = some result) :
    SRel σ result (ofBV (bv_shl_step w x y)) := by
  apply symBvShl_sound h (.cons .conc (.cons ha (.cons hb .nil)))
  simp [evalPrim, widthOf, asBV_ofBV]

theorem getMask_sound {σ : Asg} {w : Nat} {a b result : SVal} {x y : BV}
    (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (h : symBvGetMask [widthVal w, a, b] = some result) :
    SRel σ result (ofBV (bv_get_mask w x y)) := by
  apply symBvGetMask_sound h (.cons .conc (.cons ha (.cons hb .nil)))
  simp [evalPrim, widthOf, asBV_ofBV]

theorem memRead_sound {σ : Asg} {w : Nat} {image a en result : SVal}
    {m : MemImg} {addr enable : BV}
    (hm : SRel σ image (MemImg.encMemImg m))
    (ha : SRel σ a (ofBV addr)) (he : SRel σ en (ofBV enable))
    (h : symMemRead [widthVal w, image, a, en] = some result) :
    SRel σ result (ofBV (cert_mem_read w m.denote addr enable)) := by
  exact symMemRead_sound h (.cons .conc (.cons hm (.cons ha (.cons he .nil))))
    (MemImg.evalPrim_memRead w m addr enable)

theorem memWrite_sound {σ : Asg} {image a d en result : SVal}
    {m : MemImg} {addr value enable : BV}
    (hm : SRel σ image (MemImg.encMemImg m))
    (ha : SRel σ a (ofBV addr)) (hd : SRel σ d (ofBV value))
    (he : SRel σ en (ofBV enable))
    (hr : bv_nonzero enable → MemImg.InRange m (bv_uint addr))
    (h : symMemWrite [image, a, d, en] = some result) :
    SRel σ result (MemImg.encMemImg (MemImg.writeEn m addr value enable)) := by
  apply symMemWrite_sound h (.cons hm (.cons ha (.cons hd (.cons he .nil))))
  by_cases hen : bv_nonzero enable
  · simp [evalPrim, MemImg.valMemWrite_eq m addr value enable hen (hr hen)]
  · simp [evalPrim, MemImg.writeEn, hen, MemImg.valMemWrite_disabled m addr value enable hen]

theorem memWriteBE_sound {σ : Asg} {w bw : Nat} {image a d be result : SVal}
    {m : MemImg} {addr value enable : BV}
    (hm : SRel σ image (MemImg.encMemImg m))
    (ha : SRel σ a (ofBV addr)) (hd : SRel σ d (ofBV value))
    (he : SRel σ be (ofBV enable))
    (hr : bv_nonzero enable → MemImg.InRange m (bv_uint addr))
    (h : symMemWriteBE [widthVal w, image, a, d, be, widthVal bw] = some result) :
    SRel σ result (MemImg.encMemImg (MemImg.writeBE w m addr value enable bw)) := by
  apply symMemWriteBE_sound h (.cons .conc (.cons hm (.cons ha (.cons hd (.cons he (.cons .conc .nil))))))
  by_cases hen : bv_nonzero enable
  · exact MemImg.evalPrim_memWriteBE w m addr value enable bw hen (hr hen)
  · simp [evalPrim, widthOf, MemImg.valMemWriteBE_disabled w m addr value enable bw hen,
      MemImg.writeBE, hen]

theorem boolResult_sound {σ : Asg} {w : Nat} {e : BVExpr 1} {b : Bool}
    (h : bitOf (e.eval σ) = b) :
    SRel σ (.bv w (zextE w e)) (ofBV (mk_bv w (if b then 1 else 0))) := by
  have he : e.eval σ = BitVec.ofBool b := by
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    have : i = 0 := by omega
    subst i
    simpa [bitOf] using h
  refine SRel_ofBV (r := mk_bv w (if b then 1 else 0)) rfl ?_
  rw [eval_zextE, he, bv_uint_mk_bv, BitVec.toNat_setWidth, two_pow_cast]
  cases b <;> simp

theorem predicateResult_sound {σ : Asg} {w : Nat} {e : BVExpr 1} {p : Prop} [Decidable p]
    (h : bitOf (e.eval σ) = decide p) :
    SRel σ (.bv w (zextE w e)) (ofBV (mk_bv w (if p then 1 else 0))) := by
  simpa only [decide_eq_true_eq] using (boolResult_sound (w := w) h)

theorem compare_sound {σ : Asg} {w : Nat} {a b result : SVal} {x y : BV}
    {signed greater : Bool}
    (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (h : compare signed greater w a b = some result) :
    SRel σ result (ofBV (mk_bv w
      (let vx := if signed then bv_sint x else bv_uint x
       let vy := if signed then bv_sint y else bv_uint y
       if (if greater then vy < vx else vx < vy) then 1 else 0))) := by
  simp only [compare, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨wa, A⟩, hA, ⟨wb, B⟩, hB, rfl⟩ := h
  obtain ⟨xa, hxa, wx, ua⟩ := asSBV_sound ha hA
  obtain ⟨yb, hyb, wy, ub⟩ := asSBV_sound hb hB
  rw [asBV_ofBV] at hxa hyb
  cases hxa; cases hyb
  have sx := sint_rel wx ua
  have sy := sint_rel wy ub
  cases signed <;> cases greater <;> dsimp only <;> apply predicateResult_sound <;>
    simp [SInt.bit_lt, SInt.val_ofSint, SInt.val_ofUint, sx, sy, ua, ub]

theorem bitwise_sound {σ : Asg} {w : Nat} {a b result : SVal} {x y : BV}
    {op : BVBinOp} {f : Bool → Bool → Bool}
    (hop : ∀ {n : Nat} (a b : BitVec n) (i : Nat),
      (op.eval a b).getLsbD i = f (a.getLsbD i) (b.getLsbD i))
    (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y))
    (h : symBvBitwise op [widthVal w, a, b] = some result) :
    SRel σ result (ofBV (bv_bitwise w f x y)) := by
  simp only [symBvBitwise, Option.bind_eq_bind, Option.bind_eq_some_iff,
    Option.pure_def, Option.some.injEq] at h
  obtain ⟨wz, hwz, ⟨wa, A⟩, hA, ⟨wb, B⟩, hB, rfl⟩ := h
  have hwz' : wz = (w : Int) := by simpa [widthVal, cIntOf] using hwz.symm
  subst wz
  obtain ⟨xa, hxa, _, ua⟩ := asSBV_sound ha hA
  obtain ⟨yb, hyb, _, ub⟩ := asSBV_sound hb hB
  rw [asBV_ofBV] at hxa hyb
  cases hxa; cases hyb
  apply SRel_ofBV (by simp [widthOf, bv_bitwise, mk_bv])
  apply bitwise_rel ua ub
  intro i hi
  simp only [BVExpr.eval_bin, hop, getLsbD_zextE, widthOf, Int.toNat_natCast,
    hi, decide_true, Bool.true_and]

theorem foldBits_sound {σ : Asg} {w : Nat} {op : BVBinOp} {f : Bool → Bool → Bool}
    (hop : ∀ {n : Nat} (a b : BitVec n) (i : Nat),
      (op.eval a b).getLsbD i = f (a.getLsbD i) (b.getLsbD i)) :
    ∀ (ss : List SVal) (xs : List BV) (acc result : SVal) (v : BV),
      SRelL σ ss (xs.map ofBV) → SRel σ acc (ofBV v) →
      foldBits op w acc ss = some result →
      SRel σ result (ofBV (xs.foldl (bv_bitwise w f) v)) := by
  intro ss
  induction ss with
  | nil =>
      intro xs acc result v hr ha h
      have hx := SRelL_nil hr
      have : xs = [] := by simpa using hx.symm
      subst xs
      simp [foldBits] at h
      subst result
      exact ha
  | cons a ss ih =>
      intro xs acc result v hr ha h
      cases xs with
      | nil => obtain ⟨_, _, he, _, _⟩ := SRelL_cons_inv hr; cases he
      | cons x xs =>
          obtain ⟨_, _, he, hx, hxs⟩ := SRelL_cons_inv hr
          cases he
          simp only [foldBits, List.foldlM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
          obtain ⟨acc', hacc, hrest⟩ := h
          exact ih xs acc' result (bv_bitwise w f v x) hxs (bitwise_sound hop ha hx hacc) hrest

theorem source_sound {σ : Asg} {si sf sm result : SVal} {inputs : RuntimeInput} {st : StateM}
    {sd : SourceDesc}
    (hi : SRel σ si (encInput inputs)) (hf : SRel σ sf (encBVs st.flops))
    (hm : SRel σ sm (encArr MemImg.encMemImg st.mems))
    (h : source si sf sm sd = some result) :
    SRel σ result (HwMem.srcSlotV inputs st sd).enc := by
  cases sd with
  | const w v => cases h; exact .conc
  | memConst aw dw contents => cases h; exact .conc
  | input idx w =>
      simp only [source, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨a, ha, hr⟩ := h
      obtain ⟨x, hx, rx⟩ := sNth_sound si inputs.toList idx a hi ha
      have hx' : inputs[idx]? = some x := by simpa using hx
      simpa [HwMem.srcSlotV, HwMem.SlotV.enc, sourceValue, CertVal.asBV, encBV, hx'] using resize_sound rx hr
  | flopQ idx w =>
      simp only [source, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨a, ha, hr⟩ := h
      obtain ⟨x, hx, rx⟩ := sNth_sound sf st.flops.toList idx a hf ha
      have hx' : st.flops[idx]? = some x := by simpa using hx
      simpa [HwMem.srcSlotV, HwMem.SlotV.enc, sourceValue, CertVal.asBV, encBV, StateM.denote, hx'] using resize_sound rx hr
  | memImg idx aw dw =>
      obtain ⟨m, hidx, hr⟩ := sNth_sound sm st.mems.toList idx result hm h
      have hidx' : st.mems[idx]? = some m := by simpa using hidx
      simpa [HwMem.srcSlotV, HwMem.SlotV.enc, Hardware.srcMemImg, hidx'] using hr
  | flopQAsync idx w ri rv al =>
      simp only [source, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨r, hr, q, hq, h⟩ := h
      obtain ⟨reset, hreset, rr⟩ := sNth_sound si inputs.toList ri r hi hr
      obtain ⟨old, hold, rq⟩ := sNth_sound sf st.flops.toList idx q hf hq
      have hreset' : inputs[ri]? = some reset := by simpa using hreset
      have hold' : st.flops[idx]? = some old := by simpa using hold
      have hresize : bv_resize w (mk_bv w rv) = mk_bv w rv := by
        simp [bv_resize, bv_uint, mk_bv]
      cases al with
      | false =>
          have hs := select_sound rr (constVal_sound σ w rv) rq h
          simpa [HwMem.srcSlotV, HwMem.SlotV.enc, sourceValue, CertVal.asBV, encBV, StateM.denote,
            hreset', hold', hresize] using hs
      | true =>
          have hs := select_sound rr rq (constVal_sound σ w rv) h
          cases hen : bv_nonzero reset <;>
            simpa [HwMem.srcSlotV, HwMem.SlotV.enc, sourceValue, CertVal.asBV, encBV, StateM.denote,
              hreset', hold', hresize, hen] using hs

#audit_axioms constVal_sound
#audit_axioms resize_sound
#audit_axioms select_sound
#audit_axioms not_sound
#audit_axioms sra_sound
#audit_axioms shl_sound
#audit_axioms getMask_sound
#audit_axioms memRead_sound
#audit_axioms memWrite_sound
#audit_axioms memWriteBE_sound
#audit_axioms compare_sound
#audit_axioms foldBits_sound
#audit_axioms source_sound
#audit_axioms runWith_extends

end HDLProof.Sym.Native
