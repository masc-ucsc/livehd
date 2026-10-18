/-
# The memory route with the node fold FUSED into its readers

After `MemVariant` inlined `env0`, `mainM` still binds the folded environment:

    letN "env" (evalNodesM nodes (mkSourcesM …) nsrc)
      (letN "os" (mkOutputs outs env nall) (letN "nf" (flopNexts … env …) …))

The let RESIDUALIZES the fold's result, so the slot list `mkOutputs`,
`flopNexts` and `memNexts` read is a residual cons chain, and each of their
reads walks it with `tl`s: about (flops + outputs) x slots terms. B05's
variant residual is 6.1M terms with 1.96M `tl`, against 558 `hd`. B08's is
175M.

`hwMemSF` moves those three readers INTO the fold's base case:

    evalNodesThenM nodes env n nall outs flops edges fq mems ms
      = if nodes = [] then finishM env nall outs flops edges fq mems ms
        else evalNodesThenM (tl nodes) (cons (evalNodeM (hd nodes) env n) env) (n+1) …

so they read the environment while it is still the fold's static spine.
B05: 6.1M -> 192k terms. B08: 175M -> 17.7M. B01: 37M -> 3.3M.

Proved here, without touching the memory route's adequacy chain:

* `fold_fuse`: a successful `evalNodesM` fold followed by a successful
  `finishM` is a successful `evalNodesThenM`. By recursion on the fold's
  derivation.
* `mainM_transferF`: every successful `mainM` call of `hwMemS` is one of
  `hwMemSF`, with the same value.
* `memF_iff(W)`, `projectMemFDesignAt(W)_correct`, `runMemSim(W)F_correct`:
  the same conclusions as the original route's.
-/
import LeanSemanticPrimitives.Projection.MemVariant

namespace Projection
namespace MemFused

open Surface Compiler Projection.Hw Projection.HwMem Projection.MemProjection
open Projection.MemVariant Projection.ProtoVar

def R' (x : String) : SExp := .ref x
def C' (f : String) (as : List SExp) : SExp := .call f as
def P' (p : Prim) (as : List SExp) : SExp := .prim p as

/-! ## 1. The original pieces, restated and pinned by the kernel -/

def evalNodesBody : SExp :=
  .ite (P' .isNil [R' "nodes"]) (R' "env")
    (C' "evalNodesM" [P' .tl [R' "nodes"],
                      cons (C' "evalNodeM" [P' .hd [R' "nodes"], R' "env", R' "n"]) (R' "env"),
                      P' .addI [R' "n", int 1]])

def finishBinds : List (String × SExp) :=
  [ ("os", C' "mkOutputs" [R' "outs", R' "env", R' "nall"])
  , ("nf", C' "flopNexts" [R' "flops", R' "edges", R' "env", R' "nall", R' "fq", int 0])
  , ("nm", C' "memNexts" [R' "mems", R' "edges", R' "env", R' "nall", R' "ms", int 0]) ]

def resultE : SExp := .mk StateM.tagResultM [.mk StateM.tagStateM [R' "nf", R' "nm"], R' "os"]

def finishBody : SExp := lets finishBinds resultE

def headBinds : List (String × SExp) :=
  [ ("nsrc", C' "lenL" [R' "srcs"]), ("nnod", C' "lenL" [R' "nodes"])
  , ("nall", P' .addI [R' "nsrc", R' "nnod"]) ]

def sourcesE : SExp := C' "mkSourcesM" [R' "srcs", R' "inp", R' "fq", R' "ms", nil]

def mainMBodyOf (inner : SExp) : SExp :=
  .switch (R' "d") [(tagDesign, ["srcs", "nodes", "outs", "flops", "mems", "clks"],
    .switch (R' "st") [(StateM.tagStateM, ["fq", "ms"], lets headBinds inner)])]

/-- The original `mainM` body. -/
def mainMOrigInner : SExp :=
  .letN "env0" sourcesE
    (.letN "env" (C' "evalNodesM" [R' "nodes", R' "env0", R' "nsrc"]) finishBody)

set_option maxRecDepth 100000 in
theorem sFn_evalNodesM : sFn hwMemS "evalNodesM"
    = some { name := "evalNodesM", params := ["nodes", "env", "n"], inline := true
           , body := evalNodesBody } := by rfl

set_option maxRecDepth 100000 in
theorem sFn_mainM : sFn hwMemS "mainM"
    = some { name := "mainM", params := ["d", "edges", "inp", "st"], inline := false
           , body := mainMBodyOf mainMOrigInner } := by rfl

/-! ## 2. The fused program -/

def fFinishM : SFun :=
  { name := "finishM", params := ["env", "nall", "outs", "flops", "edges", "fq", "mems", "ms"]
  , inline := true, body := finishBody }

def thenArgsTail : List SExp :=
  [R' "nall", R' "outs", R' "flops", R' "edges", R' "fq", R' "mems", R' "ms"]

def thenBody : SExp :=
  .ite (P' .isNil [R' "nodes"])
    (C' "finishM" (R' "env" :: thenArgsTail))
    (C' "evalNodesThenM" ([P' .tl [R' "nodes"],
        cons (C' "evalNodeM" [P' .hd [R' "nodes"], R' "env", R' "n"]) (R' "env"),
        P' .addI [R' "n", int 1]] ++ thenArgsTail))

def fEvalNodesThenM : SFun :=
  { name := "evalNodesThenM"
  , params := ["nodes", "env", "n", "nall", "outs", "flops", "edges", "fq", "mems", "ms"]
  , inline := true, body := thenBody }

def mainMFusedInner : SExp :=
  C' "evalNodesThenM" ([R' "nodes", sourcesE, R' "nsrc"] ++ thenArgsTail)

def fMainMF : SFun :=
  { name := "mainM", params := ["d", "edges", "inp", "st"], inline := false
  , body := mainMBodyOf mainMFusedInner }

