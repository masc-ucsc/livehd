/-
`Sym.Val` -- symbolic object-language values (M3 S1).

A residual runs on the object language's `Val`s. Its symbolic run keeps the
STRUCTURE concrete -- list spines, constructor tags, everything the
specialization left static -- and makes only the hardware data symbolic:

  * `int`  an exact integer, `SInt` (two's complement, never wraps);
  * `bool` a one-bit `BVExpr`;
  * `bv`   a bit vector of a STATIC width `w`, as a `BVExpr w` of its
           normalized value -- a concrete `BV` relates when its `bv_uint` is
           that value, so an unnormalized raw field never matters;
  * `conc` a fully concrete value, related to itself only.

`SRel σ s v` says the symbolic `s` denotes the concrete `v` under the
assignment `σ`. `merge` builds `c ? a : b`; `merge_sound_t`/`_f` say it
denotes whatever the TAKEN branch denotes -- the untaken branch need not
relate to anything.
-/
import LeanSemanticPrimitives.Projection.Sym.Kit
import LeanSemanticPrimitives.Projection.Sym.Bridge
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics

namespace Projection.Sym
open Std.Tactic.BVDecide

inductive SVal where
  | conc : Val → SVal
  | int  : SInt → SVal
  | bool : BVExpr 1 → SVal
  | bv   : (w : Nat) → BVExpr w → SVal
  | cons : SVal → SVal → SVal
  | ctor : Nat → List SVal → SVal

mutual

inductive SRel (σ : Asg) : SVal → Val → Prop where
  | conc : SRel σ (.conc v) v
  | int  : i.val σ = z → SRel σ (.int i) (.int z)
  | bool : bitOf (e.eval σ) = b → SRel σ (.bool e) (.bool b)
  | bv   : asBV v = some b → b.width = w → bv_uint b = ((e.eval σ).toNat : Int) →
           SRel σ (.bv w e) v
  | cons : SRel σ h h' → SRel σ t t' → SRel σ (.cons h t) (.cons h' t')
  | ctor : SRelL σ fs vs → SRel σ (.ctor k fs) (.ctor k vs)

inductive SRelL (σ : Asg) : List SVal → List Val → Prop where
  | nil  : SRelL σ [] []
  | cons : SRel σ s v → SRelL σ ss vs → SRelL σ (s :: ss) (v :: vs)

end

/-! ## Lifting a concrete value to a symbolic shape -/

/-- A concrete BV as a constant `BVExpr` of its width. -/
def bvConst (b : BV) : BVExpr b.width := .const (BitVec.ofNat b.width (bv_uint b).toNat)

theorem toNat_bvConst (σ : Asg) (b : BV) : (((bvConst b).eval σ).toNat : Int) = bv_uint b := by
  simp only [bvConst, BVExpr.eval_const, BitVec.toNat_ofNat]
  have h0 := bv_uint_nonneg' b
  have h1 := bv_uint_lt' b
  rw [two_pow_cast] at h1
  rw [Nat.mod_eq_of_lt (by omega)]
  omega

theorem SRel_bvConst (σ : Asg) {v : Val} {b : BV} (h : asBV v = some b) :
    SRel σ (.bv b.width (bvConst b)) v :=
  .bv h rfl (toNat_bvConst σ b).symm

/-! ## Merging two branches -/

/-- Cast a `BVExpr` along a width equality. -/
def castE {w w' : Nat} (h : w = w') (e : BVExpr w) : BVExpr w' := h ▸ e

theorem eval_castE (σ : Asg) {w w' : Nat} (h : w = w') (e : BVExpr w) :
    ((castE h e).eval σ).toNat = (e.eval σ).toNat := by
  subst h; rfl

/-- The view of an `SVal` as an exact integer, when it is one. -/
def asSInt : SVal → Option SInt
  | .int i          => some i
  | .conc (.int z)  => some (SInt.lit z)
  | _               => none

/-- The view of an `SVal` as a one-bit Boolean, when it is one. -/
def asSBool : SVal → Option (BVExpr 1)
  | .bool e          => some e
  | .conc (.bool b)  => some (boolE b)
  | _                => none

/-- The view of an `SVal` as a bit vector of SOME width. -/
def asSBV : SVal → Option (Σ w, BVExpr w)
  | .bv w e  => some ⟨w, e⟩
  | .conc v  => (asBV v).map (fun b => ⟨b.width, bvConst b⟩)
  | _        => none

theorem asSInt_sound {σ : Asg} {s : SVal} {v : Val} {i : SInt}
    (hr : SRel σ s v) (h : asSInt s = some i) : ∃ z, v = .int z ∧ i.val σ = z := by
  cases hr with
  | conc =>
      cases v <;> simp [asSInt] at h
      subst h; exact ⟨_, rfl, SInt.val_lit σ _⟩
  | int hz => simp [asSInt] at h; subst h; exact ⟨_, rfl, hz⟩
  | _ => simp [asSInt] at h

theorem asSBool_sound {σ : Asg} {s : SVal} {v : Val} {e : BVExpr 1}
    (hr : SRel σ s v) (h : asSBool s = some e) : ∃ b, v = .bool b ∧ bitOf (e.eval σ) = b := by
  cases hr with
  | conc =>
      cases v <;> simp [asSBool] at h
      subst h; exact ⟨_, rfl, bit_boolE σ _⟩
  | bool hb => simp [asSBool] at h; subst h; exact ⟨_, rfl, hb⟩
  | _ => simp [asSBool] at h

theorem asSBV_sound {σ : Asg} {s : SVal} {v : Val} {w : Nat} {e : BVExpr w}
    (hr : SRel σ s v) (h : asSBV s = some ⟨w, e⟩) :
    ∃ b, asBV v = some b ∧ b.width = w ∧ bv_uint b = ((e.eval σ).toNat : Int) := by
  cases hr with
  | conc =>
      simp only [asSBV, Option.map_eq_some_iff] at h
      obtain ⟨b, hb, he⟩ := h
      cases he
      exact ⟨b, hb, rfl, (toNat_bvConst σ b).symm⟩
  | bv hb hw hu =>
      simp only [asSBV, Option.some.injEq] at h
      cases h
      exact ⟨_, hb, hw, hu⟩
  | _ => simp [asSBV] at h

mutual

/-- `c ? a : b`.  `none` when the two branches have different SHAPES (a
different list spine or tag, or BVs of different widths): the symbolic run
then refuses rather than guess. -/
def merge (c : BVExpr 1) : SVal → SVal → Option SVal
  | .conc x, .conc y =>
      if Val.beq x y then some (.conc x) else mergeLeaf c (.conc x) (.conc y)
  | .cons a b, .cons a' b' => do
      let h ← merge c a a'
      let t ← merge c b b'
      pure (.cons h t)
  | .ctor k fs, .ctor k' fs' =>
      if k = k' then (mergeList c fs fs').map (.ctor k) else none
  | x, y => mergeLeaf c x y

