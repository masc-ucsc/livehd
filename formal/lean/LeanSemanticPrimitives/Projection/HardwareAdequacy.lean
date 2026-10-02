/-
# `I_hw` adequacy — the concrete bridge

Phase 4 item 2, increment B.  `SurfaceSemantics.lean` is generic and mentions no
hardware; this file is where it is pointed at `hwS`.

Three things live here, and they are kept apart from `HardwareInterpreter.lean`
on purpose:

* `hwResolved_ok` is the one place the kernel reduces `resolveProgram hwS`.  It
  costs about 4 s of the build, and isolating it means that cost is paid once
  and is visible in the per-module timings rather than smeared through a 3 s
  module.  It is also the fact that D1.1 warned could be intolerable; it is not.
* the canonicality corollary, which is what turns a `ResultRel` into an equation
  between VALUES and so makes an `iff` statable;
* the `hwS`-specialized transfer lemmas every per-function proof goes through.

Nothing here appeals to `evalFuel`, to the fixture `#guard`s, to `projectDesign`
or to the verified compiler.
-/

import LeanSemanticPrimitives.Projection.HardwareInterpreter
import LeanSemanticPrimitives.Projection.SurfaceSemantics

namespace Projection
namespace Hw

open Surface Compiler

/-! ## The resolver equation

`hwP` and `hwInl` are DEFINED by matching on `hwResolved`, so the equation
`hwResolved = .ok (hwP, hwInl)` holds as soon as the error branch is excluded —
and excluding it only needs the head constructor, not the payload.  That is why
the statement below is `isSome` rather than a literal `.ok (…, …)` with the
41-function program written out: the kernel stops at the constructor.

Measured on this machine: the whole module costs ~4.3 s wall, against ~0.3 s for
loading its imports alone.  The default heartbeat budget is NOT enough — without
`maxHeartbeats` the elaborator gives up and reports a bare "not definitionally
equal", which reads like the equation being false rather than like a budget. -/

set_option maxHeartbeats 2000000 in
set_option maxRecDepth 100000 in
theorem hwResolved_isOk : Option.isSome (Except.toOption hwResolved) = true := rfl

theorem hwResolved_ok : hwResolved = .ok (hwP, hwInl) := by
  have h0 := hwResolved_isOk
  unfold hwP hwInl
  cases h : hwResolved with
  | ok pi    => cases pi; rfl
  | error e  => rw [h] at h0; simp [Except.toOption] at h0

theorem hwP_resolves : resolveProgram hwS = .ok (hwP, hwInl) := hwResolved_ok

/-! ## Transfer

Every per-function fact about `I_hw` is stated over the NAMED program and
crossed here.  No proof below mentions a de Bruijn index or a function position,
so inserting a helper into `hwS` cannot invalidate one. -/

theorem hw_transfer {σ : SEnv} {e : SExp} {v : Val} {t : Term}
    (hs : SEval hwS σ e v)
    (hr : resolve (funNames hwS) (σ.map Prod.fst) e = .ok t) :
    Eval hwP (σ.map Prod.snd) t v :=
  SEval_sound hwP_resolves σ e v t hs hr

/-- The whole-cycle entry call, which is the exact shape `IHwAdequate` uses.
`hwP.entry` is never computed. -/
theorem hw_entry {vs : List Val} {v : Val}
    (h : SEval hwS [] (.call "main" (vs.map SExp.lit)) v) :
    Eval hwP [] (.call hwP.entry (vs.map Term.lit)) v :=
  SEval_entry hwP_resolves h

/-! ## Canonicality, in the shape adequacy uses

`IHwAdequate` is an `iff`, so the RIGHT-to-left direction has to produce an
object value and the LEFT-to-right direction has to pin the one it is given.
`ResultRel_canonical` is what makes those the same value: an object result
satisfying `ResultRel _ rr` is `encResult rr` on the nose. -/

theorem resultRel_iff_eq {v : Val} {rr : RuntimeResult} (h : MemFree rr.nextState) :
    ResultRel v rr ↔ v = encResult rr :=
  ⟨ResultRel_canonical, fun he => he ▸ ResultRel_encResult h⟩

