/-
`Sym.Prim` -- the symbolic semantics of each object-language primitive (M3 S1).

`symPrim p ss` answers a symbolic value for `p` applied to `ss`, or refuses.
Fully concrete operands run `evalPrim` itself. Otherwise each primitive has a
helper and a soundness lemma: if the concrete operands are related to `ss`
and `evalPrim` succeeds on them, the symbolic answer is related to the
concrete one (`symPrim_sound`).

Refused (an `error`, never a guess): `mulI`, `divI`, `modI` with a symbolic
operand, `eqV` on anything symbolic, `bvBit`/`bvGetMask` with a symbolic
position or mask, and the memory primitives (a later increment).
-/
import LeanSemanticPrimitives.Projection.Sym.Val

namespace Projection.Sym
open Std.Tactic.BVDecide

/-! ## Inversion helpers -/

theorem SRelL_nil {σ : Asg} {vs : List Val} (h : SRelL σ [] vs) : vs = [] := by
  cases h; rfl

theorem SRelL_one {σ : Asg} {a : SVal} {vs : List Val} (h : SRelL σ [a] vs) :
    ∃ va, vs = [va] ∧ SRel σ a va := by
  cases h with
  | cons ha hs => cases hs; exact ⟨_, rfl, ha⟩

theorem SRelL_two {σ : Asg} {a b : SVal} {vs : List Val} (h : SRelL σ [a, b] vs) :
    ∃ va vb, vs = [va, vb] ∧ SRel σ a va ∧ SRel σ b vb := by
  cases h with
  | cons ha hs => cases hs with
    | cons hb hs' => cases hs'; exact ⟨_, _, rfl, ha, hb⟩

theorem SRelL_three {σ : Asg} {a b c : SVal} {vs : List Val} (h : SRelL σ [a, b, c] vs) :
    ∃ va vb vc, vs = [va, vb, vc] ∧ SRel σ a va ∧ SRel σ b vb ∧ SRel σ c vc := by
  cases h with
  | cons ha hs => cases hs with
    | cons hb hs' => cases hs' with
      | cons hc hs'' => cases hs''; exact ⟨_, _, _, rfl, ha, hb, hc⟩

theorem SRel_conc_inv {σ : Asg} {x : Val} {v : Val} (h : SRel σ (.conc x) v) : v = x := by
  cases h; rfl

/-- All operands concrete. -/
def allConc : List SVal → Option (List Val)
  | [] => some []
  | .conc v :: ss => (allConc ss).map (v :: ·)
  | _ => none

theorem allConc_sound {σ : Asg} :
    ∀ {ss : List SVal} {xs vs : List Val}, allConc ss = some xs → SRelL σ ss vs → vs = xs
  | [], xs, vs, h, hr => by simp [allConc] at h; subst h; exact SRelL_nil hr
  | .conc x :: ss, xs, vs, h, hr => by
      simp only [allConc, Option.map_eq_some_iff] at h
      obtain ⟨ys, hys, rfl⟩ := h
      cases hr with
      | cons ha hs => rw [SRel_conc_inv ha, allConc_sound hys hs]
  | .int _ :: _, _, _, h, _ => by simp [allConc] at h
  | .bool _ :: _, _, _, h, _ => by simp [allConc] at h
  | .bv _ _ :: _, _, _, h, _ => by simp [allConc] at h
  | .cons _ _ :: _, _, _, h, _ => by simp [allConc] at h
  | .ctor _ _ :: _, _, _, h, _ => by simp [allConc] at h

/-! ## Integers and Booleans -/

def symIntBin (f : SInt → SInt → SInt) : List SVal → Option SVal
  | [a, b] => do let i ← asSInt a; let j ← asSInt b; pure (.int (f i j))
  | _ => none

def symIntCmp (f : SInt → SInt → BVExpr 1) : List SVal → Option SVal
  | [a, b] => do let i ← asSInt a; let j ← asSInt b; pure (.bool (f i j))
  | _ => none

def symBoolBin (f : BVExpr 1 → BVExpr 1 → BVExpr 1) : List SVal → Option SVal
  | [a, b] => do let i ← asSBool a; let j ← asSBool b; pure (.bool (f i j))
  | _ => none

