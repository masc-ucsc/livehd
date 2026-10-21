/-
`Sym.Shape` -- the shape a symbolic state forces on the concrete one (M3 S3).

`stateShapeOf s` reads the list spines and widths off a symbolic state; when
`s` denotes `encStateM st`, `st` has exactly that shape
(`stateShapeOf_sound`). Running one symbolic step from `stateVar L` and
checking the next state's shape is `L` is therefore what makes "the state has
layout `L`" an INVARIANT of every run -- checked once, at run time, with no
lemma about `interpretDesign` needed.
-/
import HDLProof.Sym.Trace

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide Compiler

/-- The widths along a list of bit vectors. -/
def widthsOf : SVal → Option (List Nat)
  | .conc .nil => some []
  | .conc (.cons h t) => do
      let b ← asBV h
      let ws ← widthsOf (.conc t)
      pure (b.width :: ws)
  | .cons h t => do
      let ⟨w, _⟩ ← asSBV h
      let ws ← widthsOf t
      pure (w :: ws)
  | _ => none

theorem widthsOf_sound {σ : Asg} :
    ∀ (s : SVal) (xs : List BV) (ws : List Nat), SRel σ s (encListG ofBV xs) → widthsOf s = some ws →
      xs.map BV.width = ws
  | .conc v, xs, ws, hr, h => by
      have := SRel_conc_inv hr
      subst this
      cases xs with
      | nil => simp [encListG, widthsOf] at h; subst h; rfl
      | cons x xs =>
          simp only [encListG, widthsOf, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
            Option.some.injEq, asBV_ofBV] at h
          obtain ⟨b, hb, ws', hws, rfl⟩ := h
          cases hb
          rw [List.map_cons, widthsOf_sound (σ := σ) (.conc (encListG ofBV xs)) xs ws' .conc hws]
  | .cons h t, xs, ws, hr, hw => by
      simp only [widthsOf, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at hw
      obtain ⟨⟨w, e⟩, he, ws', hws, rfl⟩ := hw
      cases xs with
      | nil => obtain ⟨_, _, hc, _, _⟩ := SRel_cons_inv hr; cases hc
      | cons x xs =>
          obtain ⟨_, _, hc, r1, r2⟩ := SRel_cons_inv hr
          cases hc
          obtain ⟨b, hb, hbw, _⟩ := asSBV_sound r1 he
          rw [asBV_ofBV] at hb; cases hb
          rw [List.map_cons, hbw, widthsOf_sound t xs ws' r2 hws]
  | .int _, _, _, _, h => by simp [widthsOf] at h
  | .bool _, _, _, _, h => by simp [widthsOf] at h
  | .bv _ _, _, _, _, h => by simp [widthsOf] at h
  | .ctor _ _, _, _, _, h => by simp [widthsOf] at h

/-- An image's `(aw, dw)`: `2^aw` words, every word and the default of width `dw`. -/
def imgShapeOf (s : SVal) : Option (Nat × Nat) :=
  match s with
  | .ctor t [wl, d] =>
      if t = MemImg.tagMemImg then do
        let ws ← widthsOf wl
        let ⟨dw, _⟩ ← asSBV d
        let aw := Nat.log2 ws.length
        if ws.length = 2 ^ aw ∧ ws.all (· == dw) then some (aw, dw) else none
      else none
  | .conc (.ctor t [wl, d]) =>
      if t = MemImg.tagMemImg then do
        let ws ← widthsOf (.conc wl)
        let b ← asBV d
        let aw := Nat.log2 ws.length
        if ws.length = 2 ^ aw ∧ ws.all (· == b.width) then some (aw, b.width) else none
      else none
  | _ => none

theorem all_eq_of {ws : List Nat} {dw : Nat} (h : ws.all (· == dw) = true) : ∀ w ∈ ws, w = dw := by
  intro w hw
  have := List.all_eq_true.mp h w hw
  simpa using this

theorem imgShapeOf_sound {σ : Asg} {s : SVal} {m : MemImg} {aw dw : Nat}
    (hr : SRel σ s (MemImg.encMemImg m)) (h : imgShapeOf s = some (aw, dw)) :
    m.words.length = 2 ^ aw ∧ (∀ x ∈ m.words, x.width = dw) ∧ m.dflt.width = dw := by
  unfold MemImg.encMemImg at hr
  unfold imgShapeOf at h
  split at h
  · rename_i t wl d
    split at h
    · simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨ws, hws, ⟨dw', e⟩, hd, h⟩ := h
      split at h
      · rename_i hc
        simp only [Option.some.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl⟩ := h
        obtain ⟨vs, he, rl⟩ := SRel_ctor_inv hr
        cases he
        obtain ⟨_, _, h1, r1, rl'⟩ := SRelL_cons_inv rl
        cases h1
        obtain ⟨_, _, h2, r2, _⟩ := SRelL_cons_inv rl'
        cases h2
        have hmap := widthsOf_sound wl m.words ws r1 hws
        obtain ⟨b, hb, hbw, _⟩ := asSBV_sound r2 hd
        rw [asBV_ofBV] at hb; cases hb
        refine ⟨?_, fun x hx => ?_, hbw⟩
        · have := congrArg List.length hmap; simp at this; rw [this]; exact hc.1
        · have hx' : x.width ∈ ws := by rw [← hmap]; exact List.mem_map_of_mem hx
          exact all_eq_of hc.2 _ hx'
      · cases h
    · cases h
  · rename_i t wl d
    split at h
    · have := SRel_conc_inv hr
      cases this
      simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨ws, hws, b, hb, h⟩ := h
      split at h
      · rename_i hc
        simp only [Option.some.injEq, Prod.mk.injEq] at h
        obtain ⟨rfl, rfl⟩ := h
        have hmap := widthsOf_sound (σ := σ) (.conc (encListG ofBV m.words)) m.words ws .conc hws
        rw [asBV_ofBV] at hb; cases hb
        refine ⟨?_, fun x hx => ?_, rfl⟩
        · have := congrArg List.length hmap; simp at this; rw [this]; exact hc.1
        · have hx' : x.width ∈ ws := by rw [← hmap]; exact List.mem_map_of_mem hx
          exact all_eq_of hc.2 _ hx'
      · cases h
    · cases h
  · cases h

/-- The shapes along a list of images. -/
def imgsShapeOf : SVal → Option (List (Nat × Nat))
  | .conc .nil => some []
  | .conc (.cons h t) => do
      let p ← imgShapeOf (.conc h)
      let ps ← imgsShapeOf (.conc t)
      pure (p :: ps)
  | .cons h t => do
      let p ← imgShapeOf h
      let ps ← imgsShapeOf t
      pure (p :: ps)
  | _ => none

theorem imgsShapeOf_sound {σ : Asg} :
    ∀ (s : SVal) (ms : List MemImg) (ps : List (Nat × Nat)),
      SRel σ s (encListG MemImg.encMemImg ms) → imgsShapeOf s = some ps →
      ms.length = ps.length ∧ ∀ (j : Nat) (m : MemImg) (aw dw : Nat), ms[j]? = some m → ps[j]? = some (aw, dw) →
        m.words.length = 2 ^ aw ∧ (∀ x ∈ m.words, x.width = dw) ∧ m.dflt.width = dw
  | .conc v, ms, ps, hr, h => by
      have := SRel_conc_inv hr
      subst this
      cases ms with
      | nil => simp [encListG, imgsShapeOf] at h; subst h; exact ⟨rfl, fun j m _ _ hm _ => by simp at hm⟩
      | cons m ms =>
          simp only [encListG, imgsShapeOf, Option.bind_eq_bind, Option.bind_eq_some_iff,
            Option.pure_def, Option.some.injEq] at h
          obtain ⟨p, hp, ps', hps, rfl⟩ := h
          obtain ⟨hl, hs⟩ := imgsShapeOf_sound (σ := σ) (.conc (encListG MemImg.encMemImg ms)) ms ps' .conc hps
          refine ⟨by simp [hl], fun j m' aw dw h1 h2 => ?_⟩
          cases j with
          | zero =>
              simp at h1 h2; subst h1; subst h2
              exact imgShapeOf_sound (σ := σ) .conc hp
          | succ j => exact hs j m' aw dw (by simpa using h1) (by simpa using h2)
  | .cons h t, ms, ps, hr, hw => by
      simp only [imgsShapeOf, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at hw
      obtain ⟨p, hp, ps', hps, rfl⟩ := hw
      cases ms with
      | nil => obtain ⟨_, _, hc, _, _⟩ := SRel_cons_inv hr; cases hc
      | cons m ms =>
          obtain ⟨_, _, hc, r1, r2⟩ := SRel_cons_inv hr
          cases hc
          obtain ⟨hl, hs⟩ := imgsShapeOf_sound t ms ps' r2 hps
          refine ⟨by simp [hl], fun j m' aw dw h1 h2 => ?_⟩
          cases j with
          | zero =>
              simp at h1 h2; subst h1; subst h2
              exact imgShapeOf_sound r1 hp
          | succ j => exact hs j m' aw dw (by simpa using h1) (by simpa using h2)
  | .int _, _, _, _, h => by simp [imgsShapeOf] at h
  | .bool _, _, _, _, h => by simp [imgsShapeOf] at h
  | .bv _ _, _, _, _, h => by simp [imgsShapeOf] at h
  | .ctor _ _, _, _, _, h => by simp [imgsShapeOf] at h

/-- A state's shape. -/
def stateShapeOf (s : SVal) : Option Shape :=
  match s with
  | .ctor t [fl, ml] =>
      if t = StateM.tagStateM then do
        let fws ← widthsOf fl
        let mds ← imgsShapeOf ml
        pure ⟨fws, mds⟩
      else none
  | .conc (.ctor t [fl, ml]) =>
      if t = StateM.tagStateM then do
        let fws ← widthsOf (.conc fl)
        let mds ← imgsShapeOf (.conc ml)
        pure ⟨fws, mds⟩
      else none
  | _ => none

theorem stateShapeOf_sound {σ : Asg} {s : SVal} {st : StateM} {L : Shape}
    (hr : SRel σ s (StateM.encStateM st)) (h : stateShapeOf s = some L) : StMatches L st := by
  unfold StateM.encStateM at hr
  unfold stateShapeOf at h
  split at h
  · rename_i t fl ml
    split at h
    · simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at h
      obtain ⟨fws, hf, mds, hm, rfl⟩ := h
      obtain ⟨vs, he, rl⟩ := SRel_ctor_inv hr
      cases he
      obtain ⟨_, _, h1, r1, rl'⟩ := SRelL_cons_inv rl
      cases h1
      obtain ⟨_, _, h2, r2, _⟩ := SRelL_cons_inv rl'
      cases h2
      obtain ⟨hl, hs⟩ := imgsShapeOf_sound ml st.mems.toList mds r2 hm
      exact ⟨widthsOf_sound fl st.flops.toList fws r1 hf, by simpa using hl,
        fun j m aw dw h1 h2 => hs j m aw dw (by simpa using h1) h2⟩
    · cases h
  · rename_i t fl ml
    split at h
    · have := SRel_conc_inv hr
      cases this
      simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at h
      obtain ⟨fws, hf, mds, hm, rfl⟩ := h
      obtain ⟨hl, hs⟩ := imgsShapeOf_sound (σ := σ) _ st.mems.toList mds .conc hm
      exact ⟨widthsOf_sound (σ := σ) _ st.flops.toList fws .conc hf, by simpa using hl,
        fun j m aw dw h1 h2 => hs j m aw dw (by simpa using h1) h2⟩
    · cases h
  · cases h

end HDLProof.Sym