def swapF (g : SFun) : SFun := if g.name == "mainM" then fMainMF else g

def hwMemSF : SProgram :=
  { hwMemS with funs := hwMemS.funs.map swapF ++ [fFinishM, fEvalNodesThenM] }

/-! ## 3. Function lookup in the fused program -/

theorem idxOf_append_of {l m : List String} {f : String} {i : Nat} :
    idxOf l f = some i → idxOf (l ++ m) f = some i := by
  induction l generalizing i with
  | nil => simp [idxOf]
  | cons x xs ih =>
      intro h
      simp only [idxOf, List.cons_append] at h ⊢
      split at h
      · rw [if_pos ‹_›]; exact h
      · rw [if_neg ‹_›]
        cases hx : idxOf xs f with
        | none => rw [hx] at h; simp at h
        | some j =>
            rw [hx] at h
            simp only [Option.map_some, Option.some.injEq] at h
            rw [ih hx]; simp [h]

theorem swapF_name (g : SFun) : (swapF g).name = g.name := by
  unfold swapF; split
  · rename_i h; simp only [beq_iff_eq] at h; rw [h]; rfl
  · rfl

/-- Every function of `hwMemS` other than `mainM` is found, unchanged, in `hwMemSF`. -/
theorem sFn_hwMemSF_old {f : String} {fd : SFun} (hne : f ≠ "mainM")
    (h : sFn hwMemS f = some fd) : sFn hwMemSF f = some fd := by
  unfold sFn at h ⊢
  cases hi : idxOf (funNames hwMemS) f with
  | none => rw [hi] at h; simp at h
  | some i =>
    rw [hi] at h
    simp only [Option.bind_some] at h
    have hnames : funNames hwMemSF = funNames hwMemS ++ ["finishM", "evalNodesThenM"] := by
      simp only [funNames, hwMemSF, List.map_append, List.map_map]
      congr 1
    rw [hnames, idxOf_append_of hi]
    simp only [Option.bind_some, hwMemSF]
    have hlt : i < hwMemS.funs.length := by
      rcases Nat.lt_or_ge i hwMemS.funs.length with h1 | h1
      · exact h1
      · rw [List.getElem?_eq_none h1] at h; cases h
    rw [List.getElem?_append_left (by simpa using hlt), List.getElem?_map, h]
    obtain ⟨-, hname⟩ := sFn_mem (P := hwMemS) (by unfold sFn; rw [hi]; exact h)
    simp only [Option.map_some, Option.some.injEq]
    unfold swapF
    have : (fd.name == "mainM") = false := by rw [hname]; exact beq_eq_false_iff_ne.mpr hne
    rw [this]; rfl

set_option maxRecDepth 100000 in
theorem sFn_hwMemSF_mainM : sFn hwMemSF "mainM" = some fMainMF := by rfl

set_option maxRecDepth 100000 in
theorem sFn_hwMemSF_finish : sFn hwMemSF "finishM" = some fFinishM := by rfl

set_option maxRecDepth 100000 in
theorem sFn_hwMemSF_then : sFn hwMemSF "evalNodesThenM" = some fEvalNodesThenM := by rfl

/-! ## 4. Moving derivations -/

/-! The congruence, needing only that every function `P` calls (other than
`m`) is found unchanged in `Q` -- `Q` may have MORE functions. -/

mutual

theorem SEval_congrW {P Q : SProgram} {m : String}
    (hag : ∀ f fd, f ≠ m → sFn P f = some fd → sFn Q f = some fd) (hmf : FreeOf P m) :
    ∀ (σ : SEnv) (e : SExp) (v : Val),
      noCallB m e = true → SEval P σ e v → SEval Q σ e v
  | _, _, _, _, .lit          => .lit
  | _, _, _, _, .ref hx       => .ref hx
  | σ, _, v, hnm, .letN he hb => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .letN (SEval_congrW hag hmf _ _ _ hnm.1 he)
                  (SEval_congrW hag hmf _ _ _ hnm.2 hb)
  | σ, _, v, hnm, .iteT hc ha => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .iteT (SEval_congrW hag hmf _ _ _ hnm.1.1 hc)
                  (SEval_congrW hag hmf _ _ _ hnm.1.2 ha)
  | σ, _, v, hnm, .iteF hc hb => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .iteF (SEval_congrW hag hmf _ _ _ hnm.1.1 hc)
                  (SEval_congrW hag hmf _ _ _ hnm.2 hb)
  | σ, _, v, hnm, .prim hes hp => by
      simp only [noCallB] at hnm
      exact .prim (SEvalList_congrW hag hmf _ _ _ hnm hes) hp
  | σ, _, _, hnm, .mk hes => by
      simp only [noCallB] at hnm
      exact SEval.mk (P := Q) (SEvalList_congrW hag hmf _ _ _ hnm hes)
  | σ, _, _, hnm, .switch hs halt hlen hb => by
      simp only [noCallB, Bool.and_eq_true] at hnm
      exact .switch (SEval_congrW hag hmf _ _ _ hnm.1 hs) halt hlen
                    (SEval_congrW hag hmf _ _ _ (altBody_noCall hnm.2 halt) hb)
  | σ, _, _, hnm, .call hes hfn hlen hb => by
      simp only [noCallB, Bool.and_eq_true, bne_iff_ne, ne_eq] at hnm
      exact SEval.call (P := Q) (SEvalList_congrW hag hmf _ _ _ hnm.2 hes)
        (hag _ _ hnm.1 hfn) hlen
        (SEval_congrW hag hmf _ _ _ (hmf _ _ hnm.1 hfn) hb)

