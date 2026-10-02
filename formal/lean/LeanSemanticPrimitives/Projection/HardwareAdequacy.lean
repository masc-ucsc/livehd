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

/-! ## Group 3: the operator layer

Every operator helper reads its operands through `slot`, so all of them share
one hypothesis about the environment and differ only in the fold.  The shared
part is `SlotVals` below; the per-operator part is `OperatorBridge`'s pinned
equation, quoted and never re-derived. -/

/-- The environment chain's entries ARE the encodings of the slot values `arg`
names.  Carried separately from the two `SEval` facts about `env` and `n`,
because those have to be re-established at every recursive call (where the
environment arrives as `R "env"`) while this one does not mention `σ`. -/
def SlotVals (vals : List Val) (arg : Nat → BV) : Prop :=
  ∀ (k : Nat) (h : k < vals.length), vals[k]'h = encBV (arg k)

theorem slot_read {σ : SEnv} {ee en es : SExp} {vals : List Val} {arg : Nat → BV}
    (hsv : SlotVals vals arg) {d : Nat} (hd : d < vals.length)
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hes : SEval hwS σ es (.int (Int.ofNat d))) :
    SEval hwS σ (.call "slot" [ee, en, es]) (encBV (arg d)) := by
  rw [← hsv d hd]
  exact slot_agree hd henv hlen hes

/-! ### Arithmetic facts the operand layer needs

`bv_uint` is `value % 2^width`, so it is never negative -- which is what makes
`Op_MuxN`'s integer selector comparison agree with the pinned model's
`Int.toNat`.  Proved here rather than assumed: a negative selector would make
the object answer zero where `eval_op` indexes operand 0. -/

theorem two_pow_pos : ∀ w : Nat, (0 : Int) < 2 ^ w
  | 0     => by decide
  | n + 1 => by have h := two_pow_pos n; rw [Int.pow_succ]; omega

theorem bv_uint_nonneg (x : BV) : 0 ≤ bv_uint x := by
  unfold bv_uint
  exact Int.emod_nonneg _ (Int.ne_of_gt (two_pow_pos x.width))

theorem bv_uint_ofNat_toNat (x : BV) : Int.ofNat (bv_uint x).toNat = bv_uint x :=
  Int.toNat_of_nonneg (bv_uint_nonneg x)

/-! ### `Op_And`

The first operand is resized to the node width and seeds the fold; the rest go
in unchanged.  `Op_Or` and `Op_Xor` below do NOT share that shape, and the
difference is transcribed from the pinned model rather than generalised. -/

theorem foldAnd_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    (w : Nat) :
    ∀ (deps : List Nat) (acc : BV) (σ : SEnv) (ed ee en ea ew : SExp),
      (∀ d ∈ deps, d < vals.length) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldAnd" [ed, ee, en, ea, ew])
        (encBV ((deps.map arg).foldl (fun a b => bv_bitwise w (fun x y => x && y) a b) acc))
  | [],      _, _, _, _, _, _, _, _,  hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, hb, hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldAnd_agree hsv w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

theorem opAnd_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opAnd" [ew, ed, ee, en])
      (encBV (eval_op .Op_And w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldAnd_agree hsv w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim2 (.ref rfl)
          (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

/-! ### `Op_Or` and `Op_Xor`: zero-seeded, folding EVERY operand -/

theorem foldOr_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    (w : Nat) :
    ∀ (deps : List Nat) (acc : BV) (σ : SEnv) (ed ee en ea ew : SExp),
      (∀ d ∈ deps, d < vals.length) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldOr" [ed, ee, en, ea, ew])
        (encBV ((deps.map arg).foldl (fun a b => bv_bitwise w (fun x y => x || y) a b) acc))
  | [],      _, _, _, _, _, _, _, _,  hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, hb, hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldOr_agree hsv w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

theorem opOr_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opOr" [ew, ed, ee, en])
      (encBV (eval_op .Op_Or w (deps.map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact foldOr_agree hsv w deps _ _ _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl)
    (SEval_prim2 (.ref rfl) .lit rfl) (.ref rfl)

theorem foldXor_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    (w : Nat) :
    ∀ (deps : List Nat) (acc : BV) (σ : SEnv) (ed ee en ea ew : SExp),
      (∀ d ∈ deps, d < vals.length) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldXor" [ed, ee, en, ea, ew])
        (encBV ((deps.map arg).foldl (fun a b => bv_bitwise w (fun x y => xor x y) a b) acc))
  | [],      _, _, _, _, _, _, _, _,  hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, hb, hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldXor_agree hsv w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

theorem opXor_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opXor" [ew, ed, ee, en])
      (encBV (eval_op .Op_Xor w (deps.map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact foldXor_agree hsv w deps _ _ _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl)
    (SEval_prim2 (.ref rfl) .lit rfl) (.ref rfl)

/-! ### `Op_Ror`: a REDUCTION, not a bitwise `Op_Or` -/

theorem anyNz_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg) :
    ∀ (deps : List Nat) (σ : SEnv) (ed ee en : SExp),
      (∀ d ∈ deps, d < vals.length) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "anyNz" [ed, ee, en])
        (.bool ((deps.map arg).any bv_nonzero))
  | [],      _, _, _, _, _,  hdv, henv, hlen => by
      refine SEval_call3 hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | d :: ds, _, _, _, _, hb, hdv, henv, hlen => by
      refine SEval_call3 hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact SEval_prim2
        (nz_agree (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl))))
        (anyNz_agree hsv ds _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))
        rfl

theorem opRor_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opRor" [ew, ed, ee, en])
      (encBV (eval_op .Op_Ror w (deps.map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  rw [evalOp_Ror]
  refine SEval_ite_of_bool (bb := (deps.map arg).any bv_nonzero)
    (anyNz_agree hsv deps _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl)) ?_ ?_
  · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) .lit rfl
  · intro hF
    simp only [hF, Bool.false_eq_true, if_false]
    exact SEval_prim2 (.ref rfl) .lit rfl

/-! ### `Op_EQ`: every operand against the FIRST, and `[]` is 1 -/

theorem eqAll_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    (a : Int) :
    ∀ (deps : List Nat) (σ : SEnv) (ed ee en ea : SExp),
      (∀ d ∈ deps, d < vals.length) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (.int a) →
      SEval hwS σ (.call "eqAll" [ed, ee, en, ea])
        (.bool ((deps.map arg).all fun b => bv_uint b = a))
  | [],      _, _, _, _, _, _,  hdv, henv, hlen, ha => by
      refine SEval_call4 hdv henv hlen ha rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | d :: ds, _, _, _, _, _, hb, hdv, henv, hlen, ha => by
      refine SEval_call4 hdv henv hlen ha rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact SEval_prim2
        (SEval_prim2
          (SEval_prim1 (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
          (.ref rfl) rfl)
        (eqAll_agree hsv a ds _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl))
        rfl

theorem opEq_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opEq" [ew, ed, ee, en])
      (encBV (eval_op .Op_EQ w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      rw [List.map_cons, evalOp_EQ_cons]
      refine SEval_ite_of_bool
        (bb := (ds.map arg).all fun b => bv_uint b = bv_uint (arg d))
        (eqAll_agree hsv (bv_uint (arg d)) ds _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
          (SEval_prim1 (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)) ?_ ?_
      · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) .lit rfl
      · intro hF
        simp only [hF, Bool.false_eq_true, if_false]
        exact SEval_prim2 (.ref rfl) .lit rfl

/-! ### `Op_Sum`: the payload splits the operand list

`n_add` operands are ADDED and the rest SUBTRACTED, all through `bv_uint`, with
the node width applied ONCE at the end -- so the truncation is of the sum, not
of each term.  The two walks are `take` and `drop` of the same list. -/

theorem sumAdds_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg) :
    ∀ (deps : List Nat) (k : Nat) (σ : SEnv) (ek ed ee en : SExp),
      (∀ d ∈ deps, d < vals.length) →
      SEval hwS σ ek (.int (Int.ofNat k)) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "sumAdds" [ek, ed, ee, en])
        (.int ((((deps.map arg).map bv_uint).take k).sum))
  | [],      _,     _, _, _, _, _, _,  hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [List.map_nil, List.take_nil, List.sum_nil]
      exact .lit
  | _ :: _,  0,     _, _, _, _, _, _,  hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
      simp only [List.take_zero, List.sum_nil]
      exact .lit
  | d :: ds, m + 1, _, _, _, _, _, hb, hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]; omega)) ?_
      exact SEval_prim2
        (SEval_prim1 (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl))) rfl)
        (sumAdds_agree hsv ds m _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))
        rfl

