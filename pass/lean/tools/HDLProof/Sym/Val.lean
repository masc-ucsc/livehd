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
import HDLProof.Sym.Kit
import HDLProof.Sym.Bridge
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide

inductive SVal where
  | conc : Val → SVal
  | int  : SInt → SVal
  | bool : BVExpr 1 → SVal
  | bv   : (w : Nat) → BVExpr w → SVal
  | cons : SVal → SVal → SVal
  | ctor : Nat → List SVal → SVal
  /-- A NORMALIZED bit vector: its raw stored value is the unsigned value of `e` (every result
  of a bit-vector primitive is one; an input, a held register or a stored word need not be). -/
  | nbv  : (w : Nat) → BVExpr w → SVal
  /-- A guarded union `c ? a : b` of two values of DIFFERENT shapes (a different list spine,
  constructor or width), which no single symbolic value of one shape can denote. -/
  | alt  : BVExpr 1 → SVal → SVal → SVal

mutual

inductive SRel (σ : Asg) : SVal → Val → Prop where
  | conc : SRel σ (.conc v) v
  | int  : i.val σ = z → SRel σ (.int i) (.int z)
  | bool : bitOf (e.eval σ) = b → SRel σ (.bool e) (.bool b)
  | bv   : asBV v = some b → b.width = w → bv_uint b = ((e.eval σ).toNat : Int) →
           SRel σ (.bv w e) v
  | cons : SRel σ h h' → SRel σ t t' → SRel σ (.cons h t) (.cons h' t')
  | ctor : SRelL σ fs vs → SRel σ (.ctor k fs) (.ctor k vs)
  | nbv  : v = ofBV ⟨w, ((e.eval σ).toNat : Int)⟩ → SRel σ (.nbv w e) v
  | altT : bitOf (c.eval σ) = true → SRel σ a v → SRel σ (.alt c a b) v
  | altF : bitOf (c.eval σ) = false → SRel σ b v → SRel σ (.alt c a b) v

inductive SRelL (σ : Asg) : List SVal → List Val → Prop where
  | nil  : SRelL σ [] []
  | cons : SRel σ s v → SRelL σ ss vs → SRelL σ (s :: ss) (v :: vs)

end

/-! ## Lifting a concrete value to a symbolic shape -/

/-- A bit vector of width `w` holding a `w`-bit `toNat` is normalized. -/
theorem bv_uint_normal_toNat {w : Nat} (X : BitVec w) :
    bv_uint (⟨w, (X.toNat : Int)⟩ : BV) = ((X.toNat : Nat) : Int) := by
  show ((X.toNat : Nat) : Int) % 2 ^ w = ((X.toNat : Nat) : Int)
  have h := X.isLt
  have : ((2 ^ w : Nat) : Int) = (2 : Int) ^ w := by simp [Int.natCast_pow]
  rw [Int.emod_eq_of_lt (by omega) (by omega)]

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
  | .alt c a b      => do
      let i ← asSInt a
      let j ← asSInt b
      pure (SInt.mux c i j)
  | _               => none

/-- The view of an `SVal` as a one-bit Boolean, when it is one. -/
def asSBool : SVal → Option (BVExpr 1)
  | .bool e          => some e
  | .conc (.bool b)  => some (boolE b)
  | _                => none

/-- The view of an `SVal` as a bit vector of SOME width. -/
def asSBV : SVal → Option (Σ w, BVExpr w)
  | .bv w e  => some ⟨w, e⟩
  | .nbv w e => some ⟨w, e⟩
  | .conc v  => (asBV v).map (fun b => ⟨b.width, bvConst b⟩)
  | _        => none