theorem SEvalList_congrW {P Q : SProgram} {m : String}
    (hag : ∀ f fd, f ≠ m → sFn P f = some fd → sFn Q f = some fd) (hmf : FreeOf P m) :
    ∀ (σ : SEnv) (es : List SExp) (vs : List Val),
      noCallL m es = true → SEvalList P σ es vs → SEvalList Q σ es vs
  | _, _, _, _, .nil => .nil
  | σ, _, _, hnm, .cons he hes => by
      simp only [noCallL, Bool.and_eq_true] at hnm
      exact .cons (SEval_congrW hag hmf _ _ _ hnm.1 he)
                  (SEvalList_congrW hag hmf _ _ _ hnm.2 hes)

end

/-- `hwMemS` to `hwMemSF`, for anything that does not call `mainM`. -/
theorem toF {σ : SEnv} {e : SExp} {v : Val} (hnc : noCallB "mainM" e = true)
    (h : SEval hwMemS σ e v) : SEval hwMemSF σ e v :=
  SEval_congrW (fun _ _ hne hf => sFn_hwMemSF_old hne hf) (freeOfB_sound freeOfB_hwMemS) _ _ _ hnc h

theorem toFL {σ : SEnv} {es : List SExp} {vs : List Val} (hnc : noCallL "mainM" es = true)
    (h : SEvalList hwMemS σ es vs) : SEvalList hwMemSF σ es vs :=
  SEvalList_congrW (fun _ _ hne hf => sFn_hwMemSF_old hne hf) (freeOfB_sound freeOfB_hwMemS) _ _ _ hnc h

/-- Two environments that agree on every name `b` reads evaluate `b` alike. -/
def FreeAgree (σ1 σ2 : SEnv) (n : String → Nat) : Prop := ∀ y, n y ≠ 0 → slookup σ1 y = slookup σ2 y

theorem FreeAgree.mono {σ1 σ2 : SEnv} {n m : String → Nat} (h : FreeAgree σ1 σ2 n)
    (hle : ∀ y, m y ≤ n y) : FreeAgree σ1 σ2 m :=
  fun y hy => h y (by have := hle y; omega)

theorem ref_free {σ1 σ2 : SEnv} {y : String}
    (h : FreeAgree σ1 σ2 (fun z => countRefT z (.ref y))) : slookup σ1 y = slookup σ2 y :=
  h y (by simp [countRefT])

theorem letN_free {σ1 σ2 : SEnv} {y : String} {e b : SExp}
    (h : FreeAgree σ1 σ2 (fun z => countRefT z (.letN y e b))) (d : Val) :
    FreeAgree σ1 σ2 (fun z => countRefT z e)
      ∧ FreeAgree ((y, d) :: σ1) ((y, d) :: σ2) (fun z => countRefT z b) := by
  refine ⟨h.mono (fun z => by simp only [countRefT]; omega), fun z hz => ?_⟩
  by_cases hyz : y = z
  · subst hyz; simp [slookup]
  · rw [slookup_cons_ne hyz, slookup_cons_ne hyz]
    apply h z
    have hzy : (y == z) = false := beq_eq_false_iff_ne.mpr hyz
    simp only at hz
    show countRefT z (.letN y e b) ≠ 0
    have hc : countRefT z (.letN y e b) = countRefT z e + countRefT z b := by
      simp only [countRefT]
      have : (y == z) = false := hzy
      split <;> simp_all
    omega

theorem switch_free {σ1 σ2 : SEnv} {s : SExp} {as : List SAlt} {tag : Nat}
    {bs : List String} {body : SExp} {vs : List Val}
    (h : FreeAgree σ1 σ2 (fun z => countRefT z (.switch s as)))
    (halt : sFindAlt as tag = some (tag, bs, body)) (hlen : bs.length = vs.length) :
    FreeAgree σ1 σ2 (fun z => countRefT z s)
      ∧ FreeAgree (bs.zip vs ++ σ1) (bs.zip vs ++ σ2) (fun z => countRefT z body) := by
  refine ⟨h.mono (fun z => by simp only [countRefT]; omega), fun z hz => ?_⟩
  cases hc : bs.contains z with
  | true =>
      obtain ⟨w, hw⟩ := slookup_zip_some bs vs z hlen hc
      rw [slookup_append, slookup_append, hw]; rfl
  | false =>
      rw [slookup_append, slookup_append, slookup_zip_none bs vs z hc]
      simp only [Option.none_or]
      apply h z
      obtain ⟨k, hk⟩ := countRefA_found (x := z) halt
      rw [hc] at hk
      simp only [Bool.false_eq_true, if_false] at hk
      simp only at hz
      simp only [countRefT]; omega

mutual

theorem agree_sound {P : SProgram} : ∀ (σ1 σ2 : SEnv) (b : SExp) (v : Val),
    FreeAgree σ1 σ2 (fun y => countRefT y b) → SEval P σ1 b v → SEval P σ2 b v
  | _,  _,  _, _, _,   .lit => .lit
  | _,  _,  _, _, hag, .ref hy => .ref ((ref_free hag) ▸ hy)
  | _,  _,  _, _, hag, .letN he hb => by
      obtain ⟨h1, h2⟩ := letN_free hag _
      exact .letN (agree_sound _ _ _ _ h1 he) (agree_sound _ _ _ _ h2 hb)
  | _,  _,  _, _, hag, .iteT hc ha =>
      .iteT (agree_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefT]; omega)) hc)
            (agree_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefT]; omega)) ha)
  | _,  _,  _, _, hag, .iteF hc hb =>
      .iteF (agree_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefT]; omega)) hc)
            (agree_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefT]; omega)) hb)
  | _,  _,  _, _, hag, .prim hes hp =>
      .prim (agreeL_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefT]; omega)) hes) hp
  | _,  _,  _, _, hag, .mk hes =>
      .mk (agreeL_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefT]; omega)) hes)
  | _,  _,  _, _, hag, .switch hs halt hlen hb => by
      obtain ⟨h1, h2⟩ := switch_free hag halt hlen
      exact .switch (agree_sound _ _ _ _ h1 hs) halt hlen (agree_sound _ _ _ _ h2 hb)
  | _,  _,  _, _, hag, .call hes hfn hlen hb =>
      .call (agreeL_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefT]; omega)) hes) hfn hlen hb