theorem sumSubs_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg) :
    ∀ (deps : List Nat) (k : Nat) (σ : SEnv) (ek ed ee en : SExp),
      (∀ d ∈ deps, d < vals.length) →
      SEval hwS σ ek (.int (Int.ofNat k)) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "sumSubs" [ek, ed, ee, en])
        (.int ((((deps.map arg).map bv_uint).drop k).sum))
  | [],      _,     _, _, _, _, _, _,  hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [List.map_nil, List.drop_nil, List.sum_nil]
      exact .lit
  | d :: ds, 0,     _, _, _, _, _, hb, hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
      exact SEval_prim2
        (SEval_prim1 (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl))) rfl)
        (sumSubs_agree hsv ds 0 _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          .lit (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))
        rfl
  | d :: ds, m + 1, _, _, _, _, _, hb, hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]; omega)) ?_
      exact sumSubs_agree hsv ds m _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)

theorem opSum_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w nAdd : Nat} {deps : List Nat} {σ : SEnv} {ew ep ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hp : SEval hwS σ ep (.int (Int.ofNat nAdd)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSum" [ew, ep, ed, ee, en])
      (encBV (eval_op (.Op_Sum nAdd) w (deps.map arg))) := by
  refine SEval_call5 hw hp hdv henv hlen rfl rfl ?_
  exact SEval_prim2 (.ref rfl)
    (SEval_prim2
      (sumAdds_agree hsv deps nAdd _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
      (sumSubs_agree hsv deps nAdd _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
      rfl)
    rfl

/-! ### `Op_SHL`: the first operand is shifted INDEPENDENTLY by each of the rest

Not an accumulator shift: `a` is read once and never moves, and the shifted
copies are XOR-folded into a ZERO seed, so a one-operand node is 0, not `a`. -/

theorem foldShl_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    (w : Nat) (x : BV) :
    ∀ (bs : List Nat) (acc : BV) (σ : SEnv) (eb ee en ea ex ew : SExp),
      (∀ d ∈ bs, d < vals.length) →
      SEval hwS σ eb (encListG encNat bs) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ex (encBV x) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldShl" [eb, ee, en, ea, ex, ew])
        (encBV ((bs.map arg).foldl
          (fun a b => bv_bitwise w (fun p q => xor p q) a (bv_shl_step w x b)) acc))
  | [],      _, _, _, _, _, _, _, _, _,  hbv, henv, hlen, hacc, hx, hw => by
      refine SEval_call6 hbv henv hlen hacc hx hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, _, hb, hbv, henv, hlen, hacc, hx, hw => by
      refine SEval_call6 hbv henv hlen hacc hx hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldShl_agree hsv w x ds _ _ _ _ _ _ _ _
        (fun y hy => hb y (List.mem_cons_of_mem _ hy))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (SEval_prim3 (.ref rfl) (.ref rfl)
            (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
              (SEval_hd (.ref rfl))) rfl) rfl)
        (.ref rfl) (.ref rfl)

theorem opShl_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opShl" [ew, ed, ee, en])
      (encBV (eval_op .Op_SHL w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldShl_agree hsv w (arg d) ds _ _ _ _ _ _ _ _
        (fun y hy => hb y (List.mem_cons_of_mem _ hy))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim2 (.ref rfl) .lit rfl)
        (slot_read hsv (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl)))
        (.ref rfl)

/-! ### `Op_MuxN`: a DIRECT selection, not a walked list

The dep list and the index counter are both static, so this unrolls into a
chain of `ite` on the selector.  Agreement with the pinned model needs
`bv_uint` to be non-negative: the object compares the selector as an INTEGER
against `0, 1, 2, …`, while `eval_op` indexes with `Int.toNat`, and a negative
selector would make `toNat` pick operand 0 where the object picks none. -/

/-- What a selection at index `j` yields; out of range is zero, as in the
pinned model, rather than wrapping or erroring. -/
def muxAt (w : Nat) (arg : Nat → BV) (args : List Nat) (j : Nat) : BV :=
  ((args[j]?).map (fun d => bv_resize w (arg d))).getD (mk_bv w 0)

theorem muxPick_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    (w : Nat) (sel : Nat) :
    ∀ (args : List Nat) (k : Nat) (σ : SEnv) (ew ea ee en es ek : SExp),
      (∀ d ∈ args, d < vals.length) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ ea (encListG encNat args) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ es (.int (Int.ofNat sel)) →
      SEval hwS σ ek (.int (Int.ofNat k)) →
      SEval hwS σ (.call "muxPick" [ew, ea, ee, en, es, ek])
        (encBV (if k ≤ sel then muxAt w arg args (sel - k) else mk_bv w 0))
  | [],      k, _, _, _, _, _, _, _, _,  hw, hav, henv, hlen, hs, hk => by
      have hz : (if k ≤ sel then muxAt w arg [] (sel - k) else mk_bv w 0) = mk_bv w 0 := by
        simp [muxAt]
      rw [hz]
      refine SEval_call6 hw hav henv hlen hs hk rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | a :: rest, k, _, _, _, _, _, _, _, hb, hw, hav, henv, hlen, hs, hk => by
      by_cases hsk : sel = k
      · subst hsk
        have hz : (if sel ≤ sel then muxAt w arg (a :: rest) (sel - sel) else mk_bv w 0)
            = bv_resize w (arg a) := by simp [muxAt]
        rw [hz]
        refine SEval_call6 hw hav henv hlen hs hk rfl rfl ?_
        refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
        refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim])) ?_
        exact SEval_prim2 (.ref rfl)
          (slot_read hsv (hb a List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl
      · have hstep : (if k ≤ sel then muxAt w arg (a :: rest) (sel - k) else mk_bv w 0)
            = (if k + 1 ≤ sel then muxAt w arg rest (sel - (k + 1)) else mk_bv w 0) := by
          by_cases h1 : k ≤ sel
          · have h2 : k + 1 ≤ sel := by omega
            have h3 : sel - k = (sel - (k + 1)) + 1 := by omega
            simp [h1, h2, h3, muxAt]
          · have h2 : ¬ (k + 1 ≤ sel) := by omega
            simp [h1, h2]
        rw [hstep]
        refine SEval_call6 hw hav henv hlen hs hk rfl rfl ?_
        refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
        refine SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl)
          (by simp [evalPrim] <;> omega)) ?_
        exact muxPick_agree hsv w sel rest (k + 1) _ _ _ _ _ _ _
          (fun y hy => hb y (List.mem_cons_of_mem _ hy))
          (.ref rfl) (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl)
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega))