def mergeList (c : BVExpr 1) : List SVal → List SVal → Option (List SVal)
  | [], [] => some []
  | a :: as, b :: bs => do
      let h ← merge c a b
      let t ← mergeList c as bs
      pure (h :: t)
  | _, _ => none

/-- The leaves: an integer, a Boolean or a bit vector on BOTH sides (either
side may be concrete). -/
def mergeLeaf (c : BVExpr 1) (x y : SVal) : Option SVal :=
  match asSInt x, asSInt y with
  | some i, some j => some (.int (SInt.mux c i j))
  | _, _ =>
    match asSBool x, asSBool y with
    | some a, some b => some (.bool (muxE c a b))
    | _, _ =>
      match asSBV x, asSBV y with
      | some ⟨w, a⟩, some ⟨w', b⟩ =>
          if h : w' = w then some (.bv w (muxE c a (castE h b))) else none
      | _, _ => none

end

/-! ## `merge` is sound: it denotes the TAKEN branch -/

theorem mergeLeaf_t {σ : Asg} {c : BVExpr 1} (hc : bitOf (c.eval σ) = true)
    {x y m : SVal} {v : Val} (hx : SRel σ x v) (h : mergeLeaf c x y = some m) : SRel σ m v := by
  unfold mergeLeaf at h
  split at h
  · rename_i i j hi hj
    cases h
    obtain ⟨z, rfl, hz⟩ := asSInt_sound hx hi
    exact .int (by rw [SInt.val_mux, hc]; exact hz)
  · split at h
    · rename_i a b ha hb
      cases h
      obtain ⟨z, rfl, hz⟩ := asSBool_sound hx ha
      exact .bool (by rw [eval_muxE, hc]; exact hz)
    · split at h
      · rename_i w a w' b ha hb
        split at h
        · cases h
          obtain ⟨b0, hb0, hw, hu⟩ := asSBV_sound hx ha
          exact .bv hb0 hw (by rw [eval_muxE, hc]; exact hu)
        · cases h
      · cases h

