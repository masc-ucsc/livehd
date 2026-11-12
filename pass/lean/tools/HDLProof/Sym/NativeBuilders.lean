import HDLProof.Sym.NativeSound
import LeanSemanticPrimitives.Projection.SetMaskLoop
import LeanSemanticPrimitives.Projection.HardwareAdequacy
import LeanSemanticPrimitives.Projection.HardwareMemLoad

/-! The operator builders of the IR and VC traversals, restated as folds and recursions that
follow the shared `eval_op` clause by clause, each with its soundness lemma, and the IR's
dispatch `evalOp` with `evalOp_sound`. Mathlib-free. -/
open Projection Compiler Std.Tactic.BVDecide Projection.Hw Projection.HwMem
namespace HDLProof.Sym.Native
/-! ### from Sum.lean -/


theorem SRelL_take {σ : Asg} : ∀ {ss : List SVal} {vs : List Val} (n : Nat),
    SRelL σ ss vs → SRelL σ (ss.take n) (vs.take n)
  | _, _, 0, _ => by simp; exact .nil
  | [], _, n + 1, h => by cases h; exact .nil
  | _ :: _, _, n + 1, .cons h t => by simpa using SRelL.cons h (SRelL_take n t)

theorem SRelL_drop {σ : Asg} : ∀ {ss : List SVal} {vs : List Val} (n : Nat),
    SRelL σ ss vs → SRelL σ (ss.drop n) (vs.drop n)
  | _, _, 0, h => by simpa using h
  | [], _, n + 1, h => by cases h; exact .nil
  | _ :: _, _, n + 1, .cons _ t => by simpa using SRelL_drop n t