theorem opMuxN_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opMuxN" [ew, ed, ee, en])
      (encBV (eval_op .Op_MuxN w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      have hval : eval_op .Op_MuxN w ((d :: ds).map arg)
          = muxAt w arg ds (bv_uint (arg d)).toNat := by
        rw [List.map_cons, evalOp_MuxN_cons]
        simp only [muxAt, List.length_map, List.getElem?_map]
        by_cases hlt : (bv_uint (arg d)).toNat < ds.length
        · rw [if_pos hlt, List.getElem?_eq_getElem hlt]; simp
        · rw [if_neg hlt, List.getElem?_eq_none (Nat.le_of_not_lt hlt)]; simp
      rw [hval]
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      have hz : muxAt w arg ds (bv_uint (arg d)).toNat
          = (if 0 ≤ (bv_uint (arg d)).toNat
             then muxAt w arg ds ((bv_uint (arg d)).toNat - 0) else mk_bv w 0) := by
        simp
      rw [hz]
      exact muxPick_agree hsv w (bv_uint (arg d)).toNat ds 0 _ _ _ _ _ _ _
        (fun y hy => hb y (List.mem_cons_of_mem _ hy))
        (.ref rfl) (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (by rw [bv_uint_ofNat_toNat]
            exact SEval_prim1 (slot_read hsv (hb d List.mem_cons_self) (.ref rfl)
              (.ref rfl) (SEval_hd (.ref rfl))) rfl)
        .lit

/-! ### The fixed-arity operators

`Op_Not`, `Op_SRA`, `Op_GetMask`, `Op_MuxBool`, `Op_Sext` and the four
comparisons read a FIXED number of operands.  The pinned `eval_op` matches on
that exact shape and falls through to `mk_bv w 0` at any other length, while
the object reads positionally -- erroring below the arity and ignoring surplus
operands above it.  So each lemma below carries the dep list AS A LITERAL,
which is the exact and minimal hypothesis.  Nothing here weakens or widens
`SupportedByProjection`; whether that hypothesis belongs there is a separate
decision, recorded in the commit message. -/

/-- A width that did not arrive as `Int.ofNat w`.  `Op_Sext` resizes by an
operand VALUE, so the general form is needed exactly once. -/
theorem prim_bvResize_int (v : Int) (a : BV) :
    evalPrim .bvResize [.int v, encBV a] = .ok (encBV (bv_resize v.toNat a)) := rfl

theorem opNot_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : d < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opNot" [ew, ed, ee, en])
      (encBV (eval_op .Op_Not w ([d].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim2 (.ref rfl)
    (slot_read hsv hb (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl))) rfl

theorem opSra_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSra" [ew, ed, ee, en])
      (encBV (eval_op .Op_SRA w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim3 (.ref rfl)
    (slot_read hsv hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
    (slot_read hsv hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl

theorem opGetMask_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opGetMask" [ew, ed, ee, en])
      (encBV (eval_op .Op_GetMask w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim3 (.ref rfl)
    (slot_read hsv hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
    (slot_read hsv hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl

/-- Operands are `[sel, false_v, true_v]` IN THAT ORDER, so the nonzero branch
takes the THIRD.  A polarity slip here is a silent swap. -/
theorem opMuxBool_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 d2 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length) (hb2 : d2 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1, d2]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opMuxBool" [ew, ed, ee, en])
      (encBV (eval_op .Op_MuxBool w ([d0, d1, d2].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  rw [List.map_cons, List.map_cons, List.map_cons, List.map_nil, evalOp_MuxBool]
  refine SEval_ite_of_bool (bb := bv_nonzero (arg d0))
    (nz_agree (slot_read hsv hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))) ?_ ?_
  · intro hT
    simp only [hT, if_true]
    exact SEval_prim2 (.ref rfl)
      (slot_read hsv hb2 (.ref rfl) (.ref rfl)
        (SEval_hd (SEval_tl (SEval_tl (.ref rfl))))) rfl
  · intro hF
    simp only [hF, Bool.false_eq_true, if_false]
    exact SEval_prim2 (.ref rfl)
      (slot_read hsv hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl

/-- Sign extension IS "truncate to the low `n` bits, then read them SIGNED".
The pinned body spells that out as a power/mod/sign formula; nothing here
re-derives it -- `OperatorBridge.evalOp_Sext` carries that obligation. -/
theorem opSext_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSext" [ew, ed, ee, en])
      (encBV (eval_op .Op_Sext w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  rw [List.map_cons, List.map_cons, List.map_nil, evalOp_Sext]
  exact SEval_prim2 (.ref rfl)
    (SEval_prim1
      (SEval_prim2
        (SEval_prim1 (slot_read hsv hb1 (.ref rfl) (.ref rfl)
          (SEval_hd (SEval_tl (.ref rfl)))) rfl)
        (slot_read hsv hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
        (prim_bvResize_int _ _))
      rfl)
    rfl

/-! ### The four comparisons

They differ along exactly two STATIC axes: which reading of the bits
(`signed`) and which way round (`swap`).  There is no "greater" primitive --
GT is LT with the operands exchanged, which is how the pinned model writes it
too. -/

/-- The comparison the object computes, as a `Bool`, so that the two static
axes stay visible and the `ite` below is a Bool test rather than a decidable
proposition. -/
def cmpBool (signed swap : Bool) (a b : BV) : Bool :=
  if signed then (if swap then bv_sint b < bv_sint a else bv_sint a < bv_sint b)
  else (if swap then bv_uint b < bv_uint a else bv_uint a < bv_uint b)

theorem cmpLt_agree {σ : SEnv} {es esw ea eb : SExp} {signed swap : Bool} {a b : BV}
    (hs : SEval hwS σ es (.bool signed)) (hsw : SEval hwS σ esw (.bool swap))
    (ha : SEval hwS σ ea (encBV a)) (hb : SEval hwS σ eb (encBV b)) :
    SEval hwS σ (.call "cmpLt" [es, esw, ea, eb]) (.bool (cmpBool signed swap a b)) := by
  refine SEval_call4 hs hsw ha hb rfl rfl ?_
  simp only [cmpBool]
  cases signed with
  | false =>
      refine SEval.iteF (.ref rfl) ?_
      cases swap with
      | false =>
          exact SEval.iteF (.ref rfl)
            (SEval_prim2 (SEval_prim1 (.ref rfl) rfl) (SEval_prim1 (.ref rfl) rfl) rfl)
      | true =>
          exact SEval.iteT (.ref rfl)
            (SEval_prim2 (SEval_prim1 (.ref rfl) rfl) (SEval_prim1 (.ref rfl) rfl) rfl)
  | true =>
      refine SEval.iteT (.ref rfl) ?_
      cases swap with
      | false =>
          exact SEval.iteF (.ref rfl)
            (SEval_prim2 (SEval_prim1 (.ref rfl) rfl) (SEval_prim1 (.ref rfl) rfl) rfl)
      | true =>
          exact SEval.iteT (.ref rfl)
            (SEval_prim2 (SEval_prim1 (.ref rfl) rfl) (SEval_prim1 (.ref rfl) rfl) rfl)

theorem opCmp_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {signed swap : Bool} {σ : SEnv}
    {ew ed ee en es esw : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool signed)) (hsw : SEval hwS σ esw (.bool swap)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (mk_bv w (if cmpBool signed swap (arg d0) (arg d1) then 1 else 0))) := by
  refine SEval_call6 hw hdv henv hlen hs hsw rfl rfl ?_
  refine SEval_ite_of_bool (bb := cmpBool signed swap (arg d0) (arg d1))
    (cmpLt_agree (.ref rfl) (.ref rfl)
      (slot_read hsv hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
      (slot_read hsv hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))) ?_ ?_
  · intro hT; rw [hT]; exact SEval_prim2 (.ref rfl) .lit rfl
  · intro hF; rw [hF]; exact SEval_prim2 (.ref rfl) .lit rfl

theorem opULT_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool false)) (hsw : SEval hwS σ esw (.bool false)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_ULT w ([d0, d1].map arg))) := by
  have h := opCmp_agree hsv hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_ULT] using h

theorem opUGT_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool false)) (hsw : SEval hwS σ esw (.bool true)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_UGT w ([d0, d1].map arg))) := by
  have h := opCmp_agree hsv hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_UGT] using h

theorem opSLT_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool true)) (hsw : SEval hwS σ esw (.bool false)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_SLT w ([d0, d1].map arg))) := by
  have h := opCmp_agree hsv hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_SLT] using h

theorem opSGT_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool true)) (hsw : SEval hwS σ esw (.bool true)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_SGT w ([d0, d1].map arg))) := by
  have h := opCmp_agree hsv hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_SGT] using h

/-! ### The dispatch

`applyOp`'s seventeen-deep `ite` chain tests a STATIC opcode, so each case is
`k` false tests and one true test, all by `rfl`.  (That staticness is also the
Gate 0 property: none of this chain survives into the residual.)

`ArityOK` is a HYPOTHESIS here, deliberately, and is NOT a field of
`SupportedByProjection`.  The nine fixed-arity operators genuinely need it --
see the commit message for the `Op_Not`-with-two-deps counterexample and for
the census measurement -- but adding it to the shared support predicate changes
the public theorem, which is not this commit's call to make. -/

/-- The dep-list shape each supported operator needs for the object and the
pinned model to agree.  Variable-arity operators impose nothing. -/
def ArityOK : LGraphOp → List Nat → Prop
  | .Op_Not,     ds => ds.length = 1
  | .Op_SRA,     ds => ds.length = 2
  | .Op_GetMask, ds => ds.length = 2
  | .Op_MuxBool, ds => ds.length = 3
  | .Op_Sext,    ds => ds.length = 2
  | .Op_ULT,     ds => ds.length = 2
  | .Op_UGT,     ds => ds.length = 2
  | .Op_SLT,     ds => ds.length = 2
  | .Op_SGT,     ds => ds.length = 2
  | _,           _  => True

instance : ∀ op ds, Decidable (ArityOK op ds)
  | .Op_Not,     _ => inferInstanceAs (Decidable (_ = _))
  | .Op_SRA,     _ => inferInstanceAs (Decidable (_ = _))
  | .Op_GetMask, _ => inferInstanceAs (Decidable (_ = _))
  | .Op_MuxBool, _ => inferInstanceAs (Decidable (_ = _))
  | .Op_Sext,    _ => inferInstanceAs (Decidable (_ = _))
  | .Op_ULT,     _ => inferInstanceAs (Decidable (_ = _))
  | .Op_UGT,     _ => inferInstanceAs (Decidable (_ = _))
  | .Op_SLT,     _ => inferInstanceAs (Decidable (_ = _))
  | .Op_SGT,     _ => inferInstanceAs (Decidable (_ = _))
  | .Op_Const _, _ => inferInstanceAs (Decidable True)
  | .Op_Sum _,   _ => inferInstanceAs (Decidable True)
  | .Op_Sub,     _ => inferInstanceAs (Decidable True)
  | .Op_Mult,    _ => inferInstanceAs (Decidable True)
  | .Op_Div,     _ => inferInstanceAs (Decidable True)
  | .Op_UDiv,    _ => inferInstanceAs (Decidable True)
  | .Op_SDiv,    _ => inferInstanceAs (Decidable True)
  | .Op_And,     _ => inferInstanceAs (Decidable True)
  | .Op_Or,      _ => inferInstanceAs (Decidable True)
  | .Op_Xor,     _ => inferInstanceAs (Decidable True)
  | .Op_Ror,     _ => inferInstanceAs (Decidable True)
  | .Op_LT,      _ => inferInstanceAs (Decidable True)
  | .Op_GT,      _ => inferInstanceAs (Decidable True)
  | .Op_EQ,      _ => inferInstanceAs (Decidable True)
  | .Op_SHL,     _ => inferInstanceAs (Decidable True)
  | .Op_MuxN,    _ => inferInstanceAs (Decidable True)
  | .Op_SetMask, _ => inferInstanceAs (Decidable True)
  | .Op_MemRead, _ => inferInstanceAs (Decidable True)
  | .Op_MemWrite, _ => inferInstanceAs (Decidable True)
  | .Op_MemWriteBE _, _ => inferInstanceAs (Decidable True)

theorem list_len_one {α : Type} : ∀ {l : List α}, l.length = 1 → ∃ a, l = [a]
  | [a], _ => ⟨a, rfl⟩

theorem list_len_two {α : Type} : ∀ {l : List α}, l.length = 2 → ∃ a b, l = [a, b]
  | [a, b], _ => ⟨a, b, rfl⟩

theorem list_len_three {α : Type} :
    ∀ {l : List α}, l.length = 3 → ∃ a b c, l = [a, b, c]
  | [a, b, c], _ => ⟨a, b, c, rfl⟩

theorem applyOp_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {op : LGraphOp} {w : Nat} {deps : List Nat} {σ : SEnv} {eo ew ed ee en : SExp}
    (hsup : OpSupported op = true) (harity : ArityOK op deps)
    (hb : ∀ d ∈ deps, d < vals.length)
    (hop : SEval hwS σ eo (encOp op))
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "applyOp" [eo, ew, ed, ee, en])
      (encBV (eval_op op w (deps.map arg))) := by
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  cases op with
  | Op_And =>
      exact SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opAnd_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
  | Op_Or =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opOr_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))
  | Op_SRA =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSra_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))
  | Op_GetMask =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opGetMask_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))
  | Op_Xor =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opXor_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))
  | Op_Not =>
      obtain ⟨d0, rfl⟩ := list_len_one harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opNot_agree hsv (hb d0 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))
  | Op_Sum nAdd =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSum_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))
  | Op_EQ =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opEq_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))
  | Op_Ror =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opRor_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))
  | Op_MuxBool =>
      obtain ⟨d0, d1, d2, rfl⟩ := list_len_three harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opMuxBool_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (hb d2 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))))
  | Op_MuxN =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opMuxN_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))))
  | Op_ULT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opULT_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit))))))))))))
  | Op_UGT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opUGT_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit)))))))))))))
  | Op_SLT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSLT_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit))))))))))))))
  | Op_SGT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSGT_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit)))))))))))))))
  | Op_Sext =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSext_agree hsv (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))))))))))
  | Op_SHL =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opShl_agree hsv hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))))))))))
  | Op_Const c => exact absurd hsup (by simp [OpSupported])
  | Op_Sub => exact absurd hsup (by simp [OpSupported])
  | Op_Mult => exact absurd hsup (by simp [OpSupported])
  | Op_Div => exact absurd hsup (by simp [OpSupported])
  | Op_UDiv => exact absurd hsup (by simp [OpSupported])
  | Op_SDiv => exact absurd hsup (by simp [OpSupported])
  | Op_LT => exact absurd hsup (by simp [OpSupported])
  | Op_GT => exact absurd hsup (by simp [OpSupported])
  | Op_SetMask => exact absurd hsup (by simp [OpSupported])
  | Op_MemRead => exact absurd hsup (by simp [OpSupported])
  | Op_MemWrite => exact absurd hsup (by simp [OpSupported])
  | Op_MemWriteBE b => exact absurd hsup (by simp [OpSupported])