theorem mergeLeaf_f {σ : Asg} {c : BVExpr 1} (hc : bitOf (c.eval σ) = false)
    {x y m : SVal} {v : Val} (hy : SRel σ y v) (h : mergeLeaf c x y = some m) : SRel σ m v := by
  unfold mergeLeaf at h
  split at h
  · rename_i i j hi hj
    cases h
    obtain ⟨z, rfl, hz⟩ := asSInt_sound hy hj
    exact .int (by rw [SInt.val_mux, hc]; exact hz)
  · split at h
    · rename_i a b ha hb
      cases h
      obtain ⟨z, rfl, hz⟩ := asSBool_sound hy hb
      exact .bool (by rw [eval_muxE, hc]; exact hz)
    · split at h
      · rename_i w a w' b ha hb
        split at h
        · rename_i hw'
          cases h
          obtain ⟨b0, hb0, hw, hu⟩ := asSBV_sound hy hb
          exact .bv hb0 (by rw [hw, hw']) (by
            rw [eval_muxE, hc]; simp only [Bool.false_eq_true, if_false]
            rw [eval_castE]; exact hu)
        · cases h
      · cases h

/-- When neither side is a matching pair of structures, `merge` IS `mergeLeaf`. -/
theorem merge_leaf_eq {c : BVExpr 1} {x y : SVal}
    (h1 : ∀ a b, x = .conc a → y = .conc b → False)
    (h2 : ∀ a b a' b', x = .cons a b → y = .cons a' b' → False)
    (h3 : ∀ k fs k' fs', x = .ctor k fs → y = .ctor k' fs' → False) :
    merge c x y = mergeLeaf c x y := by
  cases x <;> cases y <;> first
    | rfl
    | (exfalso; exact h1 _ _ rfl rfl)
    | (exfalso; exact h2 _ _ _ _ rfl rfl)
    | (exfalso; exact h3 _ _ _ _ rfl rfl)

theorem merge_t {σ : Asg} {c : BVExpr 1} (hc : bitOf (c.eval σ) = true) (a b : SVal) :
    ∀ {v : Val} {m : SVal}, SRel σ a v → merge c a b = some m → SRel σ m v := by
  refine merge.mutual_induct
    (motive_1 := fun a b => ∀ {v : Val} {m : SVal}, SRel σ a v → merge c a b = some m → SRel σ m v)
    (motive_2 := fun as bs => ∀ {vs : List Val} {ms : List SVal},
        SRelL σ as vs → mergeList c as bs = some ms → SRelL σ ms vs)
    ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ |>.1 a b
  · intro x y hb v m ha h
    simp only [merge, hb, if_true, Option.some.injEq] at h
    subst h; exact ha
  · intro x y hb v m ha h
    simp only [merge, hb, if_false, Bool.false_eq_true] at h
    exact mergeLeaf_t hc ha h
  · intro a1 a2 b1 b2 ih1 ih2 v m ha h
    simp only [merge, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨h1, e1, t1, e2, rfl⟩ := h
    cases ha with
    | cons r1 r2 => exact .cons (ih1 r1 e1) (ih2 r2 e2)
  · intro fs k fs' ih v m ha h
    simp only [merge, if_true, Option.map_eq_some_iff] at h
    obtain ⟨ms, hms, rfl⟩ := h
    cases ha with
    | ctor r => exact .ctor (ih r hms)
  · intro k fs k' fs' hk v m ha h
    simp [merge, hk] at h
  · intro x y h1 h2 h3 v m ha h
    rw [merge_leaf_eq h1 h2 h3] at h
    exact mergeLeaf_t hc ha h
  · intro vs ms ha h
    simp [mergeList] at h; subst h; cases ha; exact .nil
  · intro a1 as b1 bs ih1 ih2 vs ms ha h
    simp only [mergeList, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨h1, e1, t1, e2, rfl⟩ := h
    cases ha with
    | cons r1 r2 => exact .cons (ih1 r1 e1) (ih2 r2 e2)
  · intro as bs h1 h2 vs ms ha h
    cases as <;> cases bs <;> simp_all [mergeList]

theorem merge_f {σ : Asg} {c : BVExpr 1} (hc : bitOf (c.eval σ) = false) (a b : SVal) :
    ∀ {v : Val} {m : SVal}, SRel σ b v → merge c a b = some m → SRel σ m v := by
  refine merge.mutual_induct
    (motive_1 := fun a b => ∀ {v : Val} {m : SVal}, SRel σ b v → merge c a b = some m → SRel σ m v)
    (motive_2 := fun as bs => ∀ {vs : List Val} {ms : List SVal},
        SRelL σ bs vs → mergeList c as bs = some ms → SRelL σ ms vs)
    ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ |>.1 a b
  · intro x y hb v m hy h
    simp only [merge, hb, if_true, Option.some.injEq] at h
    subst h
    rw [Val.eq_of_beq x y hb]; exact hy
  · intro x y hb v m hy h
    simp only [merge, hb, if_false, Bool.false_eq_true] at h
    exact mergeLeaf_f hc hy h
  · intro a1 a2 b1 b2 ih1 ih2 v m hy h
    simp only [merge, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨h1, e1, t1, e2, rfl⟩ := h
    cases hy with
    | cons r1 r2 => exact .cons (ih1 r1 e1) (ih2 r2 e2)
  · intro fs k fs' ih v m hy h
    simp only [merge, if_true, Option.map_eq_some_iff] at h
    obtain ⟨ms, hms, rfl⟩ := h
    cases hy with
    | ctor r => exact .ctor (ih r hms)
  · intro k fs k' fs' hk v m hy h
    simp [merge, hk] at h
  · intro x y h1 h2 h3 v m hy h
    rw [merge_leaf_eq h1 h2 h3] at h
    exact mergeLeaf_f hc hy h
  · intro vs ms hy h
    simp [mergeList] at h; subst h; cases hy; exact .nil
  · intro a1 as b1 bs ih1 ih2 vs ms hy h
    simp only [mergeList, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
      Option.some.injEq] at h
    obtain ⟨h1, e1, t1, e2, rfl⟩ := h
    cases hy with
    | cons r1 r2 => exact .cons (ih1 r1 e1) (ih2 r2 e2)
  · intro as bs h1 h2 vs ms hy h
    cases as <;> cases bs <;> simp_all [mergeList]

end Projection.Sym