/-- A related bit vector, zero-extended to `w`, is `ofInt w` of its unsigned value. -/
theorem zext_ofInt {σ : Asg} {s : SVal} {b : BV} {w wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    (zextE w X).eval σ = BitVec.ofInt w (bv_uint b) := by
  obtain ⟨b', hb', _, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'
  rw [eval_zextE, hu, BitVec.ofInt_natCast, BitVec.ofNat_toNat]

def addStep (w : Nat) (acc : BVExpr w) (x : SVal) : Option (BVExpr w) := do
  let ⟨_, X⟩ ← asSBV x
  pure (.bin acc .add (zextE w X))

def subStep (w : Nat) (acc : BVExpr w) (x : SVal) : Option (BVExpr w) := do
  let ⟨_, X⟩ ← asSBV x
  pure (.bin acc .add (neg (zextE w X)))

def sumB (w nAdd : Nat) (xs : List SVal) : Option SVal := do
  let a ← (xs.take nAdd).foldlM (addStep w) (.const 0#w)
  let r ← (xs.drop nAdd).foldlM (subStep w) a
  pure (.bv w r)

theorem neg_eval {σ : Asg} {w : Nat} (a : BVExpr w) : (neg a).eval σ = -(a.eval σ) := by
  simp [neg, BitVec.neg_eq_not_add]

theorem addFold_sound {σ : Asg} {w : Nat} :
    ∀ (ss : List SVal) (bs : List BV) (acc r : BVExpr w) (z : Int),
      SRelL σ ss (bs.map ofBV) → acc.eval σ = BitVec.ofInt w z →
      ss.foldlM (addStep w) acc = some r →
      r.eval σ = BitVec.ofInt w (z + (bs.map bv_uint).sum)
  | [], bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => simp at h; subst h; simpa using ha
      | cons _ _ => cases hr
  | s :: ss, bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [List.foldlM_cons, addStep, Option.bind_eq_bind, Option.bind_eq_some_iff,
                Option.pure_def, Option.some.injEq] at h
              obtain ⟨_, ⟨⟨wx, X⟩, hx, rfl⟩, hrest⟩ := h
              have := addFold_sound ss bs _ r (z + bv_uint b) h2
                (by simp [BVExpr.eval_bin, ha, zext_ofInt h1 hx, BitVec.ofInt_add]) hrest
              simpa [Int.add_assoc] using this

theorem subFold_sound {σ : Asg} {w : Nat} :
    ∀ (ss : List SVal) (bs : List BV) (acc r : BVExpr w) (z : Int),
      SRelL σ ss (bs.map ofBV) → acc.eval σ = BitVec.ofInt w z →
      ss.foldlM (subStep w) acc = some r →
      r.eval σ = BitVec.ofInt w (z - (bs.map bv_uint).sum)
  | [], bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => simp at h; subst h; simpa using ha
      | cons _ _ => cases hr
  | s :: ss, bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [List.foldlM_cons, subStep, Option.bind_eq_bind, Option.bind_eq_some_iff,
                Option.pure_def, Option.some.injEq] at h
              obtain ⟨_, ⟨⟨wx, X⟩, hx, rfl⟩, hrest⟩ := h
              have := subFold_sound ss bs _ r (z - bv_uint b) h2
                (by simp [BVExpr.eval_bin, ha, neg_eval, zext_ofInt h1 hx, Int.sub_eq_add_neg,
                      BitVec.ofInt_add, BitVec.ofInt_neg]) hrest
              rw [this]; congr 1; simp; omega

/-- The bit vectors of a related list. -/
theorem SRelL_map_take {bs : List BV} (n : Nat) : (bs.map ofBV).take n = (bs.take n).map ofBV := by
  simp [List.map_take]

theorem sum_sound {σ : Asg} {w nAdd : Nat} {ss : List SVal} {bs : List BV} {r : SVal}
    (hr : SRelL σ ss (bs.map ofBV)) (h : sumB w nAdd ss = some r) :
    SRel σ r (ofBV (eval_op (.Op_Sum nAdd) w bs)) := by
  simp only [sumB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨a, ha, r', hr', rfl⟩ := h
  have h1 := addFold_sound _ (bs.take nAdd) _ a 0
    (by rw [List.map_take]; exact SRelL_take nAdd hr) (by simp) ha
  have h2 := subFold_sound _ (bs.drop nAdd) _ r' _
    (by rw [List.map_drop]; exact SRelL_drop nAdd hr) h1 hr'
  refine SRel_ofBV (r := eval_op (.Op_Sum nAdd) w bs) rfl ?_
  rw [h2, Int.zero_add, ← mk_rel]
  simp only [eval_op, List.map_take, List.map_drop]



/-! ### from Prod.lean -/


theorem zext_ofInt' {σ : Asg} {s : SVal} {b : BV} {w wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    (zextE w X).eval σ = BitVec.ofInt w (bv_uint b) := by
  obtain ⟨b', hb', _, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'
  rw [eval_zextE, hu, BitVec.ofInt_natCast, BitVec.ofNat_toNat]

/-! ## product -/

def mulStep (w : Nat) (acc : BVExpr w) (x : SVal) : Option (BVExpr w) := do
  let ⟨_, X⟩ ← asSBV x
  pure (.bin acc .mul (zextE w X))

def productB (w : Nat) (xs : List SVal) : Option SVal := do
  let r ← xs.foldlM (mulStep w) (.const 1#w)
  pure (.bv w r)

theorem mulFold_sound {σ : Asg} {w : Nat} :
    ∀ (ss : List SVal) (bs : List BV) (acc r : BVExpr w) (z : Int),
      SRelL σ ss (bs.map ofBV) → acc.eval σ = BitVec.ofInt w z →
      ss.foldlM (mulStep w) acc = some r →
      r.eval σ = BitVec.ofInt w (z * (bs.map bv_uint).prod)
  | [], bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => simp at h; subst h; simpa using ha
      | cons _ _ => cases hr
  | s :: ss, bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [List.foldlM_cons, mulStep, Option.bind_eq_bind, Option.bind_eq_some_iff,
                Option.pure_def, Option.some.injEq] at h
              obtain ⟨_, ⟨⟨wx, X⟩, hx, rfl⟩, hrest⟩ := h
              have := mulFold_sound ss bs _ r (z * bv_uint b) h2
                (by simp [BVExpr.eval_bin, ha, zext_ofInt' h1 hx, BitVec.ofInt_mul]) hrest
              simpa [Int.mul_assoc] using this

theorem product_sound {σ : Asg} {w : Nat} {ss : List SVal} {bs : List BV} {r : SVal}
    (hr : SRelL σ ss (bs.map ofBV)) (h : productB w ss = some r) :
    SRel σ r (ofBV (eval_op .Op_Mult w bs)) := by
  simp only [productB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨r', hr', rfl⟩ := h
  have h1 := mulFold_sound _ bs _ r' 1 hr (by simp) hr'
  refine SRel_ofBV (r := eval_op .Op_Mult w bs) rfl ?_
  rw [h1, Int.one_mul, ← mk_rel]
  simp only [eval_op]

/-! ## redOr -/

def orStep (acc : BVExpr 1) (x : SVal) : Option (BVExpr 1) := do
  let ⟨_, X⟩ ← asSBV x
  pure (orE acc (nzBit X))

def redOrB (w : Nat) (xs : List SVal) : Option SVal := do
  let r ← xs.foldlM orStep falseE
  pure (.bv w (zextE w r))

theorem nz_bit {σ : Asg} {s : SVal} {b : BV} {wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    bitOf ((nzBit X).eval σ) = bv_nonzero b := by
  obtain ⟨b', hb', _, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'
  exact bit_nzBit hu

theorem orFold_sound {σ : Asg} :
    ∀ (ss : List SVal) (bs : List BV) (acc r : BVExpr 1) (z : Bool),
      SRelL σ ss (bs.map ofBV) → bitOf (acc.eval σ) = z →
      ss.foldlM orStep acc = some r →
      bitOf (r.eval σ) = (z || bs.any bv_nonzero)
  | [], bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => simp at h; subst h; simpa using ha
      | cons _ _ => cases hr
  | s :: ss, bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [List.foldlM_cons, orStep, Option.bind_eq_bind, Option.bind_eq_some_iff,
                Option.pure_def, Option.some.injEq] at h
              obtain ⟨_, ⟨⟨wx, X⟩, hx, rfl⟩, hrest⟩ := h
              have := orFold_sound ss bs _ r (z || bv_nonzero b) h2
                (by simp [ha, nz_bit h1 hx]) hrest
              simpa [Bool.or_assoc] using this

theorem redOr_sound {σ : Asg} {w : Nat} {ss : List SVal} {bs : List BV} {r : SVal}
    (hr : SRelL σ ss (bs.map ofBV)) (h : redOrB w ss = some r) :
    SRel σ r (ofBV (eval_op .Op_Ror w bs)) := by
  simp only [redOrB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨r', hr', rfl⟩ := h
  have h1 := orFold_sound _ bs _ r' false hr (by simp [falseE, bitOf]) hr'
  simp only [Bool.false_or] at h1
  simpa [eval_op] using boolResult_sound (w := w) h1



/-! ### from EqMux.lean -/


theorem rel_uint {σ : Asg} {s : SVal} {b : BV} {wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    bv_uint b = ((X.eval σ).toNat : Int) := by
  obtain ⟨b', hb', _, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'; exact hu

theorem zext_ofInt'' {σ : Asg} {s : SVal} {b : BV} {w wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    (zextE w X).eval σ = BitVec.ofInt w (bv_uint b) := by
  rw [eval_zextE, rel_uint hr hx, BitVec.ofInt_natCast, BitVec.ofNat_toNat]

/-! ## eqAll -/

def eqStep {wa : Nat} (A : BVExpr wa) (acc : BVExpr 1) (x : SVal) : Option (BVExpr 1) := do
  let ⟨_, B⟩ ← asSBV x
  pure (andE acc (SInt.eq (SInt.ofUint A) (SInt.ofUint B)))

def eqAllB (w : Nat) : List SVal → Option SVal
  | [] => some (constVal w 1)
  | a :: rest => do
      let ⟨_, A⟩ ← asSBV a
      let r ← rest.foldlM (eqStep A) trueE
      pure (.bv w (zextE w r))

theorem eqFold_sound {σ : Asg} {wa : Nat} {A : BVExpr wa} {ua : Int}
    (hA : ua = ((A.eval σ).toNat : Int)) :
    ∀ (ss : List SVal) (bs : List BV) (acc r : BVExpr 1) (z : Bool),
      SRelL σ ss (bs.map ofBV) → bitOf (acc.eval σ) = z →
      ss.foldlM (eqStep A) acc = some r →
      bitOf (r.eval σ) = (z && bs.all fun b => bv_uint b = ua)
  | [], bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => simp at h; subst h; simpa using ha
      | cons _ _ => cases hr
  | s :: ss, bs, acc, r, z, hr, ha, h => by
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [List.foldlM_cons, eqStep, Option.bind_eq_bind, Option.bind_eq_some_iff,
                Option.pure_def, Option.some.injEq] at h
              obtain ⟨_, ⟨⟨wx, X⟩, hx, rfl⟩, hrest⟩ := h
              have := eqFold_sound hA ss bs _ r (z && decide (bv_uint b = ua)) h2
                (by simp [ha, SInt.bit_eq, SInt.val_ofUint, rel_uint h1 hx, hA, eq_comm]) hrest
              simpa [Bool.and_assoc] using this

theorem eqAll_sound {σ : Asg} {w : Nat} {ss : List SVal} {bs : List BV} {r : SVal}
    (hr : SRelL σ ss (bs.map ofBV)) (h : eqAllB w ss = some r) :
    SRel σ r (ofBV (eval_op .Op_EQ w bs)) := by
  cases ss with
  | nil =>
      cases bs with
      | nil => simp [eqAllB] at h; subst h; exact .conc
      | cons _ _ => cases hr
  | cons a rest =>
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [eqAllB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
                Option.some.injEq] at h
              obtain ⟨⟨wa, A⟩, hA, r', hr', rfl⟩ := h
              have hf := eqFold_sound (rel_uint h1 hA) rest bs _ r' true h2
                (by simp [trueE, bitOf]) hr'
              simp only [Bool.true_and] at hf
              simpa [eval_op] using boolResult_sound (w := w) hf

/-! ## muxN -/

def muxGo (w : Nat) {ws : Nat} (S : BVExpr ws) : Nat → List SVal → BVExpr w → Option (BVExpr w)
  | _, [], acc => some acc
  | k, x :: xs, acc => do
      let ⟨_, X⟩ ← asSBV x
      muxGo w S (k + 1) xs (muxE (SInt.eq (SInt.ofUint S) (SInt.lit k)) (zextE w X) acc)

def muxNB (w : Nat) (sel : SVal) (xs : List SVal) : Option SVal := do
  let ⟨_, S⟩ ← asSBV sel
  let r ← muxGo w S 0 xs (.const 0#w)
  pure (.bv w r)

theorem muxGo_sound {σ : Asg} {w ws : Nat} {S : BVExpr ws} :
    ∀ (k : Nat) (ss : List SVal) (bs : List BV) (acc r : BVExpr w),
      SRelL σ ss (bs.map ofBV) → muxGo w S k ss acc = some r →
      r.eval σ = if h : k ≤ (S.eval σ).toNat ∧ (S.eval σ).toNat < k + bs.length
        then BitVec.ofInt w (bv_uint (bs[(S.eval σ).toNat - k]'(by omega)))
        else acc.eval σ
  | k, [], bs, acc, r, hr, h => by
      cases bs with
      | nil => simp [muxGo] at h; subst h; simp; intro; omega
      | cons _ _ => cases hr
  | k, s :: ss, bs, acc, r, hr, h => by
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [muxGo, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
              obtain ⟨⟨wx, X⟩, hx, hrest⟩ := h
              rw [muxGo_sound (k + 1) ss bs _ r h2 hrest, eval_muxE, SInt.bit_eq, SInt.val_ofUint,
                SInt.val_lit, zext_ofInt'' h1 hx]
              by_cases hk : (S.eval σ).toNat = k
              · subst hk; rw [dif_neg (by omega), dif_pos (by simp)]; simp
              · have hk' : ((S.eval σ).toNat : Int) ≠ (k : Int) := by omega
                simp only [hk', decide_false, if_false, List.length_cons]
                by_cases hin : k + 1 ≤ (S.eval σ).toNat ∧ (S.eval σ).toNat < k + 1 + bs.length
                · rw [dif_pos hin, dif_pos (by omega)]
                  have e : (S.eval σ).toNat - k = ((S.eval σ).toNat - (k + 1)) + 1 := by omega
                  simp [e]
                · rw [dif_neg hin, dif_neg (by omega)]; simp

theorem muxN_sound {σ : Asg} {w : Nat} {sel : SVal} {ss : List SVal} {s0 : BV} {bs : List BV} {r : SVal}
    (hs : SRel σ sel (ofBV s0)) (hr : SRelL σ ss (bs.map ofBV)) (h : muxNB w sel ss = some r) :
    SRel σ r (ofBV (eval_op .Op_MuxN w (s0 :: bs))) := by
  simp only [muxNB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨ws, S⟩, hS, r', hr', rfl⟩ := h
  have hm := muxGo_sound 0 ss bs _ r' hr hr'
  have hu := rel_uint hs hS
  refine SRel_ofBV (r := eval_op .Op_MuxN w (s0 :: bs)) (by simp only [eval_op]; split <;> rfl) ?_
  rw [hm]
  simp only [eval_op, hu, Int.toNat_natCast, Nat.zero_le, true_and, Nat.zero_add, Nat.sub_zero]
  split
  · rename_i hlt
    rw [List.getElem?_eq_getElem hlt, Option.getD_some]
    unfold bv_resize; exact mk_rel _ _
  · rename_i hlt
    rw [mk_rel]; simp



/-! ### from Div.lean -/


theorem rel_uint' {σ : Asg} {s : SVal} {b : BV} {wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    b.width = wx ∧ bv_uint b = ((X.eval σ).toNat : Int) := by
  obtain ⟨b', hb', hw, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'; exact ⟨hw, hu⟩

/-! ## unsigned division -/

def udivB (w : Nat) (a b : SVal) : Option SVal := do
  let ⟨wa, A⟩ ← asSBV a
  let ⟨wb, B⟩ ← asSBV b
  let n := max wa wb
  pure (.bv w (zextE w (BVExpr.bin (zextE n A) .udiv (zextE n B))))

theorem udiv_sound {σ : Asg} {w : Nat} {a b r : SVal} {x y : BV}
    (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y)) (h : udivB w a b = some r) :
    SRel σ r (ofBV (eval_op .Op_UDiv w [x, y])) := by
  simp only [udivB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨wa, A⟩, hA, ⟨wb, B⟩, hB, rfl⟩ := h
  obtain ⟨_, ux⟩ := rel_uint' ha hA
  obtain ⟨_, uy⟩ := rel_uint' hb hB
  refine SRel_ofBV (r := eval_op .Op_UDiv w [x, y]) rfl ?_
  have hA' : (A.eval σ).toNat < 2 ^ max wa wb :=
    BitVec.toNat_lt_twoPow_of_le (Nat.le_max_left _ _)
  have hB' : (B.eval σ).toNat < 2 ^ max wa wb :=
    BitVec.toNat_lt_twoPow_of_le (Nat.le_max_right _ _)
  simp only [eval_op, BVExpr.eval_bin, BVBinOp.eval, eval_zextE, BitVec.toNat_setWidth,
    BitVec.toNat_udiv, Nat.mod_eq_of_lt hA', Nat.mod_eq_of_lt hB', ux, uy, bv_uint_mk_bv]
  split
  · rename_i h0
    have : (B.eval σ).toNat = 0 := by omega
    simp [this]
  · rw [two_pow_cast]; push_cast [Int.natCast_emod, Int.natCast_ediv]; rfl

/-! ## signed division -/

def sdivB (w : Nat) (a b : SVal) : Option SVal := do
  let ⟨wa, A⟩ ← asSBV a
  let ⟨wb, B⟩ ← asSBV b
  let n := max wa wb + 1
  let x := sextE n A
  let y := sextE n B
  let sx : BVExpr 1 := .extract (n - 1) 1 x
  let sy : BVExpr 1 := .extract (n - 1) 1 y
  let q := BVExpr.bin (muxE sx (neg x) x) .udiv (muxE sy (neg y) y)
  let value := muxE (.bin sx .xor sy) (neg q) q
  pure (.bv w (muxE (nzBit B) (sextE w value) (.const 0#w)))

theorem neg_eval' {σ : Asg} {w : Nat} (a : BVExpr w) : (neg a).eval σ = -(a.eval σ) := by
  simp [neg, BitVec.neg_eq_not_add]

theorem bit_extract_msb {σ : Asg} {n : Nat} (hn : 0 < n) (x : BVExpr n) :
    bitOf ((BVExpr.extract (n - 1) 1 x).eval σ) = (x.eval σ).msb := by
  simp [bitOf, BVExpr.eval_extract, BitVec.getLsbD_extractLsb', BitVec.msb_eq_getLsbD_last]

/-- The absolute value of a value that is not the most negative one. -/
theorem abs_toNat {n : Nat} (X : BitVec n) (hlo : -2 ^ (n - 1) < X.toInt) :
    (if X.msb then -X else X).toNat = X.toInt.natAbs := by
  have hhi := BitVec.toInt_lt (x := X)
  rw [BitVec.msb_eq_toInt]
  by_cases hneg : X.toInt < 0
  · simp only [hneg, decide_true, if_true]
    have hpos : 0 < n := by
      rcases Nat.eq_zero_or_pos n with h0 | h0
      · subst h0; simp at hhi hlo; omega
      · exact h0
    have hne : X ≠ BitVec.intMin n := by
      intro he; rw [he, BitVec.toInt_intMin_of_pos hpos] at hlo; omega
    have h1 : (-X).toInt = -X.toInt := BitVec.toInt_neg_of_ne_intMin hne
    have hm : (-X).msb = false := by rw [BitVec.msb_eq_toInt]; simp [h1]; omega
    have := BitVec.toInt_eq_toNat_of_msb hm
    omega
  · simp only [hneg, decide_false]
    have hm : X.msb = false := by rw [BitVec.msb_eq_toInt]; simp [hneg]
    have := BitVec.toInt_eq_toNat_of_msb hm
    simp; omega

theorem two_pow_nat_int (k : Nat) : (((2 ^ k : Nat)) : Int) = (2 : Int) ^ k := by
  simp [Int.natCast_pow]

/-- A value at width `wa < n` is far from `intMin n`. -/
theorem toInt_bounds {wa n : Nat} (X : BitVec wa) (hn : wa < n) :
    -2 ^ (n - 1) < X.toInt ∧ X.toInt < 2 ^ (n - 1) := by
  have hlo := BitVec.le_toInt X
  have hhi := BitVec.toInt_lt (x := X)
  have hp := SInt.pow_pos' (n - 1)
  rcases Nat.eq_zero_or_pos wa with h0 | h0
  · subst h0
    have hx0 : X.toNat = 0 := by have := X.isLt; simp at this; omega
    have : X.toInt = 0 := by rw [BitVec.toInt_eq_toNat_cond]; simp [hx0]
    constructor <;> omega
  · have hm : (2 : Int) ^ (wa - 1) * 2 ≤ 2 ^ (n - 1) := by
      have := SInt.pow_mono (a := wa) (b := n - 1) (by omega)
      have e : (2 : Int) ^ wa = 2 ^ (wa - 1) * 2 := by
        rw [← Int.pow_succ]; congr 1; omega
      omega
    have := SInt.pow_pos' (wa - 1)
    constructor <;> omega

theorem natAbs_lt {t : Int} {k : Nat} (h1 : -2 ^ k < t) (h2 : t < 2 ^ k) : t.natAbs < 2 ^ k := by
  have : (t.natAbs : Int) < 2 ^ k := by rcases Int.natAbs_eq t with h | h <;> omega
  rw [← two_pow_nat_int] at this; exact_mod_cast this

theorem toInt_small {n : Nat} (q : BitVec n) (hq : q.toNat < 2 ^ (n - 1)) :
    q.toInt = q.toNat ∧ (-q).toInt = -(q.toNat : Int) := by
  have hm : q.msb = false := by
    rw [BitVec.msb_eq_decide]; simp only [decide_eq_false_iff_not, Nat.not_le]; exact hq
  have h1 := BitVec.toInt_eq_toNat_of_msb hm
  refine ⟨h1, ?_⟩
  rcases Nat.eq_zero_or_pos n with h0 | h0
  · subst h0; have : q.toNat = 0 := by have := q.isLt; simp at this; omega
    simp [BitVec.toInt_neg, this]
  · have hne : q ≠ BitVec.intMin n := by
      intro he
      have := BitVec.toInt_intMin_of_pos h0
      rw [← he] at this
      have := SInt.pow_pos' (n - 1)
      have e := two_pow_nat_int (n - 1)
      have : (q.toNat : Int) < (2 : Int) ^ (n - 1) := by rw [← e]; exact_mod_cast hq
      omega
    rw [BitVec.toInt_neg_of_ne_intMin hne, h1]

theorem bit_extract_last {σ : Asg} {k : Nat} (x : BVExpr (k + 1)) :
    bitOf ((BVExpr.extract k 1 x).eval σ) = (x.eval σ).msb := by
  simp [bitOf, BVExpr.eval_extract, BitVec.msb_eq_getLsbD_last]

theorem bitOf_xor (a b : BitVec 1) : bitOf (a ^^^ b) = (bitOf a ^^ bitOf b) := by
  simp [bitOf]

theorem sdiv_core {wa wb : Nat} (X : BitVec wa) (Y : BitVec wb) (hY : Y.toInt ≠ 0) :
    (if ((X.signExtend (max wa wb + 1)).msb ^^ (Y.signExtend (max wa wb + 1)).msb) then
        -((if (X.signExtend (max wa wb + 1)).msb then -(X.signExtend (max wa wb + 1))
            else X.signExtend (max wa wb + 1)) /
          (if (Y.signExtend (max wa wb + 1)).msb then -(Y.signExtend (max wa wb + 1))
            else Y.signExtend (max wa wb + 1)))
      else ((if (X.signExtend (max wa wb + 1)).msb then -(X.signExtend (max wa wb + 1))
            else X.signExtend (max wa wb + 1)) /
          (if (Y.signExtend (max wa wb + 1)).msb then -(Y.signExtend (max wa wb + 1))
            else Y.signExtend (max wa wb + 1)))).toInt
      = trunc_div_int X.toInt Y.toInt := by
  generalize hn : max wa wb + 1 = n
  obtain ⟨loA, hiA⟩ := toInt_bounds X (show wa < n by omega)
  obtain ⟨loB, hiB⟩ := toInt_bounds Y (show wb < n by omega)
  have tA : (X.signExtend n).toInt = X.toInt := BitVec.toInt_signExtend_of_le (by omega)
  have tB : (Y.signExtend n).toInt = Y.toInt := BitVec.toInt_signExtend_of_le (by omega)
  have aA := abs_toNat (X.signExtend n) (by rw [tA]; exact loA)
  have aB := abs_toNat (Y.signExtend n) (by rw [tB]; exact loB)
  rw [tA] at aA; rw [tB] at aB
  have mA : (X.signExtend n).msb = decide (X.toInt < 0) := by rw [BitVec.msb_eq_toInt, tA]
  have mB : (Y.signExtend n).msb = decide (Y.toInt < 0) := by rw [BitVec.msb_eq_toInt, tB]
  generalize hq : (if (X.signExtend n).msb then -(X.signExtend n) else X.signExtend n) /
      (if (Y.signExtend n).msb then -(Y.signExtend n) else Y.signExtend n) = q
  have hqn : q.toNat = X.toInt.natAbs / Y.toInt.natAbs := by
    rw [← hq, BitVec.toNat_udiv, aA, aB]
  have hqs : q.toNat < 2 ^ (n - 1) := by
    rw [hqn]; exact Nat.lt_of_le_of_lt (Nat.div_le_self _ _) (natAbs_lt loA hiA)
  obtain ⟨tq, tnq⟩ := toInt_small q hqs
  have key : ∀ (c : Bool), (if c then -q else q).toInt = if c then -(q.toNat : Int) else q.toNat := by
    intro c; cases c <;> simp [tq, tnq]
  rw [key, mA, mB, hqn]
  unfold trunc_div_int intAbs
  have hbn : ((if Y.toInt < 0 then -Y.toInt else Y.toInt).toNat) = Y.toInt.natAbs := by
    split <;> omega
  have han : ((if X.toInt < 0 then -X.toInt else X.toInt).toNat) = X.toInt.natAbs := by
    split <;> omega
  have hbz : Y.toInt.natAbs ≠ 0 := by omega
  simp only [han, hbn, hbz, if_false]
  by_cases ha' : X.toInt < 0 <;> by_cases hb' : Y.toInt < 0 <;> simp [ha', hb']

theorem sdiv_sound {σ : Asg} {w : Nat} {a b r : SVal} {x y : BV}
    (ha : SRel σ a (ofBV x)) (hb : SRel σ b (ofBV y)) (h : sdivB w a b = some r) :
    SRel σ r (ofBV (eval_op .Op_SDiv w [x, y])) := by
  simp only [sdivB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨wa, A⟩, hA, ⟨wb, B⟩, hB, rfl⟩ := h
  obtain ⟨wx, ux⟩ := rel_uint' ha hA
  obtain ⟨wy, uy⟩ := rel_uint' hb hB
  have six := sint_rel wx ux
  have siy := sint_rel wy uy
  refine SRel_ofBV (r := eval_op .Op_SDiv w [x, y]) rfl ?_
  have hz : bv_uint y = 0 ↔ (B.eval σ).toInt = 0 := by
    rw [uy, BitVec.toInt_eq_toNat_cond]
    have := (B.eval σ).isLt
    have e := two_pow_nat_int wb
    split <;> omega
  have hnz : bitOf ((nzBit B).eval σ) = bv_nonzero y := bit_nzBit uy
  rw [eval_muxE, hnz]
  simp only [eval_op, bv_sdiv, bv_nonzero]
  by_cases h0 : bv_uint y = 0
  · simp [h0, bv_uint_mk_bv]
  · have hb0 : (B.eval σ).toInt ≠ 0 := fun h => h0 (hz.mpr h)
    simp only [h0, if_false, ne_eq, not_false_eq_true, decide_true, if_true]
    rw [mk_rel, six, siy, ← sdiv_core (A.eval σ) (B.eval σ) hb0]
    simp only [BVExpr.eval_bin, BVBinOp.eval, eval_muxE, neg_eval', eval_sextE,
      Nat.add_sub_cancel, bitOf_xor, bit_extract_last]
    rfl



/-! ### from Fold.lean -/


theorem and_bit {n : Nat} (a b : BitVec n) (i : Nat) :
    (BVBinOp.and.eval a b).getLsbD i = (a.getLsbD i && b.getLsbD i) := by
  simp [BVBinOp.eval]
theorem or_bit {n : Nat} (a b : BitVec n) (i : Nat) :
    (BVBinOp.or.eval a b).getLsbD i = (a.getLsbD i || b.getLsbD i) := by
  simp [BVBinOp.eval]
theorem xor_bit {n : Nat} (a b : BitVec n) (i : Nat) :
    (BVBinOp.xor.eval a b).getLsbD i = (xor (a.getLsbD i) (b.getLsbD i)) := by
  simp [BVBinOp.eval]

theorem mapM_shl_sound {σ : Asg} {w : Nat} {a : SVal} {x : BV} (ha : SRel σ a (ofBV x)) :
    ∀ (ss : List SVal) (bs : List BV) (rs : List SVal),
      SRelL σ ss (bs.map ofBV) → ss.mapM (fun b => symBvShl [widthVal w, a, b]) = some rs →
      SRelL σ rs ((bs.map (bv_shl_step w x)).map ofBV)
  | [], bs, rs, hr, h => by
      cases bs with
      | nil => simp at h; subst h; exact .nil
      | cons _ _ => cases hr
  | s :: ss, bs, rs, hr, h => by
      cases bs with
      | nil => cases hr
      | cons b bs =>
          cases hr with
          | cons h1 h2 =>
              simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff,
                Option.pure_def, Option.some.injEq] at h
              obtain ⟨r, hr1, rs', hrs, rfl⟩ := h
              exact .cons (shl_sound ha h1 hr1) (mapM_shl_sound ha ss bs rs' h2 hrs)

theorem shlOp_sound {σ : Asg} {w : Nat} {a : SVal} {ss : List SVal} {x : BV} {bs : List BV} {r : SVal}
    (ha : SRel σ a (ofBV x)) (hr : SRelL σ ss (bs.map ofBV))
    (h : (do let shifted ← ss.mapM fun b => symBvShl [widthVal w, a, b]
             foldBits .xor w (constVal w 0) shifted) = some r) :
    SRel σ r (ofBV (eval_op .Op_SHL w (x :: bs))) := by
  simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨rs, hrs, hf⟩ := h
  have := foldBits_sound xor_bit rs _ _ r (mk_bv w 0) (mapM_shl_sound ha ss bs rs hr hrs)
    (constVal_sound σ w 0) hf
  simpa [eval_op, bv_shl_step, List.foldl_map] using this

theorem andOp_sound {σ : Asg} {w : Nat} {a : SVal} {ss : List SVal} {x : BV} {bs : List BV} {r : SVal}
    (ha : SRel σ a (ofBV x)) (hr : SRelL σ ss (bs.map ofBV))
    (h : (resize w a >>= fun y => foldBits .and w y ss) = some r) :
    SRel σ r (ofBV (eval_op .Op_And w (x :: bs))) := by
  simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨y, hy, hf⟩ := h
  have := foldBits_sound and_bit ss bs y r (bv_resize w x) hr (resize_sound ha hy) hf
  simpa [eval_op] using this

theorem orOp_sound {σ : Asg} {w : Nat} {ss : List SVal} {bs : List BV} {r : SVal}
    (hr : SRelL σ ss (bs.map ofBV)) (h : foldBits .or w (constVal w 0) ss = some r) :
    SRel σ r (ofBV (eval_op .Op_Or w bs)) := by
  have := foldBits_sound or_bit ss bs _ r (mk_bv w 0) hr (constVal_sound σ w 0) h
  simpa [eval_op] using this

theorem xorOp_sound {σ : Asg} {w : Nat} {ss : List SVal} {bs : List BV} {r : SVal}
    (hr : SRelL σ ss (bs.map ofBV)) (h : foldBits .xor w (constVal w 0) ss = some r) :
    SRel σ r (ofBV (eval_op .Op_Xor w bs)) := by
  have := foldBits_sound xor_bit ss bs _ r (mk_bv w 0) hr (constVal_sound σ w 0) h
  simpa [eval_op] using this



/-! ### from Sext.lean -/


theorem rel_uintS {σ : Asg} {s : SVal} {b : BV} {wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    b.width = wx ∧ bv_uint b = ((X.eval σ).toNat : Int) := by
  obtain ⟨b', hb', hw, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'; exact ⟨hw, hu⟩

/-- The value of a sign extension from bit `n - 1`, for one known amount `n`. -/
def sextVal (w : Nat) {wa : Nat} (A : BVExpr wa) (n : Nat) : BVExpr w :=
  if n = 0 then .const 0#w else sextE w (zextE n A)

def sextGo (w : Nat) {wa wn : Nat} (A : BVExpr wa) (N : BVExpr wn) : Nat → Nat → BVExpr w → BVExpr w
  | _, 0, acc => acc
  | k, c + 1, acc => sextGo w A N (k + 1) c (muxE (SInt.eq (SInt.ofUint N) (SInt.lit k)) (sextVal w A k) acc)

/-- Above the operand width the sign bit is zero, so only amounts `0..wa` are enumerated. -/
def sextB (w : Nat) (a amount : SVal) : Option SVal := do
  let ⟨wa, A⟩ ← asSBV a
  let ⟨_, N⟩ ← asSBV amount
  pure (.bv w (sextGo w A N 0 (wa + 1) (zextE w A)))

theorem sextGo_eval {σ : Asg} {w wa wn : Nat} (A : BVExpr wa) (N : BVExpr wn) :
    ∀ (k c : Nat) (acc : BVExpr w),
      (sextGo w A N k c acc).eval σ =
        if k ≤ (N.eval σ).toNat ∧ (N.eval σ).toNat < k + c
        then (sextVal w A (N.eval σ).toNat).eval σ else acc.eval σ
  | k, 0, acc => by simp [sextGo]; intro; omega
  | k, c + 1, acc => by
      rw [sextGo, sextGo_eval A N (k + 1) c, eval_muxE, SInt.bit_eq, SInt.val_ofUint, SInt.val_lit]
      by_cases hk : (N.eval σ).toNat = k
      · subst hk; simp
      · have hk' : ((N.eval σ).toNat : Int) ≠ (k : Int) := by omega
        simp only [hk', decide_false, Bool.false_eq_true, ite_false]
        by_cases hin : k + 1 ≤ (N.eval σ).toNat ∧ (N.eval σ).toNat < k + 1 + c
        · rw [if_pos hin, if_pos (by omega)]
        · rw [if_neg hin, if_neg (by omega)]

theorem sext_sound {σ : Asg} {w : Nat} {a n r : SVal} {x y : BV}
    (ha : SRel σ a (ofBV x)) (hn : SRel σ n (ofBV y)) (h : sextB w a n = some r) :
    SRel σ r (ofBV (eval_op .Op_Sext w [x, y])) := by
  simp only [sextB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨wa, A⟩, hA, ⟨wn, N⟩, hN, rfl⟩ := h
  obtain ⟨wx, ux⟩ := rel_uintS ha hA
  obtain ⟨_, uy⟩ := rel_uintS hn hN
  refine SRel_ofBV (r := eval_op .Op_Sext w [x, y]) (by simp only [eval_op]; split <;> (try split) <;> (try split) <;> rfl) ?_
  rw [sextGo_eval]
  simp only [eval_op, uy, Int.toNat_natCast, wx, Nat.zero_le, true_and, Nat.zero_add]
  generalize hk : (N.eval σ).toNat = k
  by_cases hk0 : k = 0
  · subst hk0; simp [sextVal, bv_uint_mk_bv]
  · by_cases hkw : k > wa
    · rw [if_neg hk0, if_pos hkw, if_neg (by omega), eval_zextE, ux, mk_rel,
        BitVec.ofInt_natCast, BitVec.ofNat_toNat]
    · rw [if_neg hk0, if_neg hkw, if_pos (show k < wa + 1 by omega)]
      simp only [sextVal, hk0, if_false, eval_sextE, eval_zextE]
      unfold BitVec.signExtend
      rw [BitVec.toInt_eq_toNat_cond, BitVec.toNat_setWidth, ux]
      have hmod : ((A.eval σ).toNat : Int) % 2 ^ k = (((A.eval σ).toNat % 2 ^ k : Nat) : Int) := by
        simp [Int.natCast_emod, Int.natCast_pow]
      rw [hmod]
      generalize hu : (A.eval σ).toNat % 2 ^ k = u
      have hpN : 2 ^ k = 2 * 2 ^ (k - 1) := by
        rw [← Nat.pow_succ']; congr 1; omega
      have c1 : ((2 ^ (k - 1) : Nat) : Int) = (2 : Int) ^ (k - 1) := by simp [Int.natCast_pow]
      have c2 : ((2 ^ k : Nat) : Int) = (2 : Int) ^ k := by simp [Int.natCast_pow]
      by_cases hlt : u < 2 ^ (k - 1)
      · rw [if_pos (show (u : Int) < 2 ^ (k - 1) by rw [← c1]; exact_mod_cast hlt),
          if_pos (show 2 * u < 2 ^ k by omega), mk_rel]
      · rw [if_neg (show ¬ (u : Int) < 2 ^ (k - 1) by rw [← c1]; exact_mod_cast hlt),
          if_neg (show ¬ 2 * u < 2 ^ k by omega), mk_rel, c2]



/-! ### from MemL.lean -/


theorem rel_uintM {σ : Asg} {s : SVal} {b : BV} {wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    b.width = wx ∧ bv_uint b = ((X.eval σ).toNat : Int) := by
  obtain ⟨b', hb', hw, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'; exact ⟨hw, hu⟩

theorem encListG_listVal {α : Type} (e : α → Val) :
    ∀ l : List α, encListG e l = listVal (l.map e)
  | [] => rfl
  | x :: xs => by simp [encListG, listVal, encListG_listVal e xs]

theorem listVal_inj : ∀ {a b : List Val}, listVal a = listVal b → a = b
  | [], [], _ => rfl
  | [], _ :: _, h => by simp [listVal] at h
  | _ :: _, [], h => by simp [listVal] at h
  | x :: xs, y :: ys, h => by
      simp only [listVal, Val.cons.injEq] at h
      rw [h.1, listVal_inj h.2]

/-- A related image: its words and default, as the symbolic parts relate to them. -/
theorem img_rel {σ : Asg} {s : SVal} {m : MemImg} {ws : List SVal} {d : SVal}
    (hr : SRel σ s (MemImg.encMemImg m)) (h : imgParts s = some (ws, d)) :
    SRelL σ ws (m.words.map ofBV) ∧ SRel σ d (ofBV m.dflt) := by
  obtain ⟨vs, vd, he, hws, hd⟩ := imgParts_sound hr h
  simp only [MemImg.encMemImg, encListG_listVal, Val.ctor.injEq, List.cons.injEq, and_true] at he
  obtain ⟨-, hl, hdd⟩ := he
  have e1 := listVal_inj hl
  subst e1; subst hdd
  exact ⟨hws, hd⟩

theorem encMemImg_listVal (m : MemImg) :
    MemImg.encMemImg m = .ctor memImgTag [listVal (m.words.map ofBV), ofBV m.dflt] := by
  simp [MemImg.encMemImg, encListG_listVal, memImgTag, MemImg.tagMemImg]

/-! ## memLoad -/

def loadWords (w n : Nat) {wb we : Nat} (B : BVExpr wb) (E : BVExpr we) (values : List (BVExpr w)) :
    List SVal :=
  values.mapIdx fun k old =>
    .bv w (if k < n then muxE (nzBit E) (.extract (k * w) w B) old else old)

def memLoadB (w n : Nat) (image bus enable : SVal) : Option SVal := do
  let (words, dflt) ← imgParts image
  let ⟨_, B⟩ ← asSBV bus
  let ⟨_, E⟩ ← asSBV enable
  let values ← wordsAt w words
  if n > words.length then none else
    pure (.ctor memImgTag [sListOf (loadWords w n B E values), dflt])

theorem memLoad_sound {σ : Asg} {w n : Nat} {image bus en r : SVal} {m : MemImg} {b0 e0 : BV}
    (hm : SRel σ image (MemImg.encMemImg m)) (hb : SRel σ bus (ofBV b0))
    (he : SRel σ en (ofBV e0)) (h : memLoadB w n image bus en = some r) :
    SRel σ r (MemImg.encMemImg (memLoadImg w n m b0 e0)) := by
  simp only [memLoadB, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨⟨words, dflt⟩, hip, ⟨wb, B⟩, hB, ⟨we, E⟩, hE, values, hv, h⟩ := h
  split at h
  · cases h
  · simp only [Option.pure_def, Option.some.injEq] at h
    subst h
    obtain ⟨hws, hd⟩ := img_rel hm hip
    obtain ⟨_, ub⟩ := rel_uintM hb hB
    obtain ⟨_, ue⟩ := rel_uintM he hE
    obtain ⟨hlen, hall⟩ := wordsAt_sound words values _ hws hv
    rw [List.length_map] at hlen
    rw [encMemImg_listVal]
    have hdf : (memLoadImg w n m b0 e0).dflt = m.dflt := by
      unfold memLoadImg loadImg; split <;> rfl
    rw [hdf]
    refine .ctor (.cons (sListOf_listVal ?_) (.cons hd .nil))
    apply SRelL_of_get
    · simp [loadWords, memLoadImg_length, hlen]
    intro j s v hs hv'
    simp only [loadWords, List.getElem?_mapIdx, Option.map_eq_some_iff] at hs
    obtain ⟨old, hold, rfl⟩ := hs
    have hj : j < m.words.length := by rw [← hlen]; exact (List.getElem?_eq_some_iff.mp hold).1
    obtain ⟨b, hb', hbw, hbu⟩ := hall j old (ofBV (m.words[j]'hj)) hold (by simp [hj])
    rw [asBV_ofBV] at hb'; cases hb'
    have hen := bit_nzBit (e := E) ue
    simp only [List.getElem?_map, Option.map_eq_some_iff] at hv'
    obtain ⟨x, hx, rfl⟩ := hv'
    unfold memLoadImg at hx
    by_cases hz : bv_nonzero e0
    · rw [if_pos hz] at hx
      unfold loadImg at hx
      rw [loadL_get w (by have := SInt.pow_pos' w; omega) n m.words _ 0 j, Nat.zero_add] at hx
      by_cases hjn : j < n
      · rw [if_pos hjn, List.getElem?_eq_getElem hj, Option.map_some, Option.some.injEq] at hx
        subst hx
        refine SRel_ofBV rfl ?_
        rw [if_pos hjn, eval_muxE, hen, if_pos hz, BVExpr.eval_extract, BitVec.extractLsb'_toNat,
          bv_uint_mk_bv, ub, ← Int.pow_mul, Nat.shiftRight_eq_div_pow, two_pow_cast]
        push_cast [Int.natCast_emod, Int.natCast_ediv, Int.natCast_pow]
        rw [Nat.mul_comm]
      · rw [if_neg hjn, List.getElem?_eq_getElem hj, Option.some.injEq] at hx
        subst hx
        exact .bv (asBV_ofBV _) hbw (by rw [if_neg hjn]; exact hbu)
    · rw [if_neg hz, List.getElem?_eq_getElem hj, Option.some.injEq] at hx
      subst hx
      refine .bv (asBV_ofBV _) hbw ?_
      split
      · rw [eval_muxE, hen, if_neg hz]; exact hbu
      · exact hbu


/-! ## memToBus -/

/-- Horner form of `packL`: entry `i` plus `2^dw` times the rest, `f` entries left. -/
def busGo (w dw : Nat) (dflt : SVal) : Nat → List SVal → Option (BVExpr w)
  | 0, _ => some (.const 0#w)
  | f + 1, ws => do
      let ⟨_, X⟩ ← asSBV (ws.headD dflt)
      let r ← busGo w dw dflt f ws.tail
      pure (.bin (zextE w (zextE dw X)) .add (.shiftLeft r (.const (BitVec.ofNat w dw))))

def memToBusB (w n : Nat) (image : SVal) : Option SVal := do
  let (words, dflt) ← imgParts image
  let r ← busGo w (w / n) dflt n words
  pure (.bv w r)

theorem SRelL_headD {σ : Asg} {ss : List SVal} {bs : List BV} {d : SVal} {df : BV}
    (hr : SRelL σ ss (bs.map ofBV)) (hd : SRel σ d (ofBV df)) :
    SRel σ (ss.headD d) (ofBV (bs.headD df)) ∧ SRelL σ ss.tail (bs.tail.map ofBV) := by
  cases ss with
  | nil => cases bs with
    | nil => exact ⟨hd, .nil⟩
    | cons _ _ => cases hr
  | cons s ss => cases bs with
    | nil => cases hr
    | cons b bs => cases hr with
      | cons h1 h2 => exact ⟨h1, h2⟩

theorem busGo_sound {σ : Asg} {w dw n : Nat} {d : SVal} {df : BV} (hdw : dw ≤ w)
    (hd : SRel σ d (ofBV df)) :
    ∀ (f : Nat) (ss : List SVal) (bs : List BV) (r : BVExpr w) (i : Nat),
      SRelL σ ss (bs.map ofBV) → busGo w dw d f ss = some r → i + f = n →
      r.eval σ = BitVec.ofInt w (packL df (2 ^ dw) n bs i)
  | 0, ss, bs, r, i, hr, h, hi => by
      simp only [busGo, Option.some.injEq] at h; subst h
      rw [packL_ge _ _ _ _ _ (by omega)]; simp
  | f + 1, ss, bs, r, i, hr, h, hi => by
      simp only [busGo, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨⟨wx, X⟩, hx, r', hr', rfl⟩ := h
      obtain ⟨hh, ht⟩ := SRelL_headD hr hd
      have ih := busGo_sound (n := n) hdw hd f ss.tail bs.tail r' (i + 1) ht hr' (by omega)
      obtain ⟨_, ux⟩ := rel_uintM hh hx
      rw [packL_step _ _ _ _ _ (by omega)]
      have hsh : (BitVec.ofNat w dw).toNat = dw :=
        by rw [BitVec.toNat_ofNat]; exact Nat.mod_eq_of_lt (Nat.lt_of_le_of_lt hdw (Nat.lt_two_pow_self))
      have hz : (zextE w (zextE dw X)).eval σ = BitVec.ofInt w (bv_uint (bs.headD df) % 2 ^ dw) := by
        have e : ((X.eval σ).toNat : Int) % 2 ^ dw = (((X.eval σ).toNat % 2 ^ dw : Nat) : Int) := by
          simp [Int.natCast_emod, Int.natCast_pow]
        rw [eval_zextE, eval_zextE, ux, e, BitVec.ofInt_natCast, ← BitVec.toNat_setWidth,
          BitVec.ofNat_toNat]
      have hp : BitVec.twoPow w dw = BitVec.ofInt w ((2 : Int) ^ dw) := by
        rw [show (2 : Int) ^ dw = ((2 ^ dw : Nat) : Int) by simp, BitVec.ofInt_natCast]
        apply BitVec.eq_of_toNat_eq
        rw [BitVec.toNat_twoPow, BitVec.toNat_ofNat]
      simp only [BVExpr.eval_bin, BVBinOp.eval, BVExpr.eval_shiftLeft, BVExpr.eval_const,
        BitVec.shiftLeft_eq', hsh, BitVec.shiftLeft_eq_mul_twoPow, hz, ih, hp]
      rw [← BitVec.ofInt_mul, ← BitVec.ofInt_add, Int.mul_comm]

theorem memToBus_sound {σ : Asg} {w n : Nat} {image r : SVal} {m : MemImg}
    (hm : SRel σ image (MemImg.encMemImg m)) (h : memToBusB w n image = some r) :
    SRel σ r (ofBV (memToBusBV w n m)) := by
  simp only [memToBusB, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
    Option.some.injEq] at h
  obtain ⟨⟨words, dflt⟩, hip, r', hr', rfl⟩ := h
  obtain ⟨hws, hd⟩ := img_rel hm hip
  have := busGo_sound (n := n) (Nat.div_le_self w n) hd n words m.words r' 0 hws hr' (by omega)
  refine SRel_ofBV (r := memToBusBV w n m) rfl ?_
  rw [this, memToBusBV, mk_rel]



/-! ### from SetM.lean -/


theorem rel_uintT {σ : Asg} {s : SVal} {b : BV} {wx : Nat} {X : BVExpr wx}
    (hr : SRel σ s (ofBV b)) (hx : asSBV s = some ⟨wx, X⟩) :
    b.width = wx ∧ bv_uint b = ((X.eval σ).toNat : Int) := by
  obtain ⟨b', hb', hw, hu⟩ := asSBV_sound hr hx
  rw [asBV_ofBV] at hb'; cases hb'; exact ⟨hw, hu⟩

/-- `bv_set_bit` inside the width, as a mask update on the bit vector. -/
theorem set_bit_rel {x : BV} {wa d : Nat} {X : BitVec wa} (hw : x.width = wa)
    (hx : bv_uint x = (X.toNat : Int)) (hd : d < wa) (b : Bool) :
    bv_uint (bv_set_bit x d b) =
      (((X &&& ~~~BitVec.twoPow wa d) ||| (if b then BitVec.twoPow wa d else 0#wa)).toNat : Int) := by
  have hbit := testBit_eq_bv_bit hx d
  have hT : (BitVec.twoPow wa d).toNat = 2 ^ d := by
    rw [BitVec.toNat_twoPow]; exact Nat.mod_eq_of_lt (Nat.pow_lt_pow_right (by decide) hd)
  have hTi : ((2 ^ d : Nat) : Int) = (2 : Int) ^ d := by simp [Int.natCast_pow]
  have hW : ((2 ^ wa : Nat) : Int) = (2 : Int) ^ wa := by simp [Int.natCast_pow]
  rw [bv_set_bit_split, hw, bv_uint_mk_bv, hbit, hx]
  cases hc : X.getLsbD d <;> cases b
  · -- clear, clear: unchanged
    have : (X &&& ~~~BitVec.twoPow wa d) ||| 0#wa = X := by
      apply BitVec.eq_of_getLsbD_eq; intro i hi
      by_cases hid : d = i
      · subst hid; simp [hc, BitVec.getLsbD_twoPow]
      · simp [BitVec.getLsbD_twoPow, hid, hi, show ¬ i = d from fun h => hid h.symm]
    simp only [this, Bool.false_eq_true, ite_false]
    rw [Int.emod_eq_of_lt (by omega) (by rw [← hW]; exact_mod_cast X.isLt)]
  · -- set a clear bit: add
    have hand : X &&& BitVec.twoPow wa d = 0#wa := by
      apply BitVec.eq_of_getLsbD_eq; intro i hi
      by_cases hid : d = i
      · subst hid; simp [hc, BitVec.getLsbD_twoPow]
      · simp [BitVec.getLsbD_twoPow, hid, hi, show ¬ i = d from fun h => hid h.symm]
    have hclr : X &&& ~~~BitVec.twoPow wa d = X := by
      apply BitVec.eq_of_getLsbD_eq; intro i hi
      by_cases hid : d = i
      · subst hid; simp [hc, BitVec.getLsbD_twoPow]
      · simp [BitVec.getLsbD_twoPow, hid, hi, show ¬ i = d from fun h => hid h.symm]
    simp only [hclr, if_true, ← BitVec.add_eq_or_of_and_eq_zero _ _ hand, BitVec.toNat_add, hT]
    rw [← hW, ← hTi]; push_cast; rfl
  · -- clear a set bit: subtract
    have hsplit : (X &&& ~~~BitVec.twoPow wa d) + BitVec.twoPow wa d = X := by
      rw [BitVec.add_eq_or_of_and_eq_zero]
      · apply BitVec.eq_of_getLsbD_eq; intro i hi
        by_cases hid : d = i
        · subst hid; simp [hc, BitVec.getLsbD_twoPow, hi]
        · simp [BitVec.getLsbD_twoPow, hid, hi, show ¬ i = d from fun h => hid h.symm]
      · apply BitVec.eq_of_getLsbD_eq; intro i hi
        by_cases hid : d = i
        · subst hid; simp [BitVec.getLsbD_twoPow]
        · simp [BitVec.getLsbD_twoPow, hid, hi, show ¬ i = d from fun h => hid h.symm]
    have hsub : X &&& ~~~BitVec.twoPow wa d = X - BitVec.twoPow wa d :=
      BitVec.eq_sub_iff_add_eq.mpr hsplit
    simp only [Bool.false_eq_true, ite_false, BitVec.or_zero, hsub, BitVec.toNat_sub, hT]
    have hle : 2 ^ d ≤ X.toNat := Nat.ge_two_pow_of_testBit hc
    have h2 : 2 ^ d < 2 ^ wa := Nat.pow_lt_pow_right (by decide) hd
    rw [← hW, ← hTi]
    rw [show 2 ^ wa - 2 ^ d + X.toNat = (X.toNat - 2 ^ d) + 2 ^ wa by omega, Nat.add_mod_right,
      Nat.mod_eq_of_lt (by omega)]
    have hx' := X.isLt
    have hc' : ((X.toNat - 2 ^ d : Nat) : Int) = (X.toNat : Int) - ((2 ^ d : Nat) : Int) := by
      rw [Int.ofNat_sub hle]
    rw [hc', Int.emod_eq_of_lt (by omega) (by omega)]
    simp
  · -- set, set: unchanged
    have : (X &&& ~~~BitVec.twoPow wa d) ||| BitVec.twoPow wa d = X := by
      apply BitVec.eq_of_getLsbD_eq; intro i hi
      by_cases hid : d = i
      · subst hid; simp [hc, BitVec.getLsbD_twoPow, hi]
      · simp [BitVec.getLsbD_twoPow, hid, hi, show ¬ i = d from fun h => hid h.symm]
    simp only [this, if_true]
    rw [Int.emod_eq_of_lt (by omega) (by rw [← hW]; exact_mod_cast X.isLt)]

/-- `bv_set_bit` past the width changes nothing. -/
theorem set_bit_high {x : BV} {d : Nat} (hd : x.width ≤ d) (b : Bool) :
    bv_uint (bv_set_bit x d b) = bv_uint x := by
  have hlt := SetMaskLoop.bv_uint_lt x
  have hnn := SetMaskLoop.bv_uint_nonneg x
  rw [bv_set_bit_split, SetMaskLoop.bv_bit_high x hd, bv_uint_mk_bv]
  cases b
  · simp only [Bool.false_eq_true, ite_false]; exact Int.emod_eq_of_lt hnn hlt
  · simp only [ite_true, Bool.false_eq_true, ite_false]
    rw [SetMaskLoop.pow_split hd, Int.add_mul_emod_self_left, Int.emod_eq_of_lt hnn hlt]

def setStep {wa wv : Nat} (V : BVExpr wv) (acc : BVExpr wa) (p : Nat × Nat) : BVExpr wa :=
  if p.2 < wa then
    .bin (.bin acc .and (.un .not (.const (BitVec.twoPow wa p.2)))) .or
      (muxE (.extract p.1 1 V) (.const (BitVec.twoPow wa p.2)) (.const 0#wa))
  else acc

/-- Bit `K` of `V` for a symbolic UNSIGNED index `K`: bit 0 of `V >>> K`. -/
def bitAtU {wv r : Nat} (V : BVExpr wv) (K : BVExpr r) : BVExpr 1 := .extract 0 1 (.shiftRight V K)

theorem bit_bitAtU (σ : Asg) {wv r : Nat} (V : BVExpr wv) (K : BVExpr r) :
    bitOf ((bitAtU V K).eval σ) = (V.eval σ).getLsbD (K.eval σ).toNat := by
  simp [bitAtU, bitOf, BVExpr.eval_extract, BitVec.getLsbD_extractLsb', BVExpr.eval_shiftRight,
    BitVec.ushiftRight_eq', BitVec.getLsbD_ushiftRight]

/-- One position `i` of the scatter over a SYMBOLIC mask `M` (`SetMaskLoop.smRun`): when the
mask's bit `i` is set, bit `i` of the accumulator (if inside its width) becomes the value's bit at
the counter, and the counter advances. -/
def smStepS {wa wm wv r : Nat} (M : BVExpr wm) (V : BVExpr wv) (p : BVExpr wa × BVExpr r) (i : Nat) :
    BVExpr wa × BVExpr r :=
  let mi : BVExpr 1 := .extract i 1 M
  let set : BVExpr wa :=
    if i < wa then
      .bin (.bin p.1 .and (.un .not (.const (BitVec.twoPow wa i)))) .or
        (muxE (bitAtU V p.2) (.const (BitVec.twoPow wa i)) (.const 0#wa))
    else p.1
  (muxE mi set p.1, .bin p.2 .add (zextE r mi))

/-- Positions `i, …, i+n-1` of the symbolic scatter. -/
def smRunS {wa wm wv r : Nat} (M : BVExpr wm) (V : BVExpr wv) :
    Nat → Nat → BVExpr wa × BVExpr r → BVExpr wa × BVExpr r
  | 0, _, p => p
  | n + 1, i, p => smRunS M V n (i + 1) (smStepS M V p i)

theorem toNat_extract1' {w : Nat} (x : BitVec w) (i : Nat) :
    (x.extractLsb' i 1).toNat = if x.getLsbD i then 1 else 0 := by
  rw [BitVec.extractLsb'_toNat, Nat.shiftRight_eq_div_pow, Nat.pow_one, BitVec.getLsbD,
    Nat.testBit_eq_decide_div_mod_eq]
  rcases Nat.mod_two_eq_zero_or_one (x.toNat / 2 ^ i) with h | h <;> simp [h]

/-- **The symbolic scatter is `smRun`.** -/
theorem smRunS_sound {σ : Asg} {wa wm wv r : Nat} {M : BVExpr wm} {V : BVExpr wv} {m v : BV}
    (hm : bv_uint m = ((M.eval σ).toNat : Int)) (hv : bv_uint v = ((V.eval σ).toNat : Int)) :
    ∀ (n i : Nat) (x : BV) (A : BVExpr wa) (K : BVExpr r) (k : Nat), x.width = wa →
      bv_uint x = ((A.eval σ).toNat : Int) → (K.eval σ).toNat = k → k + n < 2 ^ r →
      (SetMaskLoop.smRun m v n i x k).width = wa ∧
      bv_uint (SetMaskLoop.smRun m v n i x k) = (((smRunS M V n i (A, K)).1.eval σ).toNat : Int)
  | 0, _, x, A, K, k, hw, hx, _, _ => ⟨hw, hx⟩
  | n + 1, i, x, A, K, k, hw, hx, hk, hb => by
      simp only [SetMaskLoop.smRun, smRunS]
      have hmi : bv_bit m i = (M.eval σ).getLsbD i := testBit_eq_bv_bit hm i
      have hvk : bv_bit v k = bitOf ((bitAtU V K).eval σ) := by
        rw [testBit_eq_bv_bit hv, bit_bitAtU, hk]
      have hmiE : bitOf ((BVExpr.extract i 1 M).eval σ) = (M.eval σ).getLsbD i := by
        simp [bitOf, BVExpr.eval_extract, BitVec.getLsbD_extractLsb']
      have hK' : ((BVExpr.bin K .add (zextE r (BVExpr.extract i 1 M))).eval σ).toNat
          = k + (if (M.eval σ).getLsbD i then 1 else 0) := by
        simp only [BVExpr.eval_bin, BVBinOp.eval, BitVec.toNat_add, toNat_zextE, BVExpr.eval_extract,
          toNat_extract1', hk]
        have hr : 1 < 2 ^ r := by omega
        by_cases hmb : (M.eval σ).getLsbD i
        · simp only [hmb, if_true, Nat.mod_eq_of_lt hr]; exact Nat.mod_eq_of_lt (by omega)
        · simp only [hmb, Bool.false_eq_true, if_false, Nat.zero_mod, Nat.add_zero]
          exact Nat.mod_eq_of_lt (by omega)
      rw [hmi]
      by_cases hmb : (M.eval σ).getLsbD i
      · simp only [hmb, if_true]
        apply smRunS_sound hm hv n (i + 1) _ _ _ (k + 1)
        · simp [bv_set_bit, hw, width_mk_bv]
        · simp only [smStepS, eval_muxE, hmiE, hmb, if_true]
          split
          · rename_i hd
            rw [set_bit_rel hw hx hd, hvk]
            simp only [BVExpr.eval_bin, BVExpr.eval_un, BVExpr.eval_const, BVBinOp.eval, BVUnOp.eval,
              eval_muxE]
          · rename_i hd
            rw [set_bit_high (by omega), hx]
        · simp only [smStepS]; rw [hK', hmb]; rfl
        · omega
      · simp only [hmb, Bool.false_eq_true, if_false]
        apply smRunS_sound hm hv n (i + 1) _ _ _ k hw
        · simp only [smStepS, eval_muxE, hmiE, hmb, Bool.false_eq_true, if_false]; exact hx
        · simp only [smStepS]; rw [hK']; simp [hmb]
        · omega

/-- `Op_SetMask`: with a concrete mask, the fold over its set positions (`setStep`); with a
symbolic mask, the positional scatter `smRunS` over the node's width (`set_mask_eq_smRun`). -/
def setMaskB (w : Nat) (a mask value : SVal) : Option SVal := do
  let ⟨wa, A⟩ ← asSBV a
  let ⟨_, V⟩ ← asSBV value
  match ← maskView mask with
  | .inl m =>
      let idxs := mask_indices_bv m
      pure (.bv w (zextE w ((List.zip (List.range idxs.length) idxs).foldl (setStep V) A)))
  | .inr ⟨_, M⟩ =>
      pure (.bv w (zextE w (smRunS M V w 0 (A, (.const 0#(w.log2 + 1) : BVExpr (w.log2 + 1)))).1))

theorem setFold_sound {σ : Asg} {wa wv : Nat} {V : BVExpr wv} {v : BV}
    (hv : bv_uint v = ((V.eval σ).toNat : Int)) :
    ∀ (ps : List (Nat × Nat)) (acc : BVExpr wa) (x : BV), x.width = wa →
      bv_uint x = ((acc.eval σ).toNat : Int) →
      let r := ps.foldl (fun acc p => bv_set_bit acc p.2 (bv_bit v p.1)) x
      r.width = wa ∧ bv_uint r = (((ps.foldl (setStep V) acc).eval σ).toNat : Int)
  | [], acc, x, hw, hx => ⟨hw, hx⟩
  | p :: ps, acc, x, hw, hx => by
      simp only [List.foldl_cons]
      apply setFold_sound hv ps
      · simp [bv_set_bit, hw, width_mk_bv]
      · unfold setStep
        split
        · rename_i hd
          rw [set_bit_rel hw hx hd]
          have hb : bv_bit v p.1 = bitOf ((BVExpr.extract p.1 1 V).eval σ) := by
            rw [testBit_eq_bv_bit hv]
            simp [bitOf, BVExpr.eval_extract, BitVec.getLsbD_extractLsb']
          rw [hb]
          simp only [BVExpr.eval_bin, BVExpr.eval_un, BVExpr.eval_const, BVBinOp.eval, BVUnOp.eval,
            eval_muxE]
        · rename_i hd
          rw [set_bit_high (by omega), hx]

theorem setMask_sound {σ : Asg} {w : Nat} {a mk val r : SVal} {x m v : BV}
    (ha : SRel σ a (ofBV x)) (hm : SRel σ mk (ofBV m)) (hval : SRel σ val (ofBV v))
    (h : setMaskB w a mk val = some r) :
    SRel σ r (ofBV (eval_op .Op_SetMask w [x, m, v])) := by
  simp only [setMaskB, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨⟨wa, A⟩, hA, ⟨wv, V⟩, hV, mv, hmv, h⟩ := h
  obtain ⟨wx, ux⟩ := rel_uintT ha hA
  obtain ⟨_, uv⟩ := rel_uintT hval hV
  cases mv with
  | inl m' =>
    simp only [Option.pure_def, Option.some.injEq] at h; subst h
    have hm' := maskView_inl hm hmv
    rw [asBV_ofBV] at hm'; cases hm'
    obtain ⟨-, hf⟩ := setFold_sound uv (List.zip (List.range (mask_indices_bv m).length)
      (mask_indices_bv m)) A x wx ux
    refine SRel_ofBV (r := eval_op .Op_SetMask w [x, m, v]) rfl ?_
    simp only [eval_op, bv_set_mask, bv_resize, eval_zextE, BitVec.toNat_setWidth, mk_rel] at hf ⊢
    rw [hf, BitVec.ofInt_natCast, BitVec.toNat_ofNat]
  | inr p =>
    obtain ⟨wm, M⟩ := p
    simp only [Option.pure_def, Option.some.injEq] at h; subst h
    obtain ⟨m', hm', hmw, um⟩ := maskView_inr hm hmv
    rw [asBV_ofBV] at hm'; cases hm'
    have hK : ((BVExpr.const 0#(w.log2 + 1) : BVExpr (w.log2 + 1)).eval σ).toNat = 0 := by simp
    obtain ⟨-, hf⟩ := smRunS_sound um uv w 0 x A _ 0 wx ux hK (by simpa using Nat.lt_log2_self)
    refine SRel_ofBV (r := eval_op .Op_SetMask w [x, m, v]) rfl ?_
    simp only [eval_op, SetMaskLoop.set_mask_eq_smRun, bv_resize, eval_zextE, BitVec.toNat_setWidth,
      mk_rel] at hf ⊢
    rw [hf, BitVec.ofInt_natCast, BitVec.toNat_ofNat]

/-! ## The IR dispatch, over the proved builders -/

def evalOp (op : LGraphOp) (w : Nat) (xs : List SVal) : Option SVal :=
  match op, xs with
  | .Op_Const c, [] => some (constVal w c)
  | .Op_Sum n, _ => sumB w n xs
  | .Op_Mult, _ => productB w xs
  | .Op_UDiv, [a, b] => udivB w a b
  | .Op_SDiv, [a, b] => sdivB w a b
  | .Op_And, [] => some (constVal w 0)
  | .Op_And, a :: rest => resize w a >>= fun x => foldBits .and w x rest
  | .Op_Or, _ => foldBits .or w (constVal w 0) xs
  | .Op_Xor, _ => foldBits .xor w (constVal w 0) xs
  | .Op_Ror, _ => redOrB w xs
  | .Op_EQ, _ => eqAllB w xs
  | .Op_Not, [a] => symBvNot [widthVal w, a]
  | .Op_ULT, [a, b] => compare false false w a b
  | .Op_UGT, [a, b] => compare false true w a b
  | .Op_SLT, [a, b] => compare true false w a b
  | .Op_SGT, [a, b] => compare true true w a b
  | .Op_SHL, [] => some (constVal w 0)
  | .Op_SHL, a :: shifts => do
    let shifted ← shifts.mapM fun b => symBvShl [widthVal w, a, b]
    foldBits .xor w (constVal w 0) shifted
  | .Op_SRA, [a, b] => symBvSra [widthVal w, a, b]
  | .Op_Sext, [a, n] => sextB w a n
  | .Op_GetMask, [a, m] => symBvGetMask [widthVal w, a, m]
  | .Op_SetMask, [a, m, v] => setMaskB w a m v
  | .Op_MuxBool, [sel, fv, tv] => select w sel tv fv
  | .Op_MuxN, sel :: vals => muxNB w sel vals
  | .Op_MemRead, [m, a, en] => symMemRead [widthVal w, m, a, en]
  | .Op_MemWrite, [m, a, d, en] => symMemWrite [m, a, d, en]
  | .Op_MemWriteBE bw, [m, a, d, be] => symMemWriteBE [widthVal w, m, a, d, be, widthVal bw]
  | .Op_MemLoad n, [m, bus, en] => memLoadB w n m bus en
  | .Op_MemToBus n, [m] => memToBusB w n m
  | _, _ => none

theorem SRelL_map_inv {σ : Asg} {s : SVal} {ss : List SVal} {bs : List BV}
    (h : SRelL σ (s :: ss) (bs.map ofBV)) :
    ∃ b bs', bs = b :: bs' ∧ SRel σ s (ofBV b) ∧ SRelL σ ss (bs'.map ofBV) := by
  cases bs with
  | nil => cases h
  | cons b bs' => cases h with | cons h1 h2 => exact ⟨b, bs', rfl, h1, h2⟩

theorem SRelL_map_nil {σ : Asg} {bs : List BV} (h : SRelL σ [] (bs.map ofBV)) : bs = [] := by
  cases bs with
  | nil => rfl
  | cons _ _ => cases h

/-- **The IR's scalar dispatch is sound** against the shared `eval_op`, for every operator
the route admits (`OpSupported`), at every operand count (a shape the pinned `eval_op` does
not match is refused, not answered). -/
theorem evalOp_sound {σ : Asg} {op : LGraphOp} {w : Nat} {ss : List SVal} {bs : List BV}
    {r : SVal} (hsup : OpSupported op = true) (hr : SRelL σ ss (bs.map ofBV))
    (h : evalOp op w ss = some r) : SRel σ r (ofBV (eval_op op w bs)) := by
  cases op <;> simp only [OpSupported, Bool.false_eq_true] at hsup
  case Op_Sum n => exact sum_sound hr h
  case Op_Mult => exact product_sound hr h
  case Op_Ror => exact redOr_sound hr h
  case Op_EQ => exact eqAll_sound hr h
  case Op_Or => exact orOp_sound hr h
  case Op_Xor => exact xorOp_sound hr h
  case Op_UDiv =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      exact udiv_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_SDiv =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      exact sdiv_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_ULT =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      simpa [eval_op] using compare_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_UGT =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      simpa [eval_op] using compare_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_SLT =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      simpa [eval_op] using compare_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_SGT =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      simpa [eval_op] using compare_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_SRA =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      simpa [eval_op] using sra_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_GetMask =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      simpa [eval_op] using getMask_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_Sext =>
    match ss, h with
    | [a, b], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      rw [SRelL_map_nil hr1]
      exact sext_sound ha hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_Not =>
    match ss, h with
    | [a], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      rw [SRelL_map_nil hr0]
      simpa [eval_op] using not_sound ha h
    | [], h => simp [evalOp] at h
    | _ :: _ :: _, h => simp [evalOp] at h
  case Op_SetMask =>
    match ss, h with
    | [a, b, c], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      obtain ⟨b2, bs2, rfl, hc, hr2⟩ := SRelL_map_inv hr1
      rw [SRelL_map_nil hr2]
      exact setMask_sound ha hb hc h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | [_, _], h => simp [evalOp] at h
    | _ :: _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_MuxBool =>
    match ss, h with
    | [a, b, c], h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      obtain ⟨b1, bs1, rfl, hb, hr1⟩ := SRelL_map_inv hr0
      obtain ⟨b2, bs2, rfl, hc, hr2⟩ := SRelL_map_inv hr1
      rw [SRelL_map_nil hr2]
      simpa [eval_op] using select_sound ha hc hb h
    | [], h => simp [evalOp] at h
    | [_], h => simp [evalOp] at h
    | [_, _], h => simp [evalOp] at h
    | _ :: _ :: _ :: _ :: _, h => simp [evalOp] at h
  case Op_And =>
    match ss, h with
    | [], h =>
      simp only [evalOp, Option.some.injEq] at h; subst h
      rw [SRelL_map_nil hr]; exact .conc
    | a :: rest, h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      exact andOp_sound ha hr0 h
  case Op_SHL =>
    match ss, h with
    | [], h =>
      simp only [evalOp, Option.some.injEq] at h; subst h
      rw [SRelL_map_nil hr]; exact .conc
    | a :: rest, h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      exact shlOp_sound ha hr0 h
  case Op_MuxN =>
    match ss, h with
    | [], h => simp [evalOp] at h
    | a :: rest, h =>
      obtain ⟨b0, bs0, rfl, ha, hr0⟩ := SRelL_map_inv hr
      exact muxN_sound ha hr0 h

#audit_axioms evalOp_sound
#audit_axioms memLoad_sound
#audit_axioms memToBus_sound

end HDLProof.Sym.Native