/-! ## Group 4: the node fold

The object walks the dense node array in order, consing each value onto its
environment.  The shared side evaluates `evalGraphG` over the SAME order
(`topo` is `slotsFrom`, so dense indices are already topological).

Rather than build a second graph denotation and prove it equal, the object's
environment is described directly in terms of `evalGraphG`'s own answer: slot
`k` holds `encBV (rho k).asBV`.  The one fact that makes the induction go
through is the FIXPOINT property of `evalGraphG` -- that its own result
satisfies the local recurrence -- which `GraphRefine` assumes but never states,
so it is proved here (over the shared definitions, editing nothing). -/

section GraphFix
variable {V : Type} [NodeSemantics V]

/-- `evalGraphG`'s own result satisfies the per-node recurrence.  `GraphRefine`
proves the converse (`evalGraphG_of_localAgree`: anything satisfying the
recurrence IS `evalGraphG`); this is the direction an interpreter needs. -/
theorem evalGraphG_rec (G : GraphCert) :
    ∀ (ns : List Nat) (e : Nat → V), ns.Nodup → GraphRefine.DepOrdered G ns →
      (∀ n ∈ ns, (G.nodes n).isSome) →
      ∀ n ∈ ns, evalGraphG ns G e n = evalNodeG G (evalGraphG ns G e) n := by
  intro ns
  induction ns with
  | nil => intro _ _ _ _ n hn; simp at hn
  | cons m ms ih =>
      intro e hnodup hdepord hsome n hn
      have hm_notin : m ∉ ms := (List.nodup_cons.mp hnodup).1
      have hstep : evalGraphG (m :: ms) G e
          = evalGraphG ms G (envSetG e m (evalNodeG G e m)) := rfl
      have hR : ∀ d, d ∉ ms →
          evalGraphG ms G (envSetG e m (evalNodeG G e m)) d
            = envSetG e m (evalNodeG G e m) d :=
        fun d hd => GraphRefine.evalGraphG_not_mem G _ ms d hd
      cases List.mem_cons.mp hn with
      | inl hnm =>
          subst hnm
          rw [hstep]
          have h1 : evalGraphG ms G (envSetG e n (evalNodeG G e n)) n = evalNodeG G e n := by
            rw [hR n hm_notin]; simp [envSetG]
          rw [h1]
          refine GraphRefine.evalNodeG_congr_some G e _ n (hsome n List.mem_cons_self) ?_
          intro d hd
          have hdnot : d ∉ (n :: ms) := hdepord.1 d hd
          have hd1 : d ∉ ms := fun hc => hdnot (List.mem_cons_of_mem _ hc)
          have hd2 : d ≠ n := fun hc => hdnot (hc ▸ List.mem_cons_self)
          rw [hR d hd1]
          simp [envSetG, hd2]
      | inr hnms =>
          rw [hstep]
          exact ih _ (List.nodup_cons.mp hnodup).2 hdepord.2
            (fun n' h => hsome n' (List.mem_cons_of_mem _ h)) n hnms