theorem agreeL_sound {P : SProgram} : ∀ (σ1 σ2 : SEnv) (es : List SExp) (vs : List Val),
    FreeAgree σ1 σ2 (fun y => countRefL y es) → SEvalList P σ1 es vs → SEvalList P σ2 es vs
  | _, _, _, _, _,   .nil => .nil
  | _, _, _, _, hag, .cons he hes =>
      .cons (agree_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefL]; omega)) he)
            (agreeL_sound _ _ _ _ (hag.mono (fun z => by simp only [countRefL]; omega)) hes)

end

/-! Extending an environment at the END changes no successful evaluation: every
lookup the derivation makes already succeeds in the front part. -/

mutual

theorem SEval_append {P : SProgram} (τ : SEnv) : ∀ (σ : SEnv) (e : SExp) (v : Val),
    SEval P σ e v → SEval P (σ ++ τ) e v
  | _, _, _, .lit => .lit
  | σ, _, _, .ref hy => .ref (by rw [slookup_append, hy]; rfl)
  | σ, _, _, .letN he hb => .letN (SEval_append τ _ _ _ he) (SEval_append τ _ _ _ hb)
  | σ, _, _, .iteT hc ha => .iteT (SEval_append τ _ _ _ hc) (SEval_append τ _ _ _ ha)
  | σ, _, _, .iteF hc hb => .iteF (SEval_append τ _ _ _ hc) (SEval_append τ _ _ _ hb)
  | σ, _, _, .prim hes hp => .prim (SEvalList_append τ _ _ _ hes) hp
  | σ, _, _, .mk hes => .mk (SEvalList_append τ _ _ _ hes)
  | σ, _, _, .switch hs halt hlen hb =>
      .switch (SEval_append τ _ _ _ hs) halt hlen
        (by have := SEval_append τ _ _ _ hb; rwa [List.append_assoc] at this)
  | σ, _, _, .call hes hfn hlen hb => .call (SEvalList_append τ _ _ _ hes) hfn hlen hb

theorem SEvalList_append {P : SProgram} (τ : SEnv) : ∀ (σ : SEnv) (es : List SExp) (vs : List Val),
    SEvalList P σ es vs → SEvalList P (σ ++ τ) es vs
  | _, _, _, .nil => .nil
  | _, _, _, .cons he hes => .cons (SEval_append τ _ _ _ he) (SEvalList_append τ _ _ _ hes)

end


theorem SEvalList_appendL {P : SProgram} {σ : SEnv} : ∀ {es1 es2 : List SExp} {vs1 vs2 : List Val},
    SEvalList P σ es1 vs1 → SEvalList P σ es2 vs2 → SEvalList P σ (es1 ++ es2) (vs1 ++ vs2)
  | [], _, [], _, .nil, h2 => h2
  | _ :: _, _, _ :: _, _, .cons h hs, h2 => .cons h (SEvalList_appendL hs h2)

/-! ## 5. The fold, fused -/

def finishParams : List String := ["env", "nall", "outs", "flops", "edges", "fq", "mems", "ms"]

def tailEnv (x0 x1 x2 x3 x4 x5 x6 : Val) : SEnv :=
  [("nall", x0), ("outs", x1), ("flops", x2), ("edges", x3), ("fq", x4), ("mems", x5), ("ms", x6)]
def thenParams : List String := ["nodes", "env", "n", "nall", "outs", "flops", "edges", "fq", "mems", "ms"]

theorem thenTail_eval (vn ve vi x0 x1 x2 x3 x4 x5 x6 : Val) :
    SEvalList hwMemSF (thenParams.zip [vn, ve, vi, x0, x1, x2, x3, x4, x5, x6])
      thenArgsTail [x0, x1, x2, x3, x4, x5, x6] := by
  unfold thenArgsTail R'
  exact .cons (.ref rfl) (.cons (.ref rfl) (.cons (.ref rfl) (.cons (.ref rfl)
    (.cons (.ref rfl) (.cons (.ref rfl) (.cons (.ref rfl) .nil))))))

theorem thenEnv_eq (vn ve vi x0 x1 x2 x3 x4 x5 x6 : Val) :
    thenParams.zip [vn, ve, vi, x0, x1, x2, x3, x4, x5, x6]
      = [("nodes", vn), ("env", ve), ("n", vi)] ++ tailEnv x0 x1 x2 x3 x4 x5 x6 := rfl

theorem len3 {α : Type} : ∀ {l : List α}, l.length = 3 → ∃ a b c, l = [a, b, c]
  | [a, b, c], _ => ⟨a, b, c, rfl⟩
  | [], h | [_], h | [_, _], h | _ :: _ :: _ :: _ :: _, h => by simp at h

