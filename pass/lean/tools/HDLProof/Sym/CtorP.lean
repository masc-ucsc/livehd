/-
`Sym.CtorP` -- the object language's constructor primitives on symbolic values:
`mkCtorP` (a constructor from a concrete tag and a field list with a concrete spine),
`ctorTagP` (the tag; a bit vector's is `bvTag`) and `ctorFieldsP` (the fields as a list; a
NORMALIZED bit vector's are its width and value, a raw `.bv`'s raw value is not known, so it is
refused). Each with its soundness lemma against `evalPrim`. Mathlib-free.
-/
import HDLProof.Sym.Mem
import HDLProof.Sym.EqV

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide

theorem valList_listVal : ∀ (vs : List Val), valList (listVal vs) = some vs
  | [] => rfl
  | v :: vs => by simp [listVal, valList, valList_listVal vs]

def symMkCtorP : List SVal → Option SVal
  | [k, l] => do
      let kz ← cIntOf k
      if 0 ≤ kz then (sListElems l).map (fun fs => .ctor kz.toNat fs) else none
  | _ => none

theorem symMkCtorP_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symMkCtorP ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .mkCtorP vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [k, l], h =>
    simp only [symMkCtorP, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
    obtain ⟨kz, hk, h⟩ := h
    split at h
    · rename_i h0
      simp only [Option.map_eq_some_iff] at h
      obtain ⟨fs, hfs, rfl⟩ := h
      obtain ⟨vk, vl, rfl, rk, rl⟩ := SRelL_two hr
      rw [cIntOf_sound rk hk] at he
      obtain ⟨vs', rfl, rfs⟩ := sListElems_sound l fs vl rl hfs
      simp only [evalPrim, h0, if_true, valList_listVal, Except.ok.injEq] at he; subst he
      exact .ctor rfs
    · cases h

def symCtorTagP : List SVal → Option SVal
  | [.ctor k _] => some (.conc (.int (Int.ofNat k)))
  | [.nbv _ _] => some (.conc (.int (Int.ofNat bvTag)))
  | [.bv _ _] => some (.conc (.int (Int.ofNat bvTag)))
  | _ => none

theorem symCtorTagP_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symCtorTagP ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .ctorTagP vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [.ctor k fs], h =>
    simp only [symCtorTagP, Option.some.injEq] at h; subst h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    obtain ⟨vs', rfl, _⟩ := SRel_ctor_inv rx
    simp only [evalPrim, Except.ok.injEq] at he; subst he; exact .conc
  | [.nbv w e], h =>
    simp only [symCtorTagP, Option.some.injEq] at h; subst h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    rw [SRel_nbv_inv rx] at he
    simp only [evalPrim, ofBV, mkBV, Except.ok.injEq] at he; subst he; exact .conc
  | [.bv w e], h =>
    simp only [symCtorTagP, Option.some.injEq] at h; subst h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    obtain ⟨b, hb, _, _⟩ := SRel_bv_inv rx
    rw [asBV_form hb] at he
    simp only [evalPrim, Except.ok.injEq] at he; subst he; exact .conc

def symCtorFieldsP : List SVal → Option SVal
  | [.ctor _ fs] => some (sListOf fs)
  | [.nbv w e] => some (sListOf [.conc (.int (Int.ofNat w)), .int (SInt.ofUint e)])
  | _ => none

theorem symCtorFieldsP_sound {σ : Asg} {ss : List SVal} {vs : List Val} {s : SVal} {v : Val}
    (h : symCtorFieldsP ss = some s) (hr : SRelL σ ss vs) (he : evalPrim .ctorFieldsP vs = .ok v) :
    SRel σ s v := by
  match ss, h with
  | [.ctor k fs], h =>
    simp only [symCtorFieldsP, Option.some.injEq] at h; subst h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    obtain ⟨vs', rfl, r⟩ := SRel_ctor_inv rx
    simp only [evalPrim, Except.ok.injEq] at he; subst he
    exact sListOf_listVal r
  | [.nbv w e], h =>
    simp only [symCtorFieldsP, Option.some.injEq] at h; subst h
    obtain ⟨vx, rfl, rx⟩ := SRelL_one hr
    rw [SRel_nbv_inv rx] at he
    simp only [evalPrim, ofBV, mkBV, Except.ok.injEq] at he; subst he
    exact sListOf_listVal (.cons .conc (.cons (.int (SInt.val_ofUint σ e)) .nil))

end HDLProof.Sym
