/-
`Sym.EqV` -- the object language's `eqV` (structural equality, `Val.beq`) on symbolic values.

Each value is read through its HEAD (`headOf`): an integer, a Boolean, `nil`, a list cell, a
constructor with its fields, or a bit vector whose raw stored value is NOT known (`.bv`: an
input, a held register, a stored word may be unnormalized, and `Val.beq` compares the raw
value). A normalized bit vector (`.nbv`) is a constructor `bvTag [width, value]` whose value is
the unsigned value of its expression. Two heads of different kinds are unequal; integers compare
by `SInt.eq`, Booleans by xnor, structures field by field. A raw bit vector is compared only when
its shape alone decides the answer (a different width, tag or arity); otherwise `none`, which
`symPrim` reports as an unnormalized bit vector. Mathlib-free.
-/
import HDLProof.Sym.Prim

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide

/-- A value's head, as `Val.beq` compares it. -/
inductive Head where
  | int   : SInt → Head
  | bool  : BVExpr 1 → Head
  | nil   : Head
  | cons  : SVal → SVal → Head
  | ctor  : Nat → List SVal → Head
  /-- a bit vector of width `w` whose raw stored value is not known -/
  | rawBV : Nat → Head

def headOf : SVal → Option Head
  | .conc (.int z)     => some (.int (SInt.lit z))
  | .conc (.bool b)    => some (.bool (boolE b))
  | .conc .nil         => some .nil
  | .conc (.cons h t)  => some (.cons (.conc h) (.conc t))
  | .conc (.ctor k vs) => some (.ctor k (vs.map .conc))
  | .int i             => some (.int i)
  | .bool e            => some (.bool e)
  | .cons h t          => some (.cons h t)
  | .ctor k fs         => some (.ctor k fs)
  | .nbv w e           => some (.ctor bvTag [.conc (.int (Int.ofNat w)), .int (SInt.ofUint e)])
  | .bv w _            => some (.rawBV w)
  | .alt _ _ _         => none

/-- What a head says about the concrete value. -/
def HeadRel (σ : Asg) : Head → Val → Prop
  | .int i, v     => ∃ z, v = .int z ∧ i.val σ = z
  | .bool e, v    => ∃ b, v = .bool b ∧ bitOf (e.eval σ) = b
  | .nil, v       => v = .nil
  | .cons h t, v  => ∃ hv tv, v = .cons hv tv ∧ SRel σ h hv ∧ SRel σ t tv
  | .ctor k fs, v => ∃ vs, v = .ctor k vs ∧ SRelL σ fs vs
  | .rawBV w, v   => ∃ raw, v = .ctor bvTag [.int (Int.ofNat w), .int raw]

theorem SRelL_concs {σ : Asg} : ∀ (vs : List Val), SRelL σ (vs.map .conc) vs
  | [] => .nil
  | _ :: vs => .cons .conc (SRelL_concs vs)

/-- A value `asBV` reads is a `bvTag` constructor of its width and raw value. -/
theorem asBV_form {v : Val} {b : BV} (h : asBV v = some b) :
    v = .ctor bvTag [.int (Int.ofNat b.width), .int b.value] := by
  unfold asBV at h
  split at h
  · rename_i t w x
    split at h
    · split at h
      · rename_i ht hw
        cases h; subst ht
        simp only [Int.ofNat_eq_coe, Int.toNat_of_nonneg hw]
      · cases h
    · cases h
  · cases h

theorem headOf_sound {σ : Asg} {s : SVal} {v : Val} {h : Head} (hr : SRel σ s v)
    (hh : headOf s = some h) : HeadRel σ h v := by
  cases hr with
  | conc =>
      cases v with
      | int z => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨z, rfl, SInt.val_lit σ z⟩
      | bool b => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨b, rfl, bit_boolE σ b⟩
      | nil => simp only [headOf, Option.some.injEq] at hh; subst hh; rfl
      | cons a t => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨a, t, rfl, .conc, .conc⟩
      | ctor k vs => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨vs, rfl, SRelL_concs vs⟩
  | int hz => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨_, rfl, hz⟩
  | bool hb => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨_, rfl, hb⟩
  | bv hb hw _ =>
      simp only [headOf, Option.some.injEq] at hh; subst hh
      rw [asBV_form hb, hw]; exact ⟨_, rfl⟩
  | cons r1 r2 => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨_, _, rfl, r1, r2⟩
  | ctor r => simp only [headOf, Option.some.injEq] at hh; subst hh; exact ⟨_, rfl, r⟩
  | nbv hv =>
      rename_i w e
      simp only [headOf, Option.some.injEq] at hh; subst hh; subst hv
      exact ⟨_, rfl, .cons .conc (.cons (.int (SInt.val_ofUint σ e)) .nil)⟩
  | altT _ _ => simp [headOf] at hh
  | altF _ _ => simp [headOf] at hh