end GraphFix

/-- Every supported operator is a non-memory operator, so `eval_op_cert`
delegates to `eval_op`.  The memory operators are the only ones that do not,
and `OpSupported` excludes all three. -/
theorem evalOpCert_of_supported {op : LGraphOp} (h : OpSupported op = true)
    (w : Nat) (vs : List CertVal) :
    eval_op_cert op w vs = .bv (eval_op op w (vs.map CertVal.asBV)) := by
  cases op <;> first | rfl | exact absurd h (by simp [OpSupported])

/-! ### The environment, described through `evalGraphG` -/

/-- Slot `k`'s value, as the shared semantics computes it. -/
def slotVal (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) (k : Nat) : BV :=
  (evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s) k).asBV

/-- The first `m` slots, in SLOT ORDER.  The object holds its reverse. -/
def prefixVals (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) (m : Nat) : List Val :=
  (List.range m).map (fun k => encBV (slotVal D i s k))

@[simp] theorem prefixVals_length (D : DesignCert) (i : RuntimeInput)
    (s : RuntimeState) (m : Nat) : (prefixVals D i s m).length = m := by
  simp [prefixVals]

theorem prefixVals_slotVals (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (m : Nat) : SlotVals (prefixVals D i s m) (slotVal D i s) := by
  intro k hk
  simp [prefixVals]

theorem prefixVals_succ (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (m : Nat) :
    prefixVals D i s (m + 1) = prefixVals D i s m ++ [encBV (slotVal D i s m)] := by
  simp [prefixVals, List.range_succ]

/-- The source prefix of the environment IS what `mkSources` built. -/
theorem prefixVals_sources (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) :
    prefixVals D i s D.sources.size
      = D.sources.toList.map (fun sd => encBV (sourceValue i s sd).asBV) := by
  apply List.ext_getElem
  · simp
  · intro k h1 h2
    have hk : k < D.sources.size := by simpa [prefixVals] using h1
    have hnot : k ∉ D.toGraphCert.topo := DesignCert.source_not_mem_topo (by simpa using hk)
    simp only [prefixVals, List.getElem_map, List.getElem_range, slotVal]
    rw [GraphRefine.evalGraphG_not_mem _ _ _ _ hnot]
    simp [srcEnv, Array.getElem?_eq_getElem hk]

/-! ### The local recurrence, at one node -/

theorem nodeVal_rec {D : DesignCert} {i : RuntimeInput} {s : RuntimeState}
    (hwf : DesignCert.DesignCertWF D) {j : Nat} {c : DenseNodeCert}
    (hj : j < D.nodes.size) (hc : D.nodes[j]? = some c)
    (hsup : OpSupported c.op = true) :
    eval_op c.op c.width (c.deps.toList.map (slotVal D i s))
      = slotVal D i s (D.slotOfNode j) := by
  have hfacts := DesignCert.wf_graph_facts hwf
  have hmem : D.slotOfNode j ∈ D.toGraphCert.topo :=
    DesignCert.slotOfNode_mem_topo (by simpa [DesignCert.numNodes] using hj)
  have hrec := evalGraphG_rec (V := CertVal) D.toGraphCert D.toGraphCert.topo
    (srcEnv D i s) hfacts.1 hfacts.2.1 hfacts.2.2 _ hmem
  have hnode : D.toGraphCert.nodes (D.slotOfNode j)
      = some ⟨D.slotOfNode j, c.op, c.width, c.deps.toList⟩ := by
    simp [DesignCert.toGraphCert, DesignCert.nodeAt?_slotOfNode, hc]
  have hR : slotVal D i s (D.slotOfNode j)
      = (evalNodeG D.toGraphCert
          (evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s))
          (D.slotOfNode j)).asBV := by
    simp only [slotVal]; rw [hrec]
  rw [hR]
  simp only [evalNodeG, hnode]
  show _ = (eval_op_cert c.op c.width
    (c.deps.toList.map (evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s)))).asBV
  rw [evalOpCert_of_supported hsup]
  show eval_op c.op c.width (c.deps.toList.map (slotVal D i s))
     = eval_op c.op c.width
         ((c.deps.toList.map
            (evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s))).map CertVal.asBV)
  rw [List.map_map]
  rfl