/-- Specialized to what `SupportedByProjection` actually supplies: a memory-free
certificate makes every cycle's next state representable. -/
theorem resultRel_interpret_iff {D : Compiler.DesignCert} (hm : D.memories = #[])
    {v : Val} {e : Compiler.ClockEdges} {i : Compiler.RuntimeInput}
    {s : Compiler.RuntimeState} :
    ResultRel v (Compiler.interpretDesign D e i s)
      ↔ v = encResult (Compiler.interpretDesign D e i s) :=
  resultRel_iff_eq (interpretDesign_memFree hm e i s)

/-! ## Group 1: slot reads and source values

`nthD` is the object's only list indexing and it has NO bounds check: an
out-of-range read is `hd nil`, which has no value AT ALL rather than a default.
`sourceValue` on the shared side defaults with `getD`.  So the two agree only
inside the bound, which is exactly where `RuntimeSized` is consumed -- carried
as a hypothesis, never discovered inside a proof. -/

theorem nthD_agree {α : Type} {e : α → Val} :
    ∀ (k : Nat) (σ : SEnv) (el ek : SExp) (xs : List α) (hk : k < xs.length),
      SEval hwS σ el (encListG e xs) →
      SEval hwS σ ek (.int (Int.ofNat k)) →
      SEval hwS σ (.call "nthD" [el, ek]) (e (xs[k]'hk))
  | _,   _, _, _, [],      hk, _,  _  => absurd hk (by simp)
  | 0,   _, _, _, _ :: _,  _,  hl, hi => by
      refine SEval_call2 hl hi rfl rfl ?_
      exact SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) (SEval_hd (.ref rfl))
  | n+1, _, _, _, _ :: xs, hk, hl, hi => by
      refine SEval_call2 hl hi rfl rfl ?_
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]; omega)) ?_
      exact nthD_agree n _ _ _ xs (by simpa using hk) (SEval_tl (.ref rfl))
        (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]))

theorem nthD_arr {α : Type} {e : α → Val} {σ : SEnv} {el ek : SExp}
    {xs : Array α} {k : Nat} (hk : k < xs.size)
    (hl : SEval hwS σ el (encArr e xs))
    (hi : SEval hwS σ ek (.int (Int.ofNat k))) :
    SEval hwS σ (.call "nthD" [el, ek]) (e xs[k]) := by
  have h := nthD_agree k σ el ek xs.toList (by simpa using hk)
              (by simpa [encArr] using hl) hi
  simpa using h

/-- `bv_nonzero`, which the object spells out rather than giving a primitive. -/
theorem nz_agree {σ : SEnv} {ev : SExp} {b : BV} (h : SEval hwS σ ev (encBV b)) :
    SEval hwS σ (.call "nz" [ev]) (.bool (bv_nonzero b)) := by
  refine SEval_call1 h rfl rfl ?_
  refine SEval_prim1 (SEval_prim2 (SEval_prim1 (.ref rfl) rfl) .lit rfl) ?_
  simp [evalPrim, bv_nonzero_eq]

/-! ### The four supported source forms, separately

Each is stated against `Compiler.sourceValue` on the nose.  They are kept apart
because they consume DIFFERENT bounds: `input` reads the input vector,
`flopQ` the state vector, `flopQAsync` BOTH, and `const` neither. -/

theorem srcVal_input {σ : SEnv} {esd einp efq : SExp} {idx w : Nat}
    {i : RuntimeInput} {s : RuntimeState} (hb : idx < i.size)
    (h1 : SEval hwS σ esd (encSource (.input idx w)))
    (h2 : SEval hwS σ einp (encInput i))
    (h3 : SEval hwS σ efq (encBVs s.flops)) :
    SEval hwS σ (.call "srcVal" [esd, einp, efq])
      (encBV (sourceValue i s (.input idx w)).asBV) := by
  have hv : (sourceValue i s (.input idx w)).asBV = bv_resize w i[idx] := by
    simp [sourceValue, CertVal.asBV, Array.getElem?_eq_getElem hb]
  rw [hv]
  refine SEval_call3 h1 h2 h3 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  exact SEval_prim2 (.ref rfl) (nthD_arr hb (.ref rfl) (.ref rfl)) rfl

theorem srcVal_const {σ : SEnv} {esd einp efq : SExp} {w : Nat} {v : Int}
    {i : RuntimeInput} {s : RuntimeState}
    (h1 : SEval hwS σ esd (encSource (.const w v)))
    (h2 : SEval hwS σ einp (encInput i))
    (h3 : SEval hwS σ efq (encBVs s.flops)) :
    SEval hwS σ (.call "srcVal" [esd, einp, efq])
      (encBV (sourceValue i s (.const w v)).asBV) := by
  have hv : (sourceValue i s (.const w v)).asBV = mk_bv w v := by
    simp [sourceValue, CertVal.asBV]
  rw [hv]
  refine SEval_call3 h1 h2 h3 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  exact SEval_prim2 (.ref rfl) (.ref rfl) rfl