theorem symIntBin_sound {σ : Asg} {f : SInt → SInt → SInt} {g : Int → Int → Int} {p : Prim}
    (hf : ∀ i j, (f i j).val σ = g (i.val σ) (j.val σ))
    (hp : ∀ a b, evalPrim p [.int a, .int b] = .ok (.int (g a b)))
    {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symIntBin f ss = some s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [a, b], h =>
    simp only [symIntBin, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨i, hi, j, hj, rfl⟩ := h
    obtain ⟨va, vb, rfl, ra, rb⟩ := SRelL_two hr
    obtain ⟨za, rfl, hza⟩ := asSInt_sound ra hi
    obtain ⟨zb, rfl, hzb⟩ := asSInt_sound rb hj
    rw [hp] at he; cases he
    exact .int (by rw [hf, hza, hzb])

theorem symIntCmp_sound {σ : Asg} {f : SInt → SInt → BVExpr 1} {g : Int → Int → Bool} {p : Prim}
    (hf : ∀ i j, bitOf ((f i j).eval σ) = g (i.val σ) (j.val σ))
    (hp : ∀ a b, evalPrim p [.int a, .int b] = .ok (.bool (g a b)))
    {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symIntCmp f ss = some s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [a, b], h =>
    simp only [symIntCmp, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨i, hi, j, hj, rfl⟩ := h
    obtain ⟨va, vb, rfl, ra, rb⟩ := SRelL_two hr
    obtain ⟨za, rfl, hza⟩ := asSInt_sound ra hi
    obtain ⟨zb, rfl, hzb⟩ := asSInt_sound rb hj
    rw [hp] at he; cases he
    exact .bool (by rw [hf, hza, hzb])

theorem symBoolBin_sound {σ : Asg} {f : BVExpr 1 → BVExpr 1 → BVExpr 1} {g : Bool → Bool → Bool}
    {p : Prim}
    (hf : ∀ i j, bitOf ((f i j).eval σ) = g (bitOf (i.eval σ)) (bitOf (j.eval σ)))
    (hp : ∀ a b, evalPrim p [.bool a, .bool b] = .ok (.bool (g a b)))
    {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBoolBin f ss = some s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [a, b], h =>
    simp only [symBoolBin, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨i, hi, j, hj, rfl⟩ := h
    obtain ⟨va, vb, rfl, ra, rb⟩ := SRelL_two hr
    obtain ⟨za, rfl, hza⟩ := asSBool_sound ra hi
    obtain ⟨zb, rfl, hzb⟩ := asSBool_sound rb hj
    rw [hp] at he; cases he
    exact .bool (by rw [hf, hza, hzb])

def symNotB : List SVal → Option SVal
  | [a] => (asSBool a).map (fun i => .bool (notE i))
  | _ => none

theorem symNotB_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symNotB ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .notB vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [a], h =>
    simp only [symNotB, Option.map_eq_some_iff] at h
    obtain ⟨i, hi, rfl⟩ := h
    obtain ⟨va, rfl, ra⟩ := SRelL_one hr
    obtain ⟨za, rfl, hza⟩ := asSBool_sound ra hi
    simp [evalPrim] at he; subst he
    exact .bool (by rw [bit_notE, hza])

/-! ## Structure: lists and constructors with a concrete spine -/

/-- Neither `nil` nor a list cell's tail can be told from a symbolic LEAF: an
`int`/`bool`/`bv` never relates to `nil`. -/
theorem SRel_leaf_not_nil {σ : Asg} {s : SVal} {v : Val} (hr : SRel σ s v)
    (h : ∀ x, s ≠ .conc x) : v ≠ .nil := by
  cases hr with
  | conc => exact absurd rfl (h _)
  | int _ => intro h'; cases h'
  | bool _ => intro h'; cases h'
  | bv hb _ _ => intro h'; subst h'; simp [asBV] at hb
  | cons _ _ => intro h'; cases h'
  | ctor _ => intro h'; cases h'

def symIsNil : List SVal → Option SVal
  | [.conc _] => none
  | [_] => some (.conc (.bool false))
  | _ => none

theorem symIsNil_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symIsNil ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .isNil vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [a], h =>
    obtain ⟨va, rfl, ra⟩ := SRelL_one hr
    cases a with
    | conc x => simp [symIsNil] at h
    | _ =>
      simp only [symIsNil, Option.some.injEq] at h; subst h
      have hne := SRel_leaf_not_nil ra (by intro x hx; cases hx)
      simp only [evalPrim, Except.ok.injEq] at he
      subst he
      cases va <;> first | exact absurd rfl hne | exact .conc
  | [], h => simp [symIsNil] at h
  | _ :: _ :: _, h => simp [symIsNil] at h

def symHd : List SVal → Option SVal
  | [.cons h _] => some h
  | _ => none

def symTl : List SVal → Option SVal
  | [.cons _ t] => some t
  | _ => none

theorem symHd_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symHd ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .hd vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [.cons a b], h =>
    simp only [symHd, Option.some.injEq] at h; subst h
    obtain ⟨va, rfl, ra⟩ := SRelL_one hr
    cases ra with
    | cons r1 _ => simp [evalPrim] at he; subst he; exact r1

theorem symTl_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symTl ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .tl vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [.cons a b], h =>
    simp only [symTl, Option.some.injEq] at h; subst h
    obtain ⟨va, rfl, ra⟩ := SRelL_one hr
    cases ra with
    | cons _ r2 => simp [evalPrim] at he; subst he; exact r2

def symConsP : List SVal → Option SVal
  | [a, d] => some (.cons a d)
  | _ => none

theorem symConsP_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symConsP ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .consP vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [a, d], h =>
    simp only [symConsP, Option.some.injEq] at h; subst h
    obtain ⟨va, vd, rfl, ra, rd⟩ := SRelL_two hr
    simp [evalPrim] at he; subst he
    exact .cons ra rd

/-! ## Bit vectors -/

def cIntOf : SVal → Option Int
  | .conc (.int z) => some z
  | _ => none

theorem cIntOf_sound {σ : Asg} {s : SVal} {v : Val} {z : Int} (hr : SRel σ s v)
    (h : cIntOf s = some z) : v = .int z := by
  cases s with
  | conc x => cases x <;> simp [cIntOf] at h; subst h; exact SRel_conc_inv hr
  | _ => simp [cIntOf] at h

theorem width_mk_bv (w : Nat) (z : Int) : (mk_bv w z).width = w := rfl

/-- A relation for a result `ofBV r`, from its normalized value. -/
theorem SRel_ofBV {σ : Asg} {w : Nat} {e : BVExpr w} {r : BV} (hw : r.width = w)
    (hu : bv_uint r = ((e.eval σ).toNat : Int)) : SRel σ (.bv w e) (ofBV r) :=
  .bv (asBV_ofBV r) hw hu

def symBvMk : List SVal → Option SVal
  | [w, x] => do
      let wz ← cIntOf w
      let i ← asSInt x
      pure (.bv (widthOf wz) (i.toBits (widthOf wz)))
  | _ => none

theorem symBvMk_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvMk ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvMk vs = .ok v) : SRel σ s v := by
  match ss, h with
  | [w, x], h =>
    simp only [symBvMk, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, i, hi, rfl⟩ := h
    obtain ⟨vw, vx, rfl, rw', rx⟩ := SRelL_two hr
    rw [cIntOf_sound rw' hw] at he
    obtain ⟨z, rfl, hz⟩ := asSInt_sound rx hi
    simp only [evalPrim, Except.ok.injEq] at he; subst he
    exact SRel_ofBV (width_mk_bv _ _) (by rw [mk_rel, SInt.toBits_eq, hz])

/-- An operand that must be a bit vector: the width and the expression. -/
def bvArg (s : SVal) : Option (Σ w, BVExpr w) := asSBV s

def symBvWidth : List SVal → Option SVal
  | [x] => (asSBV x).map (fun p => .conc (.int (Int.ofNat p.1)))
  | _ => none

theorem symBvWidth_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvWidth ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvWidth vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [x], h =>
    simp only [symBvWidth, Option.map_eq_some_iff] at h
    obtain ⟨⟨w, e⟩, hx, rfl⟩ := h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    obtain ⟨b, hb, hbw, _⟩ := asSBV_sound rx hx
    simp only [evalPrim, hb, Except.ok.injEq] at he; subst he
    rw [hbw]; exact .conc

/-- A bit vector read as its unsigned integer: CONCRETE when the vector is a
literal (so a constant state bit decides the comparisons and branches it
feeds while the residual is evaluated), symbolic otherwise. -/
def uintOf {w : Nat} (e : BVExpr w) : SVal :=
  match e with
  | .const c => .conc (.int (c.toNat : Int))
  | e => .int (SInt.ofUint e)

theorem uintOf_rel {σ : Asg} {w : Nat} {e : BVExpr w} {b : BV} (hu : bv_uint b = ((e.eval σ).toNat : Int)) :
    SRel σ (uintOf e) (.int (bv_uint b)) := by
  cases e with
  | const c => simp only [uintOf]; rw [hu, BVExpr.eval_const]; exact .conc
  | _ => exact .int (by rw [SInt.val_ofUint, hu])

/-- The signed reading, concrete on a literal. -/
def sintOf {w : Nat} (e : BVExpr w) : SVal :=
  match e with
  | .const c => .conc (.int c.toInt)
  | e => .int (SInt.ofSint e)

theorem sintOf_rel {σ : Asg} {w : Nat} {e : BVExpr w} {b : BV} (hw : b.width = w)
    (hu : bv_uint b = ((e.eval σ).toNat : Int)) : SRel σ (sintOf e) (.int (bv_sint b)) := by
  cases e with
  | const c =>
      simp only [sintOf]
      have := sint_rel hw hu
      rw [BVExpr.eval_const] at this
      rw [this]; exact .conc
  | _ => exact .int (by rw [SInt.val_ofSint, sint_rel hw hu])

/-- Bit `i` of a vector, concrete on a literal. -/
def bitOfE {w : Nat} (e : BVExpr w) (i : Nat) : SVal :=
  match e with
  | .const c => .conc (.bool (c.getLsbD i))
  | e => .bool (.extract i 1 e)

theorem bitOfE_rel {σ : Asg} {w : Nat} (e : BVExpr w) (i : Nat) (hi : i < w) :
    SRel σ (bitOfE e i) (.bool ((e.eval σ).getLsbD i)) := by
  cases e with
  | const c => simp only [bitOfE, BVExpr.eval_const]; exact .conc
  | _ => exact .bool (by simp [bitOf, hi])

def symBvUint : List SVal → Option SVal
  | [x] => (asSBV x).map (fun p => uintOf p.2)
  | _ => none

theorem symBvUint_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvUint ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvUint vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [x], h =>
    simp only [symBvUint, Option.map_eq_some_iff] at h
    obtain ⟨⟨w, e⟩, hx, rfl⟩ := h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    obtain ⟨b, hb, _, hu⟩ := asSBV_sound rx hx
    simp only [evalPrim, hb, Except.ok.injEq] at he; subst he
    exact uintOf_rel hu

def symBvSint : List SVal → Option SVal
  | [x] => (asSBV x).map (fun p => sintOf p.2)
  | _ => none

theorem symBvSint_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvSint ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvSint vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [x], h =>
    simp only [symBvSint, Option.map_eq_some_iff] at h
    obtain ⟨⟨w, e⟩, hx, rfl⟩ := h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    obtain ⟨b, hb, hw, hu⟩ := asSBV_sound rx hx
    simp only [evalPrim, hb, Except.ok.injEq] at he; subst he
    exact sintOf_rel hw hu

def symBvBit : List SVal → Option SVal
  | [x, i] => do
      let ⟨w, e⟩ ← asSBV x
      let iz ← cIntOf i
      pure (if 0 ≤ iz ∧ iz.toNat < w then bitOfE e iz.toNat else .conc (.bool false))
  | _ => none

theorem symBvBit_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvBit ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvBit vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [x, i], h =>
    simp only [symBvBit, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨⟨w, e⟩, hx, iz, hi, rfl⟩ := h
    obtain ⟨vx, vi, rfl, rx, ri⟩ := SRelL_two hr
    obtain ⟨b, hb, _, hu⟩ := asSBV_sound rx hx
    rw [cIntOf_sound ri hi] at he
    simp only [evalPrim, hb, Except.ok.injEq] at he; subst he
    rw [testBit_eq_bv_bit hu]
    split
    · rename_i hc
      have := bitOfE_rel (σ := σ) e iz.toNat hc.2
      simpa [hc.1] using this
    · rename_i hc
      have : (decide (0 ≤ iz) && (e.eval σ).getLsbD iz.toNat) = false := by
        by_cases h0 : 0 ≤ iz
        · simp only [h0, decide_true, Bool.true_and]
          have hge : w ≤ iz.toNat := by
            by_cases hlt : iz.toNat < w
            · exact absurd ⟨h0, hlt⟩ hc
            · omega
          exact BitVec.getLsbD_of_ge (e.eval σ) iz.toNat hge
        · simp [h0]
      rw [this]; exact .conc

def symBvBitwise (op : BVBinOp) : List SVal → Option SVal
  | [w, a, b] => do
      let wz ← cIntOf w
      let ⟨_, A⟩ ← asSBV a
      let ⟨_, B⟩ ← asSBV b
      let W := widthOf wz
      pure (.bv W (.bin (zextE W A) op (zextE W B)))
  | _ => none

theorem symBvBitwise_sound {σ : Asg} {op : BVBinOp} {f : Bool → Bool → Bool} {p : Prim}
    (hop : ∀ {n : Nat} (x y : BitVec n) (i : Nat), (op.eval x y).getLsbD i = f (x.getLsbD i) (y.getLsbD i))
    (hp : ∀ w va vb x y, asBV va = some x → asBV vb = some y →
       evalPrim p [.int w, va, vb] = .ok (ofBV (bv_bitwise (widthOf w) f x y)))
    {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvBitwise op ss = some s) (hr : SRelL σ ss vs) (he : evalPrim p vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [w, a, b], h =>
    simp only [symBvBitwise, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, ⟨wa, A⟩, ha, ⟨wb, B⟩, hb, rfl⟩ := h
    obtain ⟨vw, va, vb, rfl, rw', ra, rb⟩ := SRelL_three hr
    obtain ⟨xa, hxa, _, hua⟩ := asSBV_sound ra ha
    obtain ⟨xb, hxb, _, hub⟩ := asSBV_sound rb hb
    rw [cIntOf_sound rw' hw, hp _ _ _ _ _ hxa hxb] at he
    simp only [Except.ok.injEq] at he; subst he
    refine SRel_ofBV rfl (bitwise_rel hua hub _ f _ ?_)
    intro i hi
    simp only [BVExpr.eval_bin, hop, getLsbD_zextE, hi, decide_true, Bool.true_and]

def symBvNot : List SVal → Option SVal
  | [w, a] => do
      let wz ← cIntOf w
      let ⟨_, A⟩ ← asSBV a
      let W := widthOf wz
      pure (.bv W (.un .not (zextE W A)))
  | _ => none

theorem symBvNot_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvNot ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvNot vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [w, a], h =>
    simp only [symBvNot, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, ⟨wa, A⟩, ha, rfl⟩ := h
    obtain ⟨vw, va, rfl, rw', ra⟩ := SRelL_two hr
    rw [cIntOf_sound rw' hw] at he
    obtain ⟨xa, hxa, _, hua⟩ := asSBV_sound ra ha
    simp only [evalPrim, hxa, Except.ok.injEq] at he; subst he
    refine SRel_ofBV rfl (not_rel hua _ _ ?_)
    intro i hi
    simp only [BVExpr.eval_un, BVUnOp.eval, BitVec.getLsbD_not, getLsbD_zextE, hi, decide_true,
      Bool.true_and]

def symBvResize : List SVal → Option SVal
  | [w, a] => do
      let wz ← cIntOf w
      let ⟨_, A⟩ ← asSBV a
      let W := widthOf wz
      pure (.bv W (zextE W A))
  | _ => none

theorem symBvResize_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvResize ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvResize vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [w, a], h =>
    simp only [symBvResize, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, ⟨wa, A⟩, ha, rfl⟩ := h
    obtain ⟨vw, va, rfl, rw', ra⟩ := SRelL_two hr
    rw [cIntOf_sound rw' hw] at he
    obtain ⟨xa, hxa, _, hua⟩ := asSBV_sound ra ha
    simp only [evalPrim, hxa, Except.ok.injEq] at he; subst he
    exact SRel_ofBV rfl (by rw [resize_rel hua, eval_zextE])

def symBvSra : List SVal → Option SVal
  | [w, x, sh] => do
      let wz ← cIntOf w
      let ⟨_, X⟩ ← asSBV x
      let ⟨_, S⟩ ← asSBV sh
      let W := widthOf wz
      pure (.bv W (sextE W (.arithShiftRight X S)))
  | _ => none

theorem symBvSra_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvSra ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvSra vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [w, x, sh], h =>
    simp only [symBvSra, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, ⟨wx, X⟩, hx, ⟨ws, S⟩, hs, rfl⟩ := h
    obtain ⟨vw, vx, vs', rfl, rw', rx, rs⟩ := SRelL_three hr
    rw [cIntOf_sound rw' hw] at he
    obtain ⟨bx, hbx, hwx, hux⟩ := asSBV_sound rx hx
    obtain ⟨bs, hbs, _, hus⟩ := asSBV_sound rs hs
    simp only [evalPrim, hbx, hbs, Except.ok.injEq] at he; subst he
    refine SRel_ofBV (by simp [bv_sra]; split <;> rfl) ?_
    rw [sra_rel hwx hux hus, eval_sextE, BVExpr.eval_arithShiftRight]
    rfl

def symBvShl : List SVal → Option SVal
  | [w, a, sh] => do
      let wz ← cIntOf w
      let ⟨_, A⟩ ← asSBV a
      let ⟨_, S⟩ ← asSBV sh
      let W := widthOf wz
      pure (.bv W (.shiftLeft (zextE W A) S))
  | _ => none

theorem symBvShl_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvShl ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvShl vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [w, a, sh], h =>
    simp only [symBvShl, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, ⟨wa, A⟩, ha, ⟨ws, S⟩, hs, rfl⟩ := h
    obtain ⟨vw, va, vs', rfl, rw', ra, rs⟩ := SRelL_three hr
    rw [cIntOf_sound rw' hw] at he
    obtain ⟨xa, hxa, _, hua⟩ := asSBV_sound ra ha
    obtain ⟨xs, hxs, _, hus⟩ := asSBV_sound rs hs
    simp only [evalPrim, hxa, hxs, Except.ok.injEq] at he; subst he
    refine SRel_ofBV rfl ?_
    rw [shl_rel hua hus, BVExpr.eval_shiftLeft, eval_zextE]
    rfl

/-- The bits of `e` at positions `idx`, LOWEST first: bit `j` of the result is
`e`'s bit `idx[j]`. -/
def catBits {w : Nat} (e : BVExpr w) : (idx : List Nat) → BVExpr idx.length
  | [] => .const 0#0
  | i :: rest => .append (catBits e rest) (.extract i 1 e) rfl

theorem getLsbD_catBits (σ : Asg) {w : Nat} (e : BVExpr w) :
    ∀ (idx : List Nat) (j : Nat), j < idx.length →
      ((catBits e idx).eval σ).getLsbD j = (e.eval σ).getLsbD (idx.getD j 0)
  | [], j, hj => by simp at hj
  | i :: rest, j, hj => by
      simp only [catBits, BVExpr.eval_append, BitVec.getLsbD_append, BVExpr.eval_extract,
        BitVec.getLsbD_extractLsb']
      cases j with
      | zero => simp
      | succ j =>
        simp only [show ¬ (j + 1 < 1) by omega, if_false, Nat.add_sub_cancel, List.getD_cons_succ]
        exact getLsbD_catBits σ e rest j (by simp at hj; omega)

def symBvGetMask : List SVal → Option SVal
  | [w, x, .conc m] => do
      let wz ← cIntOf w
      let ⟨_, X⟩ ← asSBV x
      let mb ← asBV m
      let W := widthOf wz
      pure (.bv W (zextE W (catBits X (mask_indices_bv mb))))
  | _ => none

theorem symBvGetMask_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symBvGetMask ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .bvGetMask vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [w, x, .conc m], h =>
    simp only [symBvGetMask, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨wz, hw, ⟨wx, X⟩, hx, mb, hm, rfl⟩ := h
    obtain ⟨vw, vx, vm, rfl, rw', rx, rm⟩ := SRelL_three hr
    rw [cIntOf_sound rw' hw, SRel_conc_inv rm] at he
    obtain ⟨bx, hbx, _, hux⟩ := asSBV_sound rx hx
    simp only [evalPrim, hbx, hm, Except.ok.injEq] at he; subst he
    refine SRel_ofBV rfl ?_
    rw [getmask_rel hux _ mb ((catBits X (mask_indices_bv mb)).eval σ) rfl
          (fun j hj => getLsbD_catBits σ X _ j hj), eval_zextE]

end Projection.Sym