/-- A raw bit vector (`bvTag [w, raw]`) against a `bvTag` constructor with fields `fs`: unequal
when the arity or a concrete width differs, or the width field is not an integer; otherwise the
raw value decides, which is not known (`none`). -/
def rawVsFields (w : Nat) : List SVal → Option (BVExpr 1)
  | [f0, _] =>
      match cIntOf f0 with
      | some w' => if w' = Int.ofNat w then none else some falseE
      | none =>
          match headOf f0 with
          | some (.int _) => none
          | some _ => some falseE
          | none => none
  | _ => some falseE

/-- Equality of two heads; `rec` compares sub-values. -/
def eqHead (rec : SVal → SVal → Option (BVExpr 1)) : Head → Head → Option (BVExpr 1)
  | .int i, .int j => some (SInt.eq i j)
  | .bool x, .bool y => some (notE (.bin x .xor y))
  | .nil, .nil => some trueE
  | .cons h t, .cons h' t' => do
      let a ← rec h h'
      let b ← rec t t'
      pure (andE a b)
  | .ctor k fs, .ctor k' fs' => if k = k' then eqFields rec fs fs' else some falseE
  | .rawBV w, .rawBV w' => if w = w' then none else some falseE
  | .rawBV w, .ctor k fs => if k = bvTag then rawVsFields w fs else some falseE
  | .ctor k fs, .rawBV w => if k = bvTag then rawVsFields w fs else some falseE
  | _, _ => some falseE
where
  eqFields (rec : SVal → SVal → Option (BVExpr 1)) : List SVal → List SVal → Option (BVExpr 1)
    | [], [] => some trueE
    | a :: as, b :: bs => do
        let x ← rec a b
        let y ← eqFields rec as bs
        pure (andE x y)
    | _, _ => some falseE

/-- `eqV`, with a depth budget `d`. -/
def symEqV : Nat → SVal → SVal → Option (BVExpr 1)
  | 0, _, _ => none
  | d + 1, a, b =>
      match headOf a, headOf b with
      | some ha, some hb => eqHead (symEqV d) ha hb
      | _, _ => none

/-! ## Soundness -/

/-- The outermost constructor of a value: what `Val.beq` checks first. -/
def valKind : Val → Nat
  | .int _ => 0 | .bool _ => 1 | .nil => 2 | .cons _ _ => 3 | .ctor _ _ => 4

def headKind : Head → Nat
  | .int _ => 0 | .bool _ => 1 | .nil => 2 | .cons _ _ => 3 | .ctor _ _ => 4 | .rawBV _ => 4

theorem kind_of {σ : Asg} {h : Head} {v : Val} (hr : HeadRel σ h v) : valKind v = headKind h := by
  cases h with
  | int i => obtain ⟨z, rfl, _⟩ := hr; rfl
  | bool e => obtain ⟨b, rfl, _⟩ := hr; rfl
  | nil => simp only [HeadRel] at hr; subst hr; rfl
  | cons a t => obtain ⟨_, _, rfl, _, _⟩ := hr; rfl
  | ctor k fs => obtain ⟨_, rfl, _⟩ := hr; rfl
  | rawBV w => obtain ⟨_, rfl⟩ := hr; rfl

theorem beq_kind_ne {a b : Val} (h : valKind a ≠ valKind b) : Val.beq a b = false := by
  cases a <;> cases b <;> simp_all [valKind, Val.beq]

theorem Val.beq_comm' (a b : Val) : Val.beq a b = Val.beq b a := by
  cases h1 : Val.beq a b <;> cases h2 : Val.beq b a <;> try rfl
  · rw [Val.beq_iff] at h2; subst h2; rw [Val.beq_refl] at h1; cases h1
  · rw [Val.beq_iff] at h1; subst h1; rw [Val.beq_refl] at h2; cases h2

theorem Val.beqList_comm' (as bs : List Val) : Val.beqList as bs = Val.beqList bs as := by
  have h := Val.beq_comm' (.ctor 0 as) (.ctor 0 bs)
  simpa [Val.beq] using h

theorem bit_falseE (σ : Asg) : bitOf (falseE.eval σ) = false := by simp [falseE, bitOf]
theorem bit_trueE (σ : Asg) : bitOf (trueE.eval σ) = true := by simp [trueE, bitOf]

