/-
# B1: `SEval` is insensitive to swapping a function nothing calls

`PROMOTION_OBLIGATIONS.md` splits obligation B into B1 (a function-table
congruence), B2 (the restricted rewrite, for `main`'s actual body) and B3
(making `specializeDesign_correct` interpreter-parametric).  This file is B1
and nothing else.

`SEval (P : SProgram)` takes the program as a FIXED PARAMETER and uses it in
exactly one rule -- `call`, through `sFn P f`.  So a derivation cannot observe
any part of `P` it does not call, and replacing an uncalled function is
invisible to it.

`hwSVar` is `hwS` with `main`'s body replaced.  `main` is called by nothing in
`hwS` -- not by any of the other 39 functions, and not by itself -- so every
sub-derivation inside `main_agree` transports, and only the top `call` node
needs the rewrite (that is B2, not this file).

NOT PROVED HERE, and not claimed: the rewrite itself, the canonical witness,
and `MainFreeFuns hwS` as a closed theorem.  The last is carried as a premise;
a `#guard` checks it computationally, which is a check and not a proof.
-/
import LeanSemanticPrimitives.Projection.HardwareInterpreter
import LeanSemanticPrimitives.Projection.SurfaceSemantics
import LeanSemanticPrimitives.Projection.Proto.InterpreterVariant

namespace Projection
namespace ProtoVar

open Projection.Surface

/-! ## "This term never calls `main`" -/

mutual

def noMainCallB : SExp → Bool
  | .lit _        => true
  | .ref _        => true
  | .letN _ e b   => noMainCallB e && noMainCallB b
  | .ite c a b    => noMainCallB c && noMainCallB a && noMainCallB b
  | .prim _ es    => noMainCallL es
  | .mk _ es      => noMainCallL es
  | .switch s as  => noMainCallB s && noMainCallA as
  | .call f es    => (f != "main") && noMainCallL es

def noMainCallL : List SExp → Bool
  | []      => true
  | e :: es => noMainCallB e && noMainCallL es

def noMainCallA : List SAlt → Bool
  | []               => true
  | (_, _, b) :: as  => noMainCallB b && noMainCallA as

end

/-- Every function of `P` other than `main` has a `main`-free body. -/
def MainFreeFuns (P : SProgram) : Prop :=
  ∀ f fd, f ≠ "main" → sFn P f = some fd → noMainCallB fd.body = true

/-- The branch `sFindAlt` picks is one of the alternatives, so it inherits
`main`-freedom from the list. -/
theorem altBody_noMain : ∀ {as : List SAlt} {tag : Nat} {p : SAlt},
    noMainCallA as = true → sFindAlt as tag = some p → noMainCallB p.2.2 = true
  | [],              _, _, _,   h => by simp [sFindAlt] at h
  | (k, bs, b) :: r, tag, p, hn, h => by
      simp only [noMainCallA, Bool.and_eq_true] at hn
      simp only [sFindAlt] at h
      split at h
      · cases h; exact hn.1
      · exact altBody_noMain hn.2 h

/-! ## The congruence

By recursion on the derivation, in the same equation-compiler style
`SEval_sound` uses. -/

mutual

theorem SEval_congr {P Q : SProgram}
    (hag : ∀ f, f ≠ "main" → sFn Q f = sFn P f) (hmf : MainFreeFuns P) :
    ∀ (σ : SEnv) (e : SExp) (v : Val),
      noMainCallB e = true → SEval P σ e v → SEval Q σ e v
  | _, _, _, _, .lit          => .lit
  | _, _, _, _, .ref hx       => .ref hx
  | σ, _, v, hnm, .letN he hb => by
      simp only [noMainCallB, Bool.and_eq_true] at hnm
      exact .letN (SEval_congr hag hmf _ _ _ hnm.1 he)
                  (SEval_congr hag hmf _ _ _ hnm.2 hb)
  | σ, _, v, hnm, .iteT hc ha => by
      simp only [noMainCallB, Bool.and_eq_true] at hnm
      exact .iteT (SEval_congr hag hmf _ _ _ hnm.1.1 hc)
                  (SEval_congr hag hmf _ _ _ hnm.1.2 ha)
  | σ, _, v, hnm, .iteF hc hb => by
      simp only [noMainCallB, Bool.and_eq_true] at hnm
      exact .iteF (SEval_congr hag hmf _ _ _ hnm.1.1 hc)
                  (SEval_congr hag hmf _ _ _ hnm.2 hb)
  | σ, _, v, hnm, .prim hes hp => by
      simp only [noMainCallB] at hnm
      exact .prim (SEvalList_congr hag hmf _ _ _ hnm hes) hp
  | σ, _, _, hnm, .mk hes => by
      simp only [noMainCallB] at hnm
      exact SEval.mk (P := Q) (SEvalList_congr hag hmf _ _ _ hnm hes)
  | σ, _, _, hnm, .switch hs halt hlen hb => by
      simp only [noMainCallB, Bool.and_eq_true] at hnm
      exact .switch (SEval_congr hag hmf _ _ _ hnm.1 hs) halt hlen
                    (SEval_congr hag hmf _ _ _ (altBody_noMain hnm.2 halt) hb)
  | σ, _, _, hnm, .call hes hfn hlen hb => by
      -- `f` and `fd` are auto-bound implicits and cannot be named in the
      -- pattern; unification recovers them from `hnm.1` and `hfn` instead.
      simp only [noMainCallB, Bool.and_eq_true, bne_iff_ne, ne_eq] at hnm
      exact SEval.call (P := Q) (SEvalList_congr hag hmf _ _ _ hnm.2 hes)
        ((hag _ hnm.1).trans hfn) hlen
        (SEval_congr hag hmf _ _ _ (hmf _ _ hnm.1 hfn) hb)

theorem SEvalList_congr {P Q : SProgram}
    (hag : ∀ f, f ≠ "main" → sFn Q f = sFn P f) (hmf : MainFreeFuns P) :
    ∀ (σ : SEnv) (es : List SExp) (vs : List Val),
      noMainCallL es = true → SEvalList P σ es vs → SEvalList Q σ es vs
  | _, _, _, _, .nil => .nil
  | σ, _, _, hnm, .cons he hes => by
      simp only [noMainCallL, Bool.and_eq_true] at hnm
      exact .cons (SEval_congr hag hmf _ _ _ hnm.1 he)
                  (SEvalList_congr hag hmf _ _ _ hnm.2 hes)

end

/-! ## Instantiating B1 at `hwS` / `hwSVar`

`hwSVar` replaces exactly one element of `hwS.funs`, in place, keeping its
name.  So the NAME LIST is unchanged -- hence `idxOf` agrees -- and every
position except `main`'s holds the same `SFun`. -/

/-- The replacement preserves every name, so the name list is untouched. -/
theorem funNames_replace {P : SProgram} {g : SFun} (hg : g.name = "main") :
    funNames { P with funs := P.funs.map (fun h => if h.name == "main" then g else h) }
      = funNames P := by
  simp only [funNames, List.map_map]
  apply List.map_congr_left
  intro h _
  by_cases hn : h.name == "main"
  · simp only [Function.comp_apply, hn, if_true, hg]
    exact (beq_iff_eq.mp hn).symm
  · have hne' : ¬(h.name = "main") := fun hc => hn (beq_iff_eq.mpr hc)
    simp [Function.comp_apply, hne']

/-- `idxOf` finds a name at a position that really holds that name. -/
theorem idxOf_name : ∀ {fs : List SFun} {f : String} {i : Nat},
    idxOf (fs.map SFun.name) f = some i → ∃ fd, fs[i]? = some fd ∧ fd.name = f
  | [],      _, _, h => by simp [idxOf] at h
  | g :: gs, f, i, h => by
      by_cases hn : g.name == f
      · simp only [List.map_cons, idxOf, hn, if_true, Option.some.injEq] at h
        subst h
        exact ⟨g, rfl, beq_iff_eq.mp hn⟩
      · simp only [List.map_cons, idxOf, hn, if_false] at h
        obtain ⟨j, hj, rfl⟩ : ∃ j, idxOf (gs.map SFun.name) f = some j ∧ i = j + 1 := by
          cases hj : idxOf (gs.map SFun.name) f with
          | none   => simp [hj] at h
          | some j => exact ⟨j, rfl, by simpa [hj] using h.symm⟩
        obtain ⟨fd, hfd, hname⟩ := idxOf_name hj
        exact ⟨fd, by simpa using hfd, hname⟩

/-- Generalised: replacement by any NAME-PRESERVING function that is the
identity off `main`.  `hwSVar` replaces a fixed `SFun`; `hwSVarT` replaces the
BODY of whichever function is named `main`.  Both are instances. -/
theorem funNames_mapF {P : SProgram} {F : SFun → SFun}
    (hname : ∀ h, (F h).name = h.name) :
    funNames { P with funs := P.funs.map F } = funNames P := by
  simp only [funNames, List.map_map]
  exact List.map_congr_left (fun h _ => hname h)

theorem sFn_mapF {P : SProgram} {F : SFun → SFun}
    (hname : ∀ h, (F h).name = h.name)
    (hid : ∀ h, h.name ≠ "main" → F h = h)
    (f : String) (hne : f ≠ "main") :
    sFn { P with funs := P.funs.map F } f = sFn P f := by
  unfold sFn
  rw [funNames_mapF hname]
  cases hi : idxOf (funNames P) f with
  | none   => simp
  | some i =>
      obtain ⟨fd, hfd, hnm⟩ :=
        idxOf_name (fs := P.funs) (f := f) (i := i) (by simpa [funNames] using hi)
      have : F fd = fd := hid fd (by rw [hnm]; exact hne)
      simp only [Option.bind_some, List.getElem?_map, hfd, Option.map_some, this]

/-- B1's side condition, for any single in-place replacement of `main`. -/
theorem sFn_replace {P : SProgram} {g : SFun} (hg : g.name = "main")
    (f : String) (hne : f ≠ "main") :
    sFn { P with funs := P.funs.map (fun h => if h.name == "main" then g else h) } f
      = sFn P f := by
  unfold sFn
  rw [funNames_replace hg]
  cases hi : idxOf (funNames P) f with
  | none   => simp
  | some i =>
      obtain ⟨fd, hfd, hname⟩ :=
        idxOf_name (fs := P.funs) (f := f) (i := i) (by simpa [funNames] using hi)
      have hfm : (fd.name == "main") = false := by
        rw [hname]; exact beq_eq_false_iff_ne.mpr hne
      simp only [Option.bind_some, List.getElem?_map, hfd, Option.map_some, hfm]
      simp

/-! ## `MainFreeFuns`, discharged rather than assumed -/

def mainFreeB (P : SProgram) : Bool :=
  P.funs.all (fun fd => fd.name == "main" || noMainCallB fd.body)

/-- A `sFn` hit is a genuine member of the function list, carrying its name. -/
theorem sFn_mem {P : SProgram} {f : String} {fd : SFun} (h : sFn P f = some fd) :
    fd ∈ P.funs ∧ fd.name = f := by
  unfold sFn at h
  cases hi : idxOf (funNames P) f with
  | none   => rw [hi] at h; simp at h
  | some i =>
      rw [hi] at h
      simp only [Option.bind_some] at h
      obtain ⟨fd', hfd', hname⟩ :=
        idxOf_name (fs := P.funs) (f := f) (i := i) (by simpa [funNames] using hi)
      rw [hfd'] at h
      cases h
      exact ⟨List.mem_of_getElem? hfd', hname⟩

/-- The Bool check really does establish the predicate. -/
theorem mainFreeB_sound {P : SProgram} (h : mainFreeB P = true) : MainFreeFuns P := by
  intro f fd hne hfn
  obtain ⟨hmem, hname⟩ := sFn_mem hfn
  have := (List.all_eq_true.mp h) fd hmem
  simp only [Bool.or_eq_true, beq_iff_eq] at this
  rcases this with hm | hb
  · exact absurd (hname ▸ hm) (by simpa using hne)
  · exact hb

/-- KERNEL REDUCTION, not a `#guard`.  `#guard` runs the compiler's evaluator;
this is checked by the kernel, which is what a proof may rely on. -/
theorem mainFreeB_hwS : mainFreeB Hw.hwS = true := by rfl

theorem MainFreeFuns_hwS : MainFreeFuns Hw.hwS := mainFreeB_sound mainFreeB_hwS

/-! ## The ACTUAL `hwSVar`, not an arbitrary replacement

`sFn_replace` is stated for any in-place replacement of `main`.  `hwSVar` is a
SPECIFIC one, and it is built behind a `partial def` (`goInline`), which the
kernel cannot reduce -- so `ProtoVar.changed` is NOT decidable here.  It does
not need to be: the replacement's NAME does not depend on `goInline`'s result,
only its BODY does, so the side condition `sFn_replace` wants is available
unconditionally. -/

/-- Whatever `goInline` returns, the replaced function is still named `main`:
it is `{ f with body := … }` of the function `find?` selected by name. -/
theorem rewritten_name : ∀ p, rewritten = some p → p.1.name = "main" := by
  intro p hp
  unfold rewritten at hp
  cases hf : Hw.hwS.funs.find? (fun f => f.name == "main") with
  | none   => simp only [hf] at hp; simp at hp
  | some f =>
      simp only [hf] at hp
      cases hg : goInline "env0" f.body with
      | none   => simp only [hg] at hp; simp at hp
      | some q =>
          simp only [hg, Option.map_some, Option.some.injEq] at hp
          subst hp
          show f.name = "main"
          have hb := List.find?_some hf
          simp only [beq_iff_eq] at hb
          exact hb

/-- B1's side condition, for the real variant. -/
theorem sFn_hwSVar (f : String) (hne : f ≠ "main") : sFn hwSVar f = sFn Hw.hwS f := by
  unfold hwSVar
  cases hp : rewritten with
  | none   => simp only [hp]
  | some p =>
      simp only [hp]
      by_cases hok : p.2.ok = true
      · rw [if_pos hok]
        exact sFn_replace (rewritten_name p hp) f hne
      · rw [if_neg hok]

/-! ## The corollary B2 will use

NOTE what this does NOT cover: a term that calls `main`.  `noMainCallB`
excludes it by construction, and that is deliberate -- the top `call "main"`
node is exactly what the rewrite changes, so B2 must build that node itself
from the transported sub-derivation.  Transporting it with this corollary would
be assuming what B2 has to prove. -/

theorem SEval_hwSVar_of_hwS {σ : SEnv} {e : SExp} {v : Val}
    (hnm : noMainCallB e = true) (h : SEval Hw.hwS σ e v) : SEval hwSVar σ e v :=
  SEval_congr (fun f hne => sFn_hwSVar f hne) MainFreeFuns_hwS _ _ _ hnm h

theorem SEvalList_hwSVar_of_hwS {σ : SEnv} {es : List SExp} {vs : List Val}
    (hnm : noMainCallL es = true) (h : SEvalList Hw.hwS σ es vs) :
    SEvalList hwSVar σ es vs :=
  SEvalList_congr (fun f hne => sFn_hwSVar f hne) MainFreeFuns_hwS _ _ _ hnm h

end ProtoVar
end Projection