theorem srcVal_flopQ {σ : SEnv} {esd einp efq : SExp} {idx w : Nat}
    {i : RuntimeInput} {s : RuntimeState} (hb : idx < s.flops.size)
    (h1 : SEval hwS σ esd (encSource (.flopQ idx w)))
    (h2 : SEval hwS σ einp (encInput i))
    (h3 : SEval hwS σ efq (encBVs s.flops)) :
    SEval hwS σ (.call "srcVal" [esd, einp, efq])
      (encBV (sourceValue i s (.flopQ idx w)).asBV) := by
  have hv : (sourceValue i s (.flopQ idx w)).asBV = bv_resize w s.flops[idx] := by
    simp [sourceValue, CertVal.asBV, Array.getElem?_eq_getElem hb]
  rw [hv]
  refine SEval_call3 h1 h2 h3 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  exact SEval_prim2 (.ref rfl) (nthD_arr hb (.ref rfl) (.ref rfl)) rfl

/-- The async-reset read is the only source that consults the input vector for
something other than data, and the only one with a polarity.  `activeLow` is
read from the descriptor on BOTH sides, so this genuinely checks the polarity
rather than unfolding to it. -/
theorem srcVal_flopQAsync {σ : SEnv} {esd einp efq : SExp}
    {idx w ri : Nat} {rv : Int} {al : Bool}
    {i : RuntimeInput} {s : RuntimeState}
    (hbr : ri < i.size) (hbi : idx < s.flops.size)
    (h1 : SEval hwS σ esd (encSource (.flopQAsync idx w ri rv al)))
    (h2 : SEval hwS σ einp (encInput i))
    (h3 : SEval hwS σ efq (encBVs s.flops)) :
    SEval hwS σ (.call "srcVal" [esd, einp, efq])
      (encBV (sourceValue i s (.flopQAsync idx w ri rv al)).asBV) := by
  have hv : (sourceValue i s (.flopQAsync idx w ri rv al)).asBV
      = (if (if al then !(bv_nonzero i[ri]) else bv_nonzero i[ri]) then mk_bv w rv
         else bv_resize w s.flops[idx]) := by
    simp [sourceValue, CertVal.asBV,
          Array.getElem?_eq_getElem hbr, Array.getElem?_eq_getElem hbi]
  rw [hv]
  refine SEval_call3 h1 h2 h3 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval_ite_of_bool
    (bb := (if al then !(bv_nonzero i[ri]) else bv_nonzero i[ri])) ?_ ?_ ?_
  · cases al with
    | false => exact SEval.iteF (.ref rfl) (nz_agree (nthD_arr hbr (.ref rfl) (.ref rfl)))
    | true  => exact SEval.iteT (.ref rfl)
                 (SEval_prim1 (nz_agree (nthD_arr hbr (.ref rfl) (.ref rfl))) rfl)
  · intro hT
    simp only [hT, if_true]
    exact SEval_prim2 (.ref rfl) (.ref rfl) rfl
  · intro hF
    simp only [hF, Bool.false_eq_true, if_false]
    exact SEval_prim2 (.ref rfl) (nthD_arr hbi (.ref rfl) (.ref rfl)) rfl

/-- The four combined, under exactly `SourceSupported` and the two bounds the
`RuntimeSized` fields supply.  The memory forms are excluded here, not computed
wrongly: `SourceSupported` rules them out before the object's defined-and-wrong
fallback is ever reached. -/
theorem srcVal_agree {σ : SEnv} {esd einp efq : SExp} {sd : SourceDesc}
    {i : RuntimeInput} {s : RuntimeState}
    (hsup : SourceSupported sd = true)
    (hin : SourceInputBound sd i.size) (hst : SourceFlopBound sd s.flops.size)
    (h1 : SEval hwS σ esd (encSource sd))
    (h2 : SEval hwS σ einp (encInput i))
    (h3 : SEval hwS σ efq (encBVs s.flops)) :
    SEval hwS σ (.call "srcVal" [esd, einp, efq]) (encBV (sourceValue i s sd).asBV) := by
  cases sd with
  | input idx w            => exact srcVal_input hin h1 h2 h3
  | const w v              => exact srcVal_const h1 h2 h3
  | flopQ idx w            => exact srcVal_flopQ hst h1 h2 h3
  | flopQAsync idx w ri rv al => exact srcVal_flopQAsync hin hst h1 h2 h3
  | memImg _ _ _           => exact absurd hsup (by simp [SourceSupported])
  | memConst _ _ _         => exact absurd hsup (by simp [SourceSupported])

/-! ## Group 2: the source environment

The object builds its slot environment NEWEST-FIRST and reads slot `s` of `n`
at depth `n-1-s`.  `srcEnv` on the shared side is a function `Nat → CertVal`.
Rather than relate a cons chain to a function pointwise twice, the environment
is described once as a LIST IN SLOT ORDER whose object form is its reverse; the
lookup lemma is then one `List.getElem_reverse`. -/