theorem rawVsFields_sound {σ : Asg} {w : Nat} {raw : Int} {fs : List SVal} {vs : List Val}
    {e : BVExpr 1} (hr : SRelL σ fs vs) (h : rawVsFields w fs = some e) :
    bitOf (e.eval σ) = Val.beqList [.int (Int.ofNat w), .int raw] vs := by
  match fs, hr with
  | [f0, f1], .cons r0 (.cons _ .nil) =>
    cases hc : cIntOf f0 with
    | some w' =>
      by_cases hw : w' = Int.ofNat w
      · simp [rawVsFields, hc, hw] at h
      · simp only [rawVsFields, hc, hw, if_false, Option.some.injEq] at h; subst h
        rw [cIntOf_sound r0 hc]
        have : (Int.ofNat w == w') = false := by
          simp only [beq_eq_false_iff_ne]; intro h'; exact hw h'.symm
        simp [Val.beqList, Val.beq, this, bit_falseE]
        intro h'; exact absurd h'.symm (by simpa using hw)
    | none =>
      cases hh : headOf f0 with
      | none => simp [rawVsFields, hc, hh] at h
      | some h0 =>
        have hk := kind_of (headOf_sound r0 hh)
        cases h0 with
        | int i => simp [rawVsFields, hc, hh] at h
        | _ =>
          simp only [rawVsFields, hc, hh, Option.some.injEq] at h; subst h
          rw [bit_falseE]
          simp only [Val.beqList]
          rw [beq_kind_ne (by rw [hk]; simp [valKind, headKind])]
          simp
  | [], .nil =>
    simp [rawVsFields] at h; subst h; simp [Val.beqList, bit_falseE]
  | [_], .cons _ .nil =>
    simp [rawVsFields] at h; subst h; simp [Val.beqList, bit_falseE]
  | _ :: _ :: _ :: _, .cons _ (.cons _ (.cons _ _)) =>
    simp [rawVsFields] at h; subst h; simp [Val.beqList, bit_falseE]

/-- What the comparison of two sub-values must satisfy. -/
def RecOK (σ : Asg) (rec : SVal → SVal → Option (BVExpr 1)) : Prop :=
  ∀ {a b : SVal} {va vb : Val} {e : BVExpr 1}, SRel σ a va → SRel σ b vb → rec a b = some e →
    bitOf (e.eval σ) = Val.beq va vb

theorem eqFields_sound {σ : Asg} {rec : SVal → SVal → Option (BVExpr 1)} (hrec : RecOK σ rec) :
    ∀ {fs fs' : List SVal} {vs vs' : List Val} {e : BVExpr 1}, SRelL σ fs vs → SRelL σ fs' vs' →
      eqHead.eqFields rec fs fs' = some e → bitOf (e.eval σ) = Val.beqList vs vs'
  | [], [], _, _, e, ra, rb, h => by
      cases ra; cases rb
      simp only [eqHead.eqFields, Option.some.injEq] at h; subst h; simp [Val.beqList, bit_trueE]
  | a :: as, b :: bs, _, _, e, ra, rb, h => by
      obtain ⟨va, vas, rfl, r1, r2⟩ := SRelL_cons_inv ra
      obtain ⟨vb, vbs, rfl, r3, r4⟩ := SRelL_cons_inv rb
      simp only [eqHead.eqFields, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, hx, y, hy, rfl⟩ := h
      rw [bit_andE, hrec r1 r3 hx, eqFields_sound hrec r2 r4 hy]
      simp [Val.beqList]
  | [], _ :: _, _, _, e, ra, rb, h => by
      cases ra; obtain ⟨_, _, rfl, _, _⟩ := SRelL_cons_inv rb
      simp only [eqHead.eqFields, Option.some.injEq] at h; subst h; simp [Val.beqList, bit_falseE]
  | _ :: _, [], _, _, e, ra, rb, h => by
      cases rb; obtain ⟨_, _, rfl, _, _⟩ := SRelL_cons_inv ra
      simp only [eqHead.eqFields, Option.some.injEq] at h; subst h; simp [Val.beqList, bit_falseE]

theorem eqHead_sound {σ : Asg} {rec : SVal → SVal → Option (BVExpr 1)} (hrec : RecOK σ rec)
    {ha hb : Head} {va vb : Val} {e : BVExpr 1} (ra : HeadRel σ ha va) (rb : HeadRel σ hb vb)
    (h : eqHead rec ha hb = some e) : bitOf (e.eval σ) = Val.beq va vb := by
  by_cases hk : headKind ha = headKind hb
  · cases ha <;> cases hb <;> simp [headKind] at hk
    · -- int, int
      rename_i i j
      obtain ⟨z, rfl, hz⟩ := ra; obtain ⟨z', rfl, hz'⟩ := rb
      simp only [eqHead, Option.some.injEq] at h; subst h
      rw [SInt.bit_eq, hz, hz']; simp only [Val.beq]
      by_cases hzz : z = z' <;> simp [hzz]
    · -- bool, bool
      rename_i x y
      obtain ⟨b, rfl, hb⟩ := ra; obtain ⟨b', rfl, hb'⟩ := rb
      simp only [eqHead, Option.some.injEq] at h; subst h
      simp only [bit_notE, Val.beq]
      simp only [bitOf] at hb hb' ⊢
      simp only [BVExpr.eval_bin, BVBinOp.eval, BitVec.getLsbD_xor, hb, hb']
      cases b <;> cases b' <;> rfl
    · -- nil, nil
      simp only [HeadRel] at ra rb; subst ra; subst rb
      simp only [eqHead, Option.some.injEq] at h; subst h; simp [Val.beq, bit_trueE]
    · -- cons, cons
      obtain ⟨hv, tv, rfl, r1, r2⟩ := ra; obtain ⟨hv', tv', rfl, r3, r4⟩ := rb
      simp only [eqHead, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, hx, y, hy, rfl⟩ := h
      rw [bit_andE, hrec r1 r3 hx, hrec r2 r4 hy]; simp [Val.beq]
    · -- ctor, ctor
      rename_i k fs k' fs'
      obtain ⟨vs, rfl, r1⟩ := ra; obtain ⟨vs', rfl, r2⟩ := rb
      simp only [eqHead] at h
      split at h
      · rename_i hkk; subst hkk
        rw [eqFields_sound hrec r1 r2 h]; simp [Val.beq]
      · rename_i hkk
        simp only [Option.some.injEq] at h; subst h
        rw [bit_falseE]; simp [Val.beq, hkk]
    · -- ctor, rawBV
      rename_i k fs w
      obtain ⟨vs, rfl, r1⟩ := ra; obtain ⟨raw, rfl⟩ := rb
      simp only [eqHead] at h
      split at h
      · rename_i hkk; subst hkk
        rw [rawVsFields_sound (raw := raw) r1 h, Val.beqList_comm']; simp [Val.beq]
      · rename_i hkk
        simp only [Option.some.injEq] at h; subst h
        rw [bit_falseE]; simp [Val.beq, hkk]
    · -- rawBV, ctor
      rename_i w k fs
      obtain ⟨raw, rfl⟩ := ra; obtain ⟨vs, rfl, r1⟩ := rb
      simp only [eqHead] at h
      split at h
      · rename_i hkk; subst hkk
        rw [rawVsFields_sound (raw := raw) r1 h]; simp [Val.beq]
      · rename_i hkk
        simp only [Option.some.injEq] at h; subst h
        rw [bit_falseE]; simp [Val.beq]; intro h'; exact absurd h'.symm hkk
    · -- rawBV, rawBV
      rename_i w w'
      obtain ⟨raw, rfl⟩ := ra; obtain ⟨raw', rfl⟩ := rb
      simp only [eqHead] at h
      split at h
      · cases h
      · rename_i hww
        simp only [Option.some.injEq] at h; subst h
        rw [bit_falseE]; simp [Val.beq, Val.beqList]; intro h'; exact absurd (by omega) hww
  · have hf : eqHead rec ha hb = some falseE := by
      cases ha <;> cases hb <;> simp_all [headKind, eqHead]
    rw [hf] at h; cases h
    rw [bit_falseE, beq_kind_ne (by rw [kind_of ra, kind_of rb]; exact hk)]

/-- **`eqV` is sound**: a symbolic answer denotes `Val.beq` of the concrete values. -/
theorem symEqV_sound {σ : Asg} :
    ∀ (d : Nat) {a b : SVal} {va vb : Val} {e : BVExpr 1}, SRel σ a va → SRel σ b vb →
      symEqV d a b = some e → bitOf (e.eval σ) = Val.beq va vb
  | 0, _, _, _, _, _, _, _, h => by simp [symEqV] at h
  | d + 1, a, b, va, vb, e, ra, rb, h => by
      simp only [symEqV] at h
      split at h
      · rename_i ha hb h1 h2
        exact eqHead_sound (fun r1 r2 hx => symEqV_sound d r1 r2 hx)
          (headOf_sound ra h1) (headOf_sound rb h2) h
      · cases h

end HDLProof.Sym