theorem asSInt_sound {σ : Asg} : ∀ {s : SVal} {v : Val} {i : SInt},
    SRel σ s v → asSInt s = some i → ∃ z, v = .int z ∧ i.val σ = z
  | .conc x, v, i, hr, h => by
      cases hr
      cases x <;> simp [asSInt] at h
      subst h; exact ⟨_, rfl, SInt.val_lit σ _⟩
  | .int i', v, i, hr, h => by
      cases hr with
      | int hz => simp [asSInt] at h; subst h; exact ⟨_, rfl, hz⟩
  | .alt c a b, v, i, hr, h => by
      simp only [asSInt, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨i1, h1, i2, h2, rfl⟩ := h
      cases hr with
      | altT hc ha =>
          obtain ⟨z, rfl, hz⟩ := asSInt_sound ha h1
          exact ⟨z, rfl, by rw [SInt.val_mux, hc]; exact hz⟩
      | altF hc hb =>
          obtain ⟨z, rfl, hz⟩ := asSInt_sound hb h2
          exact ⟨z, rfl, by rw [SInt.val_mux, hc]; exact hz⟩
  | .bool _, _, _, _, h | .bv _ _, _, _, _, h | .cons _ _, _, _, _, h | .ctor _ _, _, _, _, h
  | .nbv _ _, _, _, _, h => by simp [asSInt] at h

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
  | nbv hv =>
      simp only [asSBV, Option.some.injEq] at h
      cases h; subst hv
      refine ⟨_, asBV_ofBV _, rfl, ?_⟩
      exact bv_uint_normal_toNat _
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

/-! ### Inversions -/

theorem SRel_cons_inv {σ : Asg} {h t : SVal} {v : Val} (hr : SRel σ (.cons h t) v) :
    ∃ h' t', v = .cons h' t' ∧ SRel σ h h' ∧ SRel σ t t' := by
  cases hr; exact ⟨_, _, rfl, by assumption, by assumption⟩

theorem SRel_ctor_inv {σ : Asg} {k : Nat} {fs : List SVal} {v : Val} (hr : SRel σ (.ctor k fs) v) :
    ∃ vs, v = .ctor k vs ∧ SRelL σ fs vs := by
  cases hr; exact ⟨_, rfl, by assumption⟩

theorem SRelL_cons_inv {σ : Asg} {s : SVal} {ss : List SVal} {vs : List Val}
    (hr : SRelL σ (s :: ss) vs) : ∃ v vs', vs = v :: vs' ∧ SRel σ s v ∧ SRelL σ ss vs' := by
  cases hr; exact ⟨_, _, rfl, by assumption, by assumption⟩

theorem SRel_int_inv {σ : Asg} {i : SInt} {v : Val} (hr : SRel σ (.int i) v) :
    ∃ z, v = .int z ∧ i.val σ = z := by
  cases hr; exact ⟨_, rfl, by assumption⟩

theorem SRel_bool_inv {σ : Asg} {e : BVExpr 1} {v : Val} (hr : SRel σ (.bool e) v) :
    ∃ b, v = .bool b ∧ bitOf (e.eval σ) = b := by
  cases hr; exact ⟨_, rfl, by assumption⟩

theorem SRel_bv_inv {σ : Asg} {w : Nat} {e : BVExpr w} {v : Val} (hr : SRel σ (.bv w e) v) :
    ∃ b, asBV v = some b ∧ b.width = w ∧ bv_uint b = ((e.eval σ).toNat : Int) := by
  cases hr; exact ⟨_, by assumption, by assumption, by assumption⟩

theorem SRel_nbv_inv {σ : Asg} {w : Nat} {e : BVExpr w} {v : Val} (hr : SRel σ (.nbv w e) v) :
    v = ofBV ⟨w, ((e.eval σ).toNat : Int)⟩ := by
  cases hr; assumption

theorem SRel_alt_inv {σ : Asg} {c : BVExpr 1} {a b : SVal} {v : Val} (hr : SRel σ (.alt c a b) v) :
    (bitOf (c.eval σ) = true ∧ SRel σ a v) ∨ (bitOf (c.eval σ) = false ∧ SRel σ b v) := by
  cases hr with
  | altT hc ha => exact .inl ⟨hc, ha⟩
  | altF hc hb => exact .inr ⟨hc, hb⟩

/-! ## Merging two branches of DIFFERENT shapes: guarded unions -/

mutual
/-- A concrete value as a symbolic structure with concrete leaves (a bit vector stays a leaf),
so that it merges field by field with a symbolic value of the same spine. -/
def expandC : Val → SVal
  | .cons h t  => .cons (expandC h) (expandC t)
  | .ctor k vs => if (asBV (.ctor k vs)).isSome then .conc (.ctor k vs) else .ctor k (expandCL vs)
  | v          => .conc v

def expandCL : List Val → List SVal
  | []      => []
  | v :: vs => expandC v :: expandCL vs
end

mutual
theorem expandC_rel {σ : Asg} : ∀ (v : Val), SRel σ (expandC v) v
  | .cons h t => by simp only [expandC]; exact .cons (expandC_rel h) (expandC_rel t)
  | .ctor k vs => by
      simp only [expandC]
      split
      · exact .conc
      · exact .ctor (expandCL_rel vs)
  | .int _ => by simp only [expandC]; exact .conc
  | .bool _ => by simp only [expandC]; exact .conc
  | .nil => by simp only [expandC]; exact .conc

theorem expandCL_rel {σ : Asg} : ∀ (vs : List Val), SRelL σ (expandCL vs) vs
  | [] => .nil
  | v :: vs => .cons (expandC_rel v) (expandCL_rel vs)
end

mutual
/-- Every concrete part of a symbolic value lifted with `expandC`. -/
def expandS : SVal → SVal
  | .conc v     => expandC v
  | .cons h t   => .cons (expandS h) (expandS t)
  | .ctor k fs  => .ctor k (expandSL fs)
  | s           => s

def expandSL : List SVal → List SVal
  | []      => []
  | s :: ss => expandS s :: expandSL ss
end

theorem expandS_rel {σ : Asg} {s : SVal} {v : Val} (hr : SRel σ s v) : SRel σ (expandS s) v := by
  refine SRel.rec (motive_1 := fun s v _ => SRel σ (expandS s) v)
    (motive_2 := fun ss vs _ => SRelL σ (expandSL ss) vs) ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ ?_ hr
  · intro v; exact expandC_rel v
  · intro i z hz; exact .int hz
  · intro e b hb; exact .bool hb
  · intro v b w e hb hw hu; exact .bv hb hw hu
  · intro h h' t t' _ _ ih1 ih2; exact .cons ih1 ih2
  · intro fs vs k _ ih; exact .ctor ih
  · intro v w e hv; exact .nbv hv
  · intro c a v b hc ha _; exact .altT hc ha
  · intro c b v a hc hb _; exact .altF hc hb
  · exact .nil
  · intro s v ss vs _ _ ih1 ih2; exact .cons ih1 ih2

/-- How many leaves a guarded union of concrete integers has, if it is one. -/
def finIntLeaves : SVal → Option Nat
  | .conc (.int _) => some 1
  | .alt _ a b => do
      let x ← finIntLeaves a
      let y ← finIntLeaves b
      pure (x + y)
  | _ => none

/-- The number of leaves of a guarded union. -/
def altSize : SVal → Nat
  | .alt _ a b => altSize a + altSize b
  | _ => 1

/-- `c ? a : b` for branches of ANY shapes. Two concrete integers (or unions of them, at most 16
choices) stay a guarded union, so a width or a position chosen by a branch stays concrete on each
path; otherwise `merge`, then `merge` with the concrete parts lifted to structures, then a guarded
union of at most 64 leaves. `none` only past that bound (a resource limit). -/
def mergeA (c : BVExpr 1) (a b : SVal) : Option SVal :=
  match finIntLeaves a, finIntLeaves b, a, b with
  | some x, some y, .conc u, .conc u' =>
      if Val.beq u u' then some a else if x + y ≤ 16 then some (.alt c a b) else merge c a b
  | some x, some y, _, _ => if x + y ≤ 16 then some (.alt c a b) else merge c a b
  | _, _, _, _ =>
      match merge c a b with
      | some m => some m
      | none =>
          match merge c (expandS a) (expandS b) with
          | some m => some m
          | none => if altSize a + altSize b ≤ 64 then some (.alt c a b) else none

theorem mergeA_t {σ : Asg} {c : BVExpr 1} (hc : bitOf (c.eval σ) = true) {a b m : SVal} {v : Val}
    (ha : SRel σ a v) (h : mergeA c a b = some m) : SRel σ m v := by
  unfold mergeA at h
  split at h
  · split at h
    · cases h; exact ha
    · split at h
      · cases h; exact .altT hc ha
      · exact merge_t hc _ _ ha h
  · split at h
    · cases h; exact .altT hc ha
    · exact merge_t hc _ _ ha h
  · split at h
    · rename_i m' hm; cases h; exact merge_t hc _ _ ha hm
    · split at h
      · rename_i m' hm; cases h; exact merge_t hc _ _ (expandS_rel ha) hm
      · split at h
        · cases h; exact .altT hc ha
        · cases h

theorem mergeA_f {σ : Asg} {c : BVExpr 1} (hc : bitOf (c.eval σ) = false) {a b m : SVal} {v : Val}
    (hb : SRel σ b v) (h : mergeA c a b = some m) : SRel σ m v := by
  unfold mergeA at h
  split at h
  · split at h
    · rename_i hbeq
      cases h
      rw [Val.eq_of_beq _ _ hbeq]; exact hb
    · split at h
      · cases h; exact .altF hc hb
      · exact merge_f hc _ _ hb h
  · split at h
    · cases h; exact .altF hc hb
    · exact merge_f hc _ _ hb h
  · split at h
    · rename_i m' hm; cases h; exact merge_f hc _ _ hb hm
    · split at h
      · rename_i m' hm; cases h; exact merge_f hc _ _ (expandS_rel hb) hm
      · split at h
        · cases h; exact .altF hc hb
        · cases h

/-- The first guarded union among a list of operands, split into its two sides. -/
def splitAltL : List SVal → Option (BVExpr 1 × List SVal × List SVal)
  | [] => none
  | .alt c a b :: ss => some (c, a :: ss, b :: ss)
  | s :: ss => (splitAltL ss).map fun (c, xs, ys) => (c, s :: xs, s :: ys)

theorem splitAltL_sound {σ : Asg} :
    ∀ {ss : List SVal} {vs : List Val} {c : BVExpr 1} {xs ys : List SVal},
      SRelL σ ss vs → splitAltL ss = some (c, xs, ys) →
      (bitOf (c.eval σ) = true → SRelL σ xs vs) ∧ (bitOf (c.eval σ) = false → SRelL σ ys vs)
  | [], _, _, _, _, _, h => by simp [splitAltL] at h
  | s :: ss, vs, c, xs, ys, hr, h => by
      obtain ⟨v, vs', rfl, r, rs⟩ := SRelL_cons_inv hr
      by_cases hs : ∃ c' a b, s = .alt c' a b
      · obtain ⟨c', a, b, rfl⟩ := hs
        simp only [splitAltL, Option.some.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl, rfl⟩ := h
        rcases SRel_alt_inv r with ⟨ht, ra⟩ | ⟨hf, rb⟩
        · exact ⟨fun _ => .cons ra rs, fun h' => (by rw [ht] at h'; cases h')⟩
        · exact ⟨fun h' => (by rw [hf] at h'; cases h'), fun _ => .cons rb rs⟩
      · have e : splitAltL (s :: ss) = (splitAltL ss).map fun (c, xs, ys) => (c, s :: xs, s :: ys) := by
          cases s <;> simp_all [splitAltL]
        rw [e] at h
        simp only [Option.map_eq_some_iff] at h
        obtain ⟨⟨c', xs', ys'⟩, h', he⟩ := h
        simp only [Prod.mk.injEq] at he; obtain ⟨rfl, rfl, rfl⟩ := he
        obtain ⟨h1, h2⟩ := splitAltL_sound rs h'
        exact ⟨fun hc => .cons r (h1 hc), fun hc => .cons r (h2 hc)⟩

/-- A list with a concrete spine. -/
def sListOf : List SVal → SVal
  | []      => .conc .nil
  | s :: ss => .cons s (sListOf ss)

end HDLProof.Sym