/-- A slot environment in SLOT ORDER, as the object holds it (reversed). -/
abbrev objEnv (vals : List Val) : Val := encListG (fun v => v) vals.reverse

theorem lenL_agree {α : Type} {e : α → Val} :
    ∀ (xs : List α) (σ : SEnv) (el : SExp), SEval hwS σ el (encListG e xs) →
      SEval hwS σ (.call "lenL" [el]) (.int (Int.ofNat xs.length))
  | [],      _, _, hl => by
      refine SEval_call1 hl rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | _ :: xs, _, _, hl => by
      refine SEval_call1 hl rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval_prim2 .lit (lenL_agree xs _ _ (SEval_tl (.ref rfl))) ?_
      simp [evalPrim]; omega

/-- `slot(env, n, s)` is `nthD` at depth `n-1-s`, which is slot `s` because the
chain is newest-first.  The bound is `s < n`; there is no check in the object. -/
theorem slot_agree {σ : SEnv} {eenv en es : SExp} {vals : List Val} {sl : Nat}
    (h : sl < vals.length)
    (henv : SEval hwS σ eenv (objEnv vals))
    (hn : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.int (Int.ofNat sl))) :
    SEval hwS σ (.call "slot" [eenv, en, es]) (vals[sl]'h) := by
  have hrev : vals.length - 1 - sl < vals.reverse.length := by
    simp only [List.length_reverse]; omega
  have hidx : vals.reverse[vals.length - 1 - sl]'hrev = vals[sl]'h := by
    rw [List.getElem_reverse]
    congr 1
    omega
  have hcast : Int.ofNat vals.length - 1 - Int.ofNat sl
      = Int.ofNat (vals.length - 1 - sl) := by
    simp only [Int.ofNat_eq_natCast]; omega
  refine SEval_call3 henv hn hs rfl rfl ?_
  rw [← hidx]
  refine nthD_agree (e := fun v => v) (vals.length - 1 - sl) _ _ _ vals.reverse hrev
    ?_ ?_
  · exact .ref rfl
  · refine SEval_prim2 (SEval_prim2 (.ref rfl) .lit rfl) (.ref rfl) ?_
    simp only [evalPrim]
    rw [hcast]

/-- `mkSources` consumes the source array in order and conses each value onto
the accumulator, so the chain comes out newest-first -- which is
`List.reverseAux`, the accumulator recursion itself, not a separate `reverse`
step to be proved equal to it. -/
theorem mkSources_agree {i : RuntimeInput} {s : RuntimeState} :
    ∀ (srcs : List SourceDesc) (acc : List Val) (σ : SEnv) (es ei ef ea : SExp),
      (∀ sd ∈ srcs, SourceSupported sd = true) →
      (∀ sd ∈ srcs, SourceInputBound sd i.size) →
      (∀ sd ∈ srcs, SourceFlopBound sd s.flops.size) →
      SEval hwS σ es (encListG encSource srcs) →
      SEval hwS σ ei (encInput i) →
      SEval hwS σ ef (encBVs s.flops) →
      SEval hwS σ ea (encListG (fun v => v) acc) →
      SEval hwS σ (.call "mkSources" [es, ei, ef, ea])
        (encListG (fun v => v)
          (List.reverseAux (srcs.map (fun sd => encBV (sourceValue i s sd).asBV)) acc))
  | [],         _, _, _, _, _, _, _,    _,   _,   h1, h2, h3, h4 => by
      refine SEval_call4 h1 h2 h3 h4 rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | sd :: rest, _, _, _, _, _, _, hsup, hin, hst, h1, h2, h3, h4 => by
      refine SEval_call4 h1 h2 h3 h4 rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact mkSources_agree rest _ _ _ _ _ _
        (fun x hx => hsup x (List.mem_cons_of_mem _ hx))
        (fun x hx => hin x (List.mem_cons_of_mem _ hx))
        (fun x hx => hst x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_consP
          (srcVal_agree (hsup sd List.mem_cons_self) (hin sd List.mem_cons_self)
            (hst sd List.mem_cons_self) (SEval_hd (.ref rfl)) (.ref rfl) (.ref rfl))
          (.ref rfl))

/-- The list the object's chain carries IS `srcEnv`, slot by slot.  Stated as
an equation between VALUES rather than as a second environment relation, so
nothing downstream has to carry a notion of "object environment" of its own. -/
theorem source_env_agree {D : DesignCert} {i : RuntimeInput} {s : RuntimeState}
    {sl : Nat} (h : sl < D.sources.size) :
    (D.sources.toList.map (fun sd => encBV (sourceValue i s sd).asBV))[sl]'(by simpa using h)
      = encBV (srcEnv D i s sl).asBV := by
  simp [srcEnv, Array.getElem?_eq_getElem h]

end Hw
end Projection