/-! ### `evalNode` and the fold -/

theorem evalNode_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {c : DenseNodeCert} {σ : SEnv} {end_ eenv en : SExp}
    (hsup : OpSupported c.op = true) (harity : ArityOK c.op c.deps.toList)
    (hb : ∀ d ∈ c.deps.toList, d < vals.length)
    (hnd : SEval hwS σ end_ (encNode c))
    (henv : SEval hwS σ eenv (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "evalNode" [end_, eenv, en])
      (encBV (eval_op c.op c.width (c.deps.toList.map arg))) := by
  refine SEval_call3 hnd henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  exact applyOp_agree hsv hsup harity hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
    (.ref rfl)

theorem evalNodes_agree {D : DesignCert} {i : RuntimeInput} {s : RuntimeState}
    (hwf : DesignCert.DesignCertWF D)
    (hops : ∀ c ∈ D.nodes.toList, OpSupported c.op = true)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList) :
    ∀ (r j : Nat), j + r = D.nodes.size →
      ∀ (σ : SEnv) (eno eenv en : SExp),
        SEval hwS σ eno (encListG encNode (D.nodes.toList.drop j)) →
        SEval hwS σ eenv (objEnv (prefixVals D i s (D.sources.size + j))) →
        SEval hwS σ en (.int (Int.ofNat (D.sources.size + j))) →
        SEval hwS σ (.call "evalNodes" [eno, eenv, en])
          (objEnv (prefixVals D i s (D.sources.size + D.nodes.size)))
  | 0,     j, hr, _, _, _, _, hno, henv, hlen => by
      have hj : j = D.nodes.size := by omega
      subst hj
      refine SEval_call3 hno henv hlen rfl rfl ?_
      refine SEval.iteT (SEval_prim1 (.ref rfl) ?_) (.ref rfl)
      have : D.nodes.toList.drop D.nodes.size = [] := by simp
      rw [this]
      rfl
  | r + 1, j, hr, _, _, _, _, hno, henv, hlen => by
      have hj : j < D.nodes.size := by omega
      have hjl : j < D.nodes.toList.length := by simpa using hj
      have hdrop : D.nodes.toList.drop j
          = D.nodes.toList[j] :: D.nodes.toList.drop (j + 1) :=
        List.drop_eq_getElem_cons hjl
      have hcj : D.nodes[j]? = some D.nodes.toList[j] := by
        rw [Array.getElem?_eq_getElem hj]; simp
      have hmemc : D.nodes.toList[j] ∈ D.nodes.toList := List.getElem_mem hjl
      rw [hdrop] at hno
      refine SEval_call3 hno henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      have hbnd : ∀ d ∈ (D.nodes.toList[j]).deps.toList,
          d < (prefixVals D i s (D.sources.size + j)).length := by
        intro d hd
        have := hwf.depsBounded j D.nodes.toList[j] hcj d hd
        simpa using this
      have hnext : objEnv (prefixVals D i s (D.sources.size + j + 1))
          = Val.cons (encBV (slotVal D i s (D.slotOfNode j)))
              (objEnv (prefixVals D i s (D.sources.size + j))) := by
        simp [objEnv, prefixVals_succ, DesignCert.slotOfNode, encListG]
      refine evalNodes_agree hwf hops harity r (j + 1) (by omega) _ _ _ _ ?_ ?_ ?_
      · exact SEval_tl (.ref rfl)
      · rw [show D.sources.size + (j + 1) = D.sources.size + j + 1 from rfl, hnext]
        refine SEval_consP ?_ (.ref rfl)
        rw [← nodeVal_rec hwf hj hcj (hops _ hmemc)]
        refine evalNode_agree (prefixVals_slotVals D i s (D.sources.size + j))
          (hops _ hmemc) (harity _ hmemc) hbnd (SEval_hd (.ref rfl)) (.ref rfl) ?_
        rw [prefixVals_length]
        exact .ref rfl
      · exact SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega)