/-- **Fusion.** A successful `evalNodesM` fold from `(vn, ve, vi)` to `dF`, and a
successful `finishM` on `dF`, make a successful `evalNodesThenM` from
`(vn, ve, vi)` with the same answer. By recursion on the fold's derivation. -/
theorem fold_fuse_aux : ∀ {σ : SEnv} {b : SExp} {dF : Val}, SEval hwMemS σ b dF →
    ∀ {vn ve vi : Val}, σ = [("nodes", vn), ("env", ve), ("n", vi)] → b = evalNodesBody →
    ∀ {x0 x1 x2 x3 x4 x5 x6 v : Val},
      SEval hwMemSF (finishParams.zip [dF, x0, x1, x2, x3, x4, x5, x6]) finishBody v →
      SEval hwMemSF (thenParams.zip [vn, ve, vi, x0, x1, x2, x3, x4, x5, x6]) thenBody v
  | _, _, _, .iteT hc ha, vn, ve, vi, hσ, hb, x0, x1, x2, x3, x4, x5, x6, v, hfin => by
      simp only [evalNodesBody, SExp.ite.injEq] at hb
      obtain ⟨rfl, rfl, rfl⟩ := hb
      subst hσ
      cases ha with
      | ref hy =>
        simp [slookup] at hy
        subst hy
        unfold thenBody
        refine .iteT ?_ ?_
        · rw [thenEnv_eq]; exact toF (by rfl) (SEval_append (tailEnv x0 x1 x2 x3 x4 x5 x6) _ _ _ hc)
        · exact .call (.cons (.ref rfl) (thenTail_eval _ _ _ x0 x1 x2 x3 x4 x5 x6))
            sFn_hwMemSF_finish rfl hfin
  | _, _, _, .iteF hc hbr, vn, ve, vi, hσ, hb, x0, x1, x2, x3, x4, x5, x6, v, hfin =>
      match hbr with
      | .call hes hfn hlen hbody =>
          have ih0 := fun (a b c : Val) h1 h2 (y0 y1 y2 y3 y4 y5 y6 w : Val) h3 =>
            fold_fuse_aux (vn := a) (ve := b) (vi := c) (x0 := y0) (x1 := y1) (x2 := y2) (x3 := y3)
              (x4 := y4) (x5 := y5) (x6 := y6) (v := w) hbody h1 h2 h3
          by
          simp only [evalNodesBody, SExp.ite.injEq, C', SExp.call.injEq] at hb
          obtain ⟨rfl, rfl, rfl, rfl⟩ := hb
          subst hσ
          rw [sFn_evalNodesM] at hfn
          cases hfn
          rename_i vs'
          have hl3 : vs'.length = 3 := by simpa using hlen.symm
          obtain ⟨vn', ve', vi', rfl⟩ := len3 hl3
          · have ih := ih0 vn' ve' vi' rfl rfl x0 x1 x2 x3 x4 x5 x6 v hfin
            unfold thenBody
            refine .iteF ?_ ?_
            · rw [thenEnv_eq]; exact toF (by rfl) (SEval_append (tailEnv x0 x1 x2 x3 x4 x5 x6) _ _ _ hc)
            · have h3 := toFL (by rfl) (SEvalList_append (tailEnv x0 x1 x2 x3 x4 x5 x6) _ _ _ hes)
              rw [← thenEnv_eq] at h3
              exact .call (SEvalList_appendL h3 (thenTail_eval _ _ _ x0 x1 x2 x3 x4 x5 x6))
                sFn_hwMemSF_then rfl ih
      | .lit => by simp [evalNodesBody, C'] at hb
      | .ref _ => by simp [evalNodesBody, C'] at hb
      | .letN _ _ => by simp [evalNodesBody, C'] at hb
      | .iteT _ _ => by simp [evalNodesBody, C'] at hb
      | .iteF _ _ => by simp [evalNodesBody, C'] at hb
      | .prim _ _ => by simp [evalNodesBody, C', P'] at hb
      | .mk _ => by simp [evalNodesBody, C'] at hb
      | .switch _ _ _ _ => by simp [evalNodesBody, C'] at hb
  | _, _, _, .lit, _, _, _, _, hb, _, _, _, _, _, _, _, _, _ => by simp [evalNodesBody] at hb
  | _, _, _, .ref _, _, _, _, _, hb, _, _, _, _, _, _, _, _, _ => by simp [evalNodesBody] at hb
  | _, _, _, .letN _ _, _, _, _, _, hb, _, _, _, _, _, _, _, _, _ => by simp [evalNodesBody] at hb
  | _, _, _, .prim _ _, _, _, _, _, hb, _, _, _, _, _, _, _, _, _ => by simp [evalNodesBody] at hb
  | _, _, _, .mk _, _, _, _, _, hb, _, _, _, _, _, _, _, _, _ => by simp [evalNodesBody] at hb
  | _, _, _, .switch _ _ _ _, _, _, _, _, hb, _, _, _, _, _, _, _, _, _ => by simp [evalNodesBody] at hb
  | _, _, _, .call _ _ _ _, _, _, _, _, hb, _, _, _, _, _, _, _, _, _ => by simp [evalNodesBody] at hb

theorem fold_fuse {vn ve vi dF : Val}
    (h : SEval hwMemS [("nodes", vn), ("env", ve), ("n", vi)] evalNodesBody dF)
    {x0 x1 x2 x3 x4 x5 x6 v : Val}
    (hfin : SEval hwMemSF (finishParams.zip [dF, x0, x1, x2, x3, x4, x5, x6]) finishBody v) :
    SEval hwMemSF (thenParams.zip [vn, ve, vi, x0, x1, x2, x3, x4, x5, x6]) thenBody v :=
  fold_fuse_aux h rfl rfl hfin

/-! ## 6. `mainM`, transferred -/

theorem len_eq {α : Type} {n : Nat} {l : List α} (h : l.length = n) : l.length = n := h

theorem len2 {α : Type} : ∀ {l : List α}, l.length = 2 → ∃ a b, l = [a, b]
  | [a, b], _ => ⟨a, b, rfl⟩
  | [], h | [_], h | _ :: _ :: _ :: _, h => by simp at h

theorem len4 {α : Type} : ∀ {l : List α}, l.length = 4 → ∃ a b c d, l = [a, b, c, d]
  | [a, b, c, d], _ => ⟨a, b, c, d, rfl⟩
  | [], h | [_], h | [_, _], h | [_, _, _], h | _ :: _ :: _ :: _ :: _ :: _, h => by simp at h

theorem len6 {α : Type} : ∀ {l : List α}, l.length = 6 → ∃ a b c d e f, l = [a, b, c, d, e, f]
  | [a, b, c, d, e, f], _ => ⟨a, b, c, d, e, f, rfl⟩
  | [], h | [_], h | [_, _], h | [_, _, _], h | [_, _, _, _], h | [_, _, _, _, _], h
  | _ :: _ :: _ :: _ :: _ :: _ :: _ :: _, h => by simp at h

/-- The names `finishBody` reads. -/
theorem finish_names (y : String) (h : countRefT y finishBody ≠ 0) :
    y = "env" ∨ y = "nall" ∨ y = "outs" ∨ y = "flops" ∨ y = "edges" ∨ y = "fq"
      ∨ y = "mems" ∨ y = "ms" := by
  by_cases h1 : y = "env"; · exact Or.inl h1
  by_cases h2 : y = "nall"; · exact Or.inr (Or.inl h2)
  by_cases h3 : y = "outs"; · exact Or.inr (Or.inr (Or.inl h3))
  by_cases h4 : y = "flops"; · exact Or.inr (Or.inr (Or.inr (Or.inl h4)))
  by_cases h5 : y = "edges"; · exact Or.inr (Or.inr (Or.inr (Or.inr (Or.inl h5))))
  by_cases h6 : y = "fq"; · exact Or.inr (Or.inr (Or.inr (Or.inr (Or.inr (Or.inl h6)))))
  by_cases h7 : y = "mems"; · exact Or.inr (Or.inr (Or.inr (Or.inr (Or.inr (Or.inr (Or.inl h7))))))
  by_cases h8 : y = "ms"; · exact Or.inr (Or.inr (Or.inr (Or.inr (Or.inr (Or.inr (Or.inr h8))))))
  exfalso; apply h
  have e : ∀ s : String, s ≠ y → (s == y) = false := fun s hs => beq_eq_false_iff_ne.mpr hs
  simp only [finishBody, finishBinds, resultE, lets, countRefT, countRefL, R', C', int,
    e "env" (Ne.symm h1), e "nall" (Ne.symm h2), e "outs" (Ne.symm h3), e "flops" (Ne.symm h4),
    e "edges" (Ne.symm h5), e "fq" (Ne.symm h6), e "mems" (Ne.symm h7), e "ms" (Ne.symm h8)]
  repeat' split
  all_goals simp_all

set_option maxRecDepth 10000 in
theorem body_transferF {a b c d v : Val}
    (h : SEval hwMemS [("d", a), ("edges", b), ("inp", c), ("st", d)] (mainMBodyOf mainMOrigInner) v) :
    SEval hwMemSF [("d", a), ("edges", b), ("inp", c), ("st", d)] (mainMBodyOf mainMFusedInner) v := by
  unfold mainMBodyOf at h ⊢
  cases h with
  | switch hs halt hlen hb =>
    cases hs with
    | ref hy =>
      simp only [slookup] at hy
      simp only [sFindAlt] at halt
      split at halt
      · rename_i htag
        simp only [Option.some.injEq, Prod.mk.injEq] at halt
        obtain ⟨-, rfl, rfl⟩ := halt
        obtain ⟨srcs, nodes, outs, flops, mems, clks, rfl⟩ := len6 (by simpa using hlen.symm)
        refine SEval_switch_of_tag (body := (R' "st").switch
            [(StateM.tagStateM, ["fq", "ms"], lets headBinds mainMFusedInner)])
          (.ref (by simpa [slookup] using hy)) (by simp [sFindAlt, htag]) hlen ?_
        cases hb with
        | switch hs2 halt2 hlen2 hb2 =>
          cases hs2 with
          | ref hy2 =>
            simp [slookup] at hy2
            simp only [sFindAlt] at halt2
            split at halt2
            · rename_i htag2
              simp only [Option.some.injEq, Prod.mk.injEq] at halt2
              obtain ⟨-, rfl, rfl⟩ := halt2
              obtain ⟨fq, ms, rfl⟩ := len2 (by simpa using hlen2.symm)
              refine SEval_switch_of_tag (body := lets headBinds mainMFusedInner)
                (.ref (by simpa [slookup] using hy2)) (by simp [sFindAlt, htag2]) hlen2 ?_
              simp only [lets, headBinds] at hb2 ⊢
              cases hb2 with
              | letN he1 hb3 =>
                cases hb3 with
                | letN he2 hb4 =>
                  cases hb4 with
                  | letN he3 hb5 =>
                    rename_i d1 d2 d3
                    refine .letN (toF (by rfl) he1) (.letN (toF (by rfl) he2) (.letN (toF (by rfl) he3) ?_))
                    unfold mainMOrigInner at hb5
                    cases hb5 with
                    | letN hsrc hrest =>
                      rename_i d0
                      cases hrest with
                      | letN hev hfin =>
                        rename_i dF
                        cases hev with
                        | call hes hfn hlen3 hbody =>
                          rw [sFn_evalNodesM] at hfn
                          cases hfn
                          rename_i vs3
                          obtain ⟨u1, u2, u3, rfl⟩ := len3 (by simpa using hlen3.symm)
                          -- the fold's three arguments, read back
                          cases hes with
                          | cons hn hes' =>
                            cases hes' with
                            | cons he0 hes'' =>
                              cases hes'' with
                              | cons hns hnil =>
                                cases hnil
                                cases hn with | ref hn' => ?_
                                cases he0 with | ref he0' => ?_
                                cases hns with | ref hns' => ?_
                                simp [slookup] at hn' he0' hns'
                                subst hn' he0' hns'
                                -- the readers, moved to `finishM`'s own environment
                                have hfin' : SEval hwMemSF (finishParams.zip [dF, d3, outs, flops, b, fq, mems, ms])
                                    finishBody v := by
                                  refine toF (by rfl) (agree_sound _ _ _ _ ?_ hfin)
                                  intro y hy
                                  rcases finish_names y hy with rfl | rfl | rfl | rfl | rfl | rfl | rfl | rfl <;>
                                    simp [slookup, finishParams]
                                have hfold := fold_fuse hbody hfin'
                                unfold mainMFusedInner
                                refine .call ?_ sFn_hwMemSF_then rfl hfold
                                refine .cons (.ref (by simp [slookup])) (.cons (toF (by rfl) hsrc)
                                  (.cons (.ref (by simp [slookup])) ?_))
                                unfold thenArgsTail R'
                                exact .cons (.ref (by simp [slookup])) (.cons (.ref (by simp [slookup]))
                                  (.cons (.ref (by simp [slookup])) (.cons (.ref (by simp [slookup]))
                                  (.cons (.ref (by simp [slookup])) (.cons (.ref (by simp [slookup]))
                                  (.cons (.ref (by simp [slookup])) .nil))))))
            · cases halt2
      · cases halt

theorem mainM_transferF {vs : List Val} {v : Val}
    (h : SEval hwMemS [] (.call "mainM" (vs.map SExp.lit)) v) :
    SEval hwMemSF [] (.call "mainM" (vs.map SExp.lit)) v := by
  cases h with
  | call hes hfn hlen hb =>
    rename_i vs' fd
    rw [sFn_mainM] at hfn
    cases hfn
    have hvs : vs' = vs := SEvalList_lits_inj vs vs' hes
    subst hvs
    obtain ⟨a, b, c, d, rfl⟩ := len4 (by simpa using hlen.symm)
    exact .call (SEvalList_lits _) sFn_hwMemSF_mainM rfl (body_transferF hb)

/-! ## 7. Resolution and binding-time analysis of the fused program -/

def hwMemFResolved : Except String (Program × List Bool) := resolveProgram hwMemSF
def hwMemPF : Program := match hwMemFResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def hwMemInlF : List Bool := match hwMemFResolved with | .ok (_, i) => i | .error _ => []
def hwMemAF : Except BTAError AProgram := bta hwMemPF hwMemInlF [.stat, .dyn, .dyn, .dyn] 200
def hwMemAPF : AProgram := match hwMemAF with | .ok a => a | .error _ => ⟨[], 0⟩

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemFResolved_isOk : Option.isSome (Except.toOption hwMemFResolved) = true := rfl

theorem hwMemPF_resolves : resolveProgram hwMemSF = .ok (hwMemPF, hwMemInlF) := by
  have h0 := hwMemFResolved_isOk
  unfold hwMemPF hwMemInlF
  unfold hwMemFResolved at h0 ⊢
  cases h : resolveProgram hwMemSF with
  | ok pi   => cases pi; rfl
  | error e => rw [h] at h0; simp [Except.toOption] at h0

theorem hwMemSF_entry : hwMemSF.entry = "mainM" := rfl

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemAF_isOk : Option.isSome (Except.toOption hwMemAF) = true := rfl

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemAPF_entry_params :
    (hwMemAPF.fn hwMemAPF.entry).map AFunDef.params = some [.stat, .dyn, .dyn, .dyn] := rfl

theorem hwMemAF_ok : hwMemAF = .ok hwMemAPF := by
  have h0 := hwMemAF_isOk
  unfold hwMemAPF
  cases h : hwMemAF with
  | ok a    => rfl
  | error z => rw [h] at h0; simp [Except.toOption] at h0

theorem hwMemAPF_erases : eraseProgram hwMemAPF = hwMemPF := bta_erases hwMemAF_ok

theorem hwMemAPF_wf : wfAProgram hwMemAPF = true := bta_sound hwMemAF_ok

theorem hwMemAPF_entry :
    ∃ afd, hwMemAPF.fn hwMemAPF.entry = some afd
         ∧ afd.params = [.stat, .dyn, .dyn, .dyn] := by
  have h := hwMemAPF_entry_params
  cases hfn : hwMemAPF.fn hwMemAPF.entry with
  | none     => rw [hfn] at h; simp at h
  | some afd =>
      rw [hfn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      exact ⟨afd, rfl, h⟩

/-! ## 8. On every admitted step the fused entry agrees with `hwMemP`'s -/

theorem memF_iff_of {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hw : ∃ r : StateM.ResultM,
      SEval hwMemS []
        (.call "mainM" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                        .lit (StateM.encStateM st)])
        (StateM.encResultM r)) (v : Val) :
    Eval hwMemPF [] (.call hwMemPF.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
      ↔ Eval hwMemP [] (.call hwMemP.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v := by
  obtain ⟨r, hs⟩ := hw
  have hP : Eval hwMemP [] (.call hwMemP.entry
      ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
      (StateM.encResultM r) := by
    refine SEval_entry hwMemP_resolves ?_
    simpa [hwMemS_entry] using hs
  have hV : Eval hwMemPF [] (.call hwMemPF.entry
      ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
      (StateM.encResultM r) := by
    refine SEval_entry hwMemPF_resolves ?_
    rw [hwMemSF_entry]
    exact mainM_transferF (vs := [encDesign D, encEdges e, encInput i, StateM.encStateM st]) hs
  constructor
  · intro h; rw [Eval_det h hV]; exact hP
  · intro h; rw [Eval_det h hP]; exact hV

theorem memF_iff {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsB D = true) (hR : runtimeOKB D i st e = true) (v : Val) :
    Eval hwMemPF [] (.call hwMemPF.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
      ↔ Eval hwMemP [] (.call hwMemP.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v :=
  memF_iff_of (admits_run_seval hA hR) v

theorem memF_iffW {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsWB D = true) (hR : runtimeOKB D i st e = true) (v : Val) :
    Eval hwMemPF [] (.call hwMemPF.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
      ↔ Eval hwMemP [] (.call hwMemP.entry
        ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v :=
  memF_iff_of (admits_run_sevalW hA hR) v

/-! ## 9. The first projection through the fused program -/

def projectMemFDesignAt (sf wf : Nat) (D : DesignCert) : Except MixError Program :=
  mixDriver sf wf hwMemAPF [encDesign D]

theorem projectMemFDesignAt_correct {sf wf : Nat} {D : DesignCert} {R : Program}
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hproj : projectMemFDesignAt sf wf D = .ok R)
    (hA : admitsB D = true) (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval R [] (.call R.entry
            ([encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hden, hok, hiff⟩ := memEntry_unique hA hR
  have hR0 : runtimeOKB D i st e = true := by
    unfold runtimeFullOKB at hR; simp only [Bool.and_eq_true] at hR; exact hR.1
  refine ⟨r, hden, hok, fun v => ?_⟩
  exact specializeFour_correct_of hwMemAPF_entry hwMemAPF_erases
    (fun w => (memF_iff hA hR0 w).trans (hiff w)) hproj v

theorem projectMemFDesignAtW_correct {sf wf : Nat} {D : DesignCert} {R : Program}
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hproj : projectMemFDesignAt sf wf D = .ok R)
    (hA : admitsWB D = true) (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval R [] (.call R.entry
            ([encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hden, hok, hiff⟩ := memEntry_uniqueW hA hR
  have hR0 : runtimeOKB D i st e = true := by
    unfold runtimeFullOKB at hR; simp only [Bool.and_eq_true] at hR; exact hR.1
  refine ⟨r, hden, hok, fun v => ?_⟩
  exact specializeFour_correct_of hwMemAPF_entry hwMemAPF_erases
    (fun w => (memF_iffW hA hR0 w).trans (hiff w)) hproj v

/-! ## 10. The checked simulator, specialized through the fused program -/

structure MemSimWFAtF (sf wf : Nat) (sim : MemSimulator) : Prop where
  proj  : projectMemFDesignAt sf wf sim.design = .ok sim.prog
  chk   : checkResidual sim.prog = some sim.bound
  admit : admitsB sim.design = true

structure MemSimWFAtWF (sf wf : Nat) (sim : MemSimulator) : Prop where
  proj  : projectMemFDesignAt sf wf sim.design = .ok sim.prog
  chk   : checkResidual sim.prog = some sim.bound
  admit : admitsWB sim.design = true

theorem mkMemSimOf_wfF {sf wf : Nat} {D : DesignCert} {R : Program}
    {sim : MemSimulator} (hproj : projectMemFDesignAt sf wf D = .ok R)
    (h : mkMemSimOf D R = some sim) : MemSimWFAtF sf wf sim := by
  obtain ⟨hd, hp, hchk, hadm⟩ := mkMemSimOf_parts h
  exact ⟨by rw [hd, hp]; exact hproj, by rw [hp]; exact hchk, by rw [hd]; exact hadm⟩

theorem mkMemSimOfW_wfF {sf wf : Nat} {D : DesignCert} {R : Program}
    {sim : MemSimulator} (hproj : projectMemFDesignAt sf wf D = .ok R)
    (h : mkMemSimOfW D R = some sim) : MemSimWFAtWF sf wf sim := by
  obtain ⟨hd, hp, hchk, hadm⟩ := mkMemSimOfW_parts h
  exact ⟨by rw [hd, hp]; exact hproj, by rw [hp]; exact hchk, by rw [hd]; exact hadm⟩

theorem runMemSimF_correct {sf wf : Nat} {sim : MemSimulator} (hw : MemSimWFAtF sf wf sim)
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hR : runtimeFullOKB sim.design i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSim sim e i st = some r
      ∧ r.denote = Compiler.interpretDesign sim.design e i (StateM.denote st)
      ∧ stateFullOKB sim.design r.nextState = true := by
  obtain ⟨r, hden, hok, hiff⟩ := projectMemFDesignAt_correct hw.proj hw.admit hR
  have hev : Eval sim.prog [] (.call sim.prog.entry
      [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)])
      (StateM.encResultM r) := (hiff _).mpr rfl
  have hfuel := checkResidual_complete hw.chk hev
  refine ⟨r, ?_, hden, hok⟩
  unfold runMemSim
  rw [hfuel]
  exact StateM.decResultM_encResultM r

theorem runMemSimWF_correct {sf wf : Nat} {sim : MemSimulator} (hw : MemSimWFAtWF sf wf sim)
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hR : runtimeFullOKB sim.design i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSim sim e i st = some r
      ∧ r.denote = Compiler.interpretDesign sim.design e i (StateM.denote st)
      ∧ stateFullOKB sim.design r.nextState = true := by
  obtain ⟨r, hden, hok, hiff⟩ := projectMemFDesignAtW_correct hw.proj hw.admit hR
  have hev : Eval sim.prog [] (.call sim.prog.entry
      [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)])
      (StateM.encResultM r) := (hiff _).mpr rfl
  have hfuel := checkResidual_complete hw.chk hev
  refine ⟨r, ?_, hden, hok⟩
  unfold runMemSim
  rw [hfuel]
  exact StateM.decResultM_encResultM r

#audit_axioms agree_sound
#audit_axioms SEval_append
#audit_axioms fold_fuse
#audit_axioms body_transferF
#audit_axioms mainM_transferF
#audit_axioms hwMemPF_resolves
#audit_axioms hwMemAPF_erases
#audit_axioms hwMemAPF_entry
#audit_axioms memF_iff
#audit_axioms memF_iffW
#audit_axioms projectMemFDesignAt_correct
#audit_axioms projectMemFDesignAtW_correct
#audit_axioms runMemSimF_correct
#audit_axioms runMemSimWF_correct

end MemFused
end Projection