/-! ## Group 5: outputs

Order and width resize, exactly.  `mkOutputs` walks `D.outputs` in order and
resizes each slot read to the output's own width -- the same `bv_resize
o.width` the shared side applies, so this lemma checks the ORDER and the
RESIZE and contains nothing else. -/

theorem mkOutputs_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg) :
    ∀ (outs : List OutputDesc) (σ : SEnv) (eo ee en : SExp),
      (∀ o ∈ outs, o.slot < vals.length) →
      SEval hwS σ eo (encListG encOutput outs) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "mkOutputs" [eo, ee, en])
        (encListG encBV (outs.map (fun o => bv_resize o.width (arg o.slot))))
  | [],      _, _, _, _, _,  ho, henv, hlen => by
      refine SEval_call3 ho henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | o :: os, _, _, _, _, hb, ho, henv, hlen => by
      refine SEval_call3 ho henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval_switch_of_tag (SEval_hd (.ref rfl)) rfl rfl ?_
      exact SEval_consP
        (SEval_prim2 (.ref rfl)
          (slot_read hsv (hb o List.mem_cons_self) (.ref rfl) (.ref rfl) (.ref rfl)) rfl)
        (mkOutputs_agree hsv os _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))

/-! ## Group 6: flop updates

Reset polarity, async-vs-sync edge priority, then enable/din/old-state hold --
in that order, because that is the order `srcFlopNext` tests them.  Every
right-hand side reads the OLD state: `fq` is the incoming `s.flops` and nothing
in this group reads a value it has just written. -/

theorem firesAt_agree {σ : SEnv} {ee ec : SExp} {e : ClockEdges} {c : Nat}
    (hb : c < e.size)
    (hev : SEval hwS σ ee (encEdges e)) (hcv : SEval hwS σ ec (.int (Int.ofNat c))) :
    SEval hwS σ (.call "firesAt" [ee, ec]) (.bool (fires e c)) := by
  have hf : fires e c = e.toList[c]'(by simpa using hb) := by
    simp [fires, Array.getElem?_eq_getElem hb]
  rw [hf]
  refine SEval_call2 hev hcv rfl rfl ?_
  exact nthD_agree (e := fun b : Bool => (Val.bool b)) c _ _ _ e.toList
    (by simpa using hb) (.ref rfl) (.ref rfl)

/-- The enable and reset conditions, NAMED.  Written as matches inline they
become dependent matches the moment a hypothesis mentions the same `Option`,
and the per-flop statements stop matching `srcFlopNext`. -/
def enabledOf (arg : Nat → BV) : Option Nat → Bool
  | none   => true
  | some x => bv_nonzero (arg x)

def rstOf (arg : Nat → BV) (al : Bool) : Option Nat → Bool
  | none   => false
  | some r => xor al (bv_nonzero (arg r))

/-- `srcFlopNext` through those names.  `rfl`: naming changed nothing. -/
theorem srcFlopNext_named (rho : Nat → CertVal) (e : ClockEdges) (st : RuntimeState)
    (idx : Nat) (f : FlopDesc) :
    srcFlopNext rho e st idx f
      = (if rstOf (fun k => (rho k).asBV) f.resetActiveLow f.resetPin
             && (fires e f.clock || f.asyncReset)
          then mk_bv f.width f.resetValue
          else if fires e f.clock && enabledOf (fun k => (rho k).asBV) f.enable
               then bv_resize f.width (rho f.din).asBV
               else st.flops[idx]?.getD (mk_bv f.width 0)) := rfl

theorem rstActive_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {σ : SEnv} {erp eal ee enn : SExp} {rp : Option Nat} {al : Bool}
    (hbnd : ∀ r, rp = some r → r < vals.length)
    (hrp : SEval hwS σ erp (encONat rp)) (hal : SEval hwS σ eal (.bool al))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ enn (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "rstActive" [erp, eal, ee, enn]) (.bool (rstOf arg al rp)) := by
  cases rp with
  | none =>
      refine SEval_call4 hrp hal henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | some r =>
      refine SEval_call4 hrp hal henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      cases al with
      | false =>
          simp only [rstOf, Bool.false_xor]
          exact SEval.iteF (.ref rfl)
            (nz_agree (slot_read hsv (hbnd r rfl) (.ref rfl) (.ref rfl)
              (SEval_hd (.ref rfl))))
      | true =>
          simp only [rstOf, Bool.true_xor]
          exact SEval.iteT (.ref rfl)
            (SEval_prim1 (nz_agree (slot_read hsv (hbnd r rfl) (.ref rfl) (.ref rfl)
              (SEval_hd (.ref rfl)))) rfl)

theorem capture_agree {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {σ : SEnv} {een eedge ew edin ee enn efq ei : SExp}
    {enOpt : Option Nat} {edge : Bool} {w din idx : Nat} {st : RuntimeState}
    (hben : ∀ x, enOpt = some x → x < vals.length)
    (hbdin : din < vals.length) (hbidx : idx < st.flops.size)
    (h1 : SEval hwS σ een (encONat enOpt))
    (h2 : SEval hwS σ eedge (.bool edge))
    (h3 : SEval hwS σ ew (.int (Int.ofNat w)))
    (h4 : SEval hwS σ edin (.int (Int.ofNat din)))
    (h5 : SEval hwS σ ee (objEnv vals))
    (h6 : SEval hwS σ enn (.int (Int.ofNat vals.length)))
    (h7 : SEval hwS σ efq (encBVs st.flops))
    (h8 : SEval hwS σ ei (.int (Int.ofNat idx))) :
    SEval hwS σ (.call "capture" [een, eedge, ew, edin, ee, enn, efq, ei])
      (encBV (if edge && enabledOf arg enOpt
              then bv_resize w (arg din)
              else st.flops[idx]?.getD (mk_bv w 0))) := by
  have hold : st.flops[idx]?.getD (mk_bv w 0) = st.flops[idx] := by
    simp [Array.getElem?_eq_getElem hbidx]
  rw [hold]
  refine SEval_call8 h1 h2 h3 h4 h5 h6 h7 h8 rfl rfl ?_
  cases enOpt with
  | none =>
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [enabledOf, Bool.and_true]
      cases edge with
      | false =>
          simp only [Bool.false_eq_true, if_false]
          exact SEval.iteF (.ref rfl) (nthD_arr hbidx (.ref rfl) (.ref rfl))
      | true =>
          simp only [if_true]
          exact SEval.iteT (.ref rfl)
            (SEval_prim2 (.ref rfl)
              (slot_read hsv hbdin (.ref rfl) (.ref rfl) (.ref rfl)) rfl)
  | some x =>
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [enabledOf]
      refine SEval_ite_of_bool (bb := edge && bv_nonzero (arg x))
        (SEval_prim2 (.ref rfl)
          (nz_agree (slot_read hsv (hben x rfl) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl)))) rfl) ?_ ?_
      · intro hT
        simp only [hT, if_true]
        exact SEval_prim2 (.ref rfl)
          (slot_read hsv hbdin (.ref rfl) (.ref rfl) (.ref rfl)) rfl
      · intro hF
        simp only [hF, Bool.false_eq_true, if_false]
        exact nthD_arr hbidx (.ref rfl) (.ref rfl)

theorem flopNext_agree {vals : List Val} {rho : Nat → CertVal}
    (hsv : SlotVals vals (fun k => (rho k).asBV))
    {e : ClockEdges} {st : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hck : f.clock < e.size) (hdin : f.din < vals.length)
    (hen : ∀ x, f.enable = some x → x < vals.length)
    (hrpb : ∀ r, f.resetPin = some r → r < vals.length)
    (hidx : idx < st.flops.size)
    {σ : SEnv} {ef eedge ee enn efq ei : SExp}
    (h1 : SEval hwS σ ef (encFlop f))
    (h2 : SEval hwS σ eedge (encEdges e))
    (h3 : SEval hwS σ ee (objEnv vals))
    (h4 : SEval hwS σ enn (.int (Int.ofNat vals.length)))
    (h5 : SEval hwS σ efq (encBVs st.flops))
    (h6 : SEval hwS σ ei (.int (Int.ofNat idx))) :
    SEval hwS σ (.call "flopNext" [ef, eedge, ee, enn, efq, ei])
      (encBV (srcFlopNext rho e st idx f)) := by
  rw [srcFlopNext_named]
  refine SEval_call6 h1 h2 h3 h4 h5 h6 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.letN (firesAt_agree hck (.ref rfl) (.ref rfl)) ?_
  refine SEval.letN (capture_agree hsv hen hdin hidx (.ref rfl) (.ref rfl) (.ref rfl)
    (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)) ?_
  cases hrp : f.resetPin with
  | none =>
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [rstOf, Bool.false_and, Bool.false_eq_true, if_false]
      exact .ref rfl
  | some r =>
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      cases har : f.asyncReset with
      | true =>
          simp only [Bool.or_true, Bool.and_true]
          refine SEval.iteT (.ref rfl) ?_
          refine SEval_ite_of_bool
            (bb := rstOf (fun k => (rho k).asBV) f.resetActiveLow (some r)) ?_ ?_ ?_
          · rw [← hrp]
            exact rstActive_agree hsv hrpb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
          · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) (.ref rfl) rfl
          · intro hF
            simp only [hF, Bool.false_eq_true, if_false]
            exact .ref rfl
      | false =>
          simp only [Bool.or_false]
          refine SEval.iteF (.ref rfl) ?_
          refine SEval_ite_of_bool
            (bb := rstOf (fun k => (rho k).asBV) f.resetActiveLow (some r) && fires e f.clock)
            (SEval_prim2 (a := Val.bool (rstOf (fun k => (rho k).asBV) f.resetActiveLow (some r)))
              ?_ (.ref rfl) rfl) ?_ ?_
          · rw [← hrp]
            exact rstActive_agree hsv hrpb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
          · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) (.ref rfl) rfl
          · intro hF
            simp only [hF, Bool.false_eq_true, if_false]
            exact .ref rfl

/-- The flop vector as the object builds it: one `srcFlopNext` per flop, with
the index counting up.  Spelled as its own recursion rather than through
`mapIdx` so that the fold lemma is the recursion itself and the bridge to
`Array.mapIdx` is one separate, elementwise statement. -/
def flopNextsFrom (rho : Nat → CertVal) (e : ClockEdges) (st : RuntimeState) :
    Nat → List FlopDesc → List BV
  | _,   []      => []
  | idx, f :: fs => srcFlopNext rho e st idx f :: flopNextsFrom rho e st (idx + 1) fs

@[simp] theorem flopNextsFrom_length (rho : Nat → CertVal) (e : ClockEdges)
    (st : RuntimeState) :
    ∀ (idx : Nat) (fl : List FlopDesc), (flopNextsFrom rho e st idx fl).length = fl.length
  | _,   []      => rfl
  | idx, _ :: fs => by
      simp only [flopNextsFrom, List.length_cons, flopNextsFrom_length rho e st (idx + 1) fs]

theorem flopNextsFrom_getElem (rho : Nat → CertVal) (e : ClockEdges) (st : RuntimeState) :
    ∀ (idx : Nat) (fl : List FlopDesc) (k : Nat) (h : k < fl.length),
      (flopNextsFrom rho e st idx fl)[k]'(by simpa using h)
        = srcFlopNext rho e st (idx + k) (fl[k]'h)
  | _,   [],      _,     h => absurd h (by simp)
  | idx, _ :: _,  0,     _ => by simp [flopNextsFrom]
  | idx, _ :: fs, n + 1, h => by
      have hn : n < fs.length := by simpa using h
      have := flopNextsFrom_getElem rho e st (idx + 1) fs n hn
      simp only [flopNextsFrom, List.getElem_cons_succ, this]
      congr 1
      omega

theorem flopNexts_agree {vals : List Val} {rho : Nat → CertVal}
    (hsv : SlotVals vals (fun k => (rho k).asBV)) {e : ClockEdges} {st : RuntimeState} :
    ∀ (fl : List FlopDesc) (idx : Nat) (σ : SEnv) (ef eedge ee enn efq ei : SExp),
      (∀ f ∈ fl, f.clock < e.size ∧ f.din < vals.length ∧
         (∀ x, f.enable = some x → x < vals.length) ∧
         (∀ r, f.resetPin = some r → r < vals.length)) →
      idx + fl.length ≤ st.flops.size →
      SEval hwS σ ef (encListG encFlop fl) →
      SEval hwS σ eedge (encEdges e) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ enn (.int (Int.ofNat vals.length)) →
      SEval hwS σ efq (encBVs st.flops) →
      SEval hwS σ ei (.int (Int.ofNat idx)) →
      SEval hwS σ (.call "flopNexts" [ef, eedge, ee, enn, efq, ei])
        (encListG encBV (flopNextsFrom rho e st idx fl))
  | [],      _,   _, _, _, _, _, _, _, _,  _,    hfv, hev, henv, hlen, hfq, hi => by
      refine SEval_call6 hfv hev henv hlen hfq hi rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | f :: fs, idx, _, _, _, _, _, _, _, hb, hidx, hfv, hev, henv, hlen, hfq, hi => by
      refine SEval_call6 hfv hev henv hlen hfq hi rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      obtain ⟨hck, hdin, hen, hrp⟩ := hb f List.mem_cons_self
      have hidx0 : idx < st.flops.size := by simp only [List.length_cons] at hidx; omega
      exact SEval_consP
        (flopNext_agree hsv hck hdin hen hrp hidx0 (SEval_hd (.ref rfl)) (.ref rfl)
          (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
        (flopNexts_agree hsv fs (idx + 1) _ _ _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (by simp only [List.length_cons] at hidx; omega)
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega)))

end Hw
end Projection
