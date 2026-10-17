/-
# General source-layer preservation for the ACTUAL `hwMemS` / `hwMemP`

This module proves, over the real extended program -- not over a fixture and
not over a hand-written copy of its functions -- that

1. `hwMemS` RESOLVES freshly (`hwMemResolved_ok`), so `hwMemP` is the program
   `resolveProgram` actually produces;
2. every function of the original `hwS` is still FOUND BY NAME in `hwMemS`
   (`sFn_lift`), and therefore every `SEval hwS` derivation transports
   (`SEval_mono`, `lift`) -- including the recursive callees `srcVal`, `nthD`
   and `nz` that the lifted lemmas invoke.  This is proved from the program
   structure, not inferred from numeric ids: `sFn` routes through `idxOf` on the
   NAME list, and `sFn_of_prefix` shows a name found in a prefix is found, at
   the same position, with the SAME function record, in the extension;
3. `memAt` preserves the memory list: the EXACT selected image when the index is
   present, and the EXACT absent image (`[]`, zero-width zero -- matching
   `sourceValue`'s `(fun _ => mk_bv 0 0)`) when it is not, for every encoded
   image list and every `Int` index, negative ones included;
4. `romWords` preserves ROM contents: one word per content entry, in order, each
   `mk_bv dw c` -- so the word count is `contents.size`, never `2 ^ aw`;
5. `srcValM` agrees with `Compiler.sourceValue` on ALL SIX source forms
   (`srcValM_agree`), the two memory forms included, under the ORIGINAL bounds
   premises alone.  `SourceSupported` is NOT a premise: that is the point of the
   memory route.  The memory answers are related to the reference by
   `MemImg.denote`, through the already-proved `srcMemImg_denote` and
   `ofMemConst_denote`.

## Scope, stated exactly

* These are statements about `hwMemS`, the NAMED program, crossed to `hwMemP`
  through the repository's production bridge `Surface.SEval_sound` -- the same
  route `HardwareAdequacy` uses for `hwS`.  `SEval_sound` is proved without
  mentioning `resolve` or `Eval` on its left-hand side, so the crossing is a
  real check of binder order, not a tautology.
* The theorems quantify over arbitrary encoded image lists, arbitrary ROM
  contents, arbitrary `StateM`s and arbitrary `RuntimeInput`s.  The worked
  two-clock fixture in `HardwareMemCycle` is an execution witness, not the
  statement.
* `applyOpM`, the typed slot/value relation, `mkSourcesM`, `evalNodesM`,
  `memNext(s)` and whole-`mainM` adequacy are NOT proved here.  No support,
  admission or whole-step credit follows from this module; `SourceSupported`,
  `OpSupported` and `memFree` are untouched and the public route still refuses
  memory-bearing designs.
-/
import LeanSemanticPrimitives.Projection.HardwareMem
import LeanSemanticPrimitives.Projection.HardwareAdequacy
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. The extended program resolves

Exactly the `hwResolved_ok` pattern: `hwMemP`/`hwMemInl` are DEFINED by matching
on `hwMemResolved`, so the equation holds as soon as the error branch is
excluded, and the error branch is excluded by one kernel reduction. -/

theorem hwMemResolved_isOk : Option.isSome (Except.toOption hwMemResolved) = true := rfl

theorem hwMemResolved_ok : hwMemResolved = .ok (hwMemP, hwMemInl) := by
  have h0 := hwMemResolved_isOk
  unfold hwMemP hwMemInl
  cases h : hwMemResolved with
  | ok pi   => cases pi; rfl
  | error e => rw [h] at h0; simp [Except.toOption] at h0

/-- Fresh resolution of the ACTUAL extended program.  Every function id is
assigned by this call; none is carried over from `hwP`. -/
theorem hwMemP_resolves : resolveProgram hwMemS = .ok (hwMemP, hwMemInl) := hwMemResolved_ok

/-! ## 2. Name/function correspondence, from the program structure

`sFn P f` is `idxOf (funNames P) f >>= (P.funs[·]?)`.  A name present in a
PREFIX of the function list is found by `idxOf` at the same position, and that
position still indexes the same record after appending -- so the extension
returns the SAME `SFun`, not merely the same number. -/

private theorem idxOf_lt : ∀ {xs : List String} {f : String} {i : Nat},
    idxOf xs f = some i → i < xs.length
  | [],      _, _, h => by simp [idxOf] at h
  | x :: xs, f, i, h => by
      simp only [idxOf] at h
      split at h
      · cases h; simp
      · simp only [Option.map_eq_some_iff] at h
        obtain ⟨j, hj, rfl⟩ := h
        have := idxOf_lt hj
        simp only [List.length_cons]
        omega

private theorem idxOf_append : ∀ {xs ys : List String} {f : String} {i : Nat},
    idxOf xs f = some i → idxOf (xs ++ ys) f = some i
  | [],      _,  _, _, h => by simp [idxOf] at h
  | x :: xs, ys, f, i, h => by
      simp only [idxOf] at h
      simp only [List.cons_append, idxOf]
      split at h
      · rename_i hx
        rw [if_pos hx]; exact h
      · rename_i hx
        rw [if_neg hx]
        simp only [Option.map_eq_some_iff] at h
        obtain ⟨j, hj, rfl⟩ := h
        rw [idxOf_append hj]
        simp

/-- **Preservation of function lookup.**  Stated over an arbitrary extension, so
it is the program SHAPE that carries it, not the particular ten functions added.
The conclusion pins the same `fd`: a preserved index alone would not say the
extended program runs the same body. -/
theorem sFn_of_prefix {P Q : SProgram} (hpre : ∃ ext : List SFun, Q.funs = P.funs ++ ext)
    {f : String} {fd : SFun} (h : sFn P f = some fd) : sFn Q f = some fd := by
  obtain ⟨ext, hq⟩ := hpre
  unfold sFn funNames at h ⊢
  cases hi : idxOf (P.funs.map SFun.name) f with
  | none    => rw [hi] at h; simp at h
  | some i  =>
      rw [hi] at h
      -- `(some i).bind g` reduces to `g i`
      have h' : P.funs[i]? = some fd := h
      have hlt : i < P.funs.length := by simpa using idxOf_lt hi
      rw [hq, List.map_append, idxOf_append hi]
      show (P.funs ++ ext)[i]? = some fd
      rw [List.getElem?_append_left hlt]
      exact h'

/-- `hwMemS` extends `hwS`: its function list is `hwS`'s with ten appended, by
definition -- no existing entry is edited or reordered. -/
theorem hwMemS_prefix : ∃ ext : List SFun, hwMemS.funs = hwS.funs ++ ext := ⟨_, rfl⟩

/-- Every original function is found, by NAME, with the SAME body, in `hwMemS`. -/
theorem sFn_lift {f : String} {fd : SFun} (h : sFn hwS f = some fd) :
    sFn hwMemS f = some fd := sFn_of_prefix hwMemS_prefix h

/-! ## 3. Semantic transport

`SEval P` mentions `P` in exactly one place -- the `call` rule's `sFn P f` -- so
preservation of function lookup is exactly what is needed to replay a whole
derivation in the extended program, recursive callees included. -/

mutual

theorem SEval_mono {P Q : SProgram}
    (hF : ∀ (f : String) (fd : SFun), sFn P f = some fd → sFn Q f = some fd) :
    ∀ (σ : SEnv) (e : SExp) (v : Val), SEval P σ e v → SEval Q σ e v
  | _, _, _, .lit                => .lit
  | _, _, _, .ref hx             => .ref hx
  | _, _, _, .letN he hb         => .letN (SEval_mono hF _ _ _ he) (SEval_mono hF _ _ _ hb)
  | _, _, _, .iteT hc ha         => .iteT (SEval_mono hF _ _ _ hc) (SEval_mono hF _ _ _ ha)
  | _, _, _, .iteF hc hb         => .iteF (SEval_mono hF _ _ _ hc) (SEval_mono hF _ _ _ hb)
  | _, _, _, .prim hes hp        => .prim (SEvalList_mono hF _ _ _ hes) hp
  | _, _, _, .mk hes             => .mk (SEvalList_mono hF _ _ _ hes)
  | _, _, _, .switch hs hf hl hb =>
      .switch (SEval_mono hF _ _ _ hs) hf hl (SEval_mono hF _ _ _ hb)
  | _, _, _, .call hes hfn hl hb =>
      .call (SEvalList_mono hF _ _ _ hes) (hF _ _ hfn) hl (SEval_mono hF _ _ _ hb)

theorem SEvalList_mono {P Q : SProgram}
    (hF : ∀ (f : String) (fd : SFun), sFn P f = some fd → sFn Q f = some fd) :
    ∀ (σ : SEnv) (es : List SExp) (vs : List Val), SEvalList P σ es vs → SEvalList Q σ es vs
  | _, _, _, .nil        => .nil
  | _, _, _, .cons he ht => .cons (SEval_mono hF _ _ _ he) (SEvalList_mono hF _ _ _ ht)

end

/-- Every derivation over the original program replays over the extended one. -/
theorem lift {σ : SEnv} {e : SExp} {v : Val} (h : SEval hwS σ e v) : SEval hwMemS σ e v :=
  SEval_mono (fun _ _ => sFn_lift) _ _ _ h


/-! ## 4. `memAt` preserves the memory list

Quantified over EVERY encoded image list and EVERY `Int` index -- not over the
worked fixture.  The answer is the exact selected image when the index is
present and the exact absent image otherwise, matching `sourceValue`'s
`s.mems[idx]?.getD (fun _ => mk_bv 0 0)`; `Hardware.absentImg_denote` is the
already-proved bridge for the second case.  Negative indices are included: the
object walks to `nil`, which is the absent image, and the reference's `getD`
likewise never reads below zero. -/

private theorem memAt_index_step (a : MemImg) (rest : List MemImg) {x : Int} (hx : ¬ (x = 0)) :
    (if (0:Int) ≤ x - 1 then rest[(x - 1).toNat]?.getD Hardware.absentImg
     else Hardware.absentImg)
      = (if (0:Int) ≤ x then (a :: rest)[x.toNat]?.getD Hardware.absentImg
         else Hardware.absentImg) := by
  by_cases h0 : (0:Int) ≤ x
  · have h1 : (0:Int) ≤ x - 1 := by omega
    have hn : x.toNat = (x - 1).toNat + 1 := by omega
    rw [if_pos h1, if_pos h0, hn]
    simp
  · have h1 : ¬ ((0:Int) ≤ x - 1) := by omega
    rw [if_neg h1, if_neg h0]

theorem memAt_sound (ms : List MemImg) (x : Int) {σ : SEnv} {eml ei : SExp}
    (hl : SEval hwMemS σ eml (encListG MemImg.encMemImg ms))
    (hi : SEval hwMemS σ ei (.int x)) :
    SEval hwMemS σ (.call "memAt" [eml, ei])
      (MemImg.encMemImg (if (0:Int) ≤ x then ms[x.toNat]?.getD Hardware.absentImg
                         else Hardware.absentImg)) := by
  induction ms generalizing x σ eml ei with
  | nil =>
      refine SEval_call2 hl hi rfl rfl ?_
      refine SEval.iteT (SEval_isNil_nil (.ref rfl)) ?_
      have hv : (MemImg.encMemImg
            (if (0:Int) ≤ x then ([] : List MemImg)[x.toNat]?.getD Hardware.absentImg
             else Hardware.absentImg))
          = Val.ctor memImgTag [.nil, ofBV (mk_bv 0 0)] := by
        simp [MemImg.encMemImg, Hardware.absentImg, encListG, MemImg.tagMemImg, memImgTag]
      rw [hv]
      exact SEval.mk (.cons .lit (.cons (SEval_prim2 .lit .lit rfl) .nil))
  | cons a rest ih =>
      refine SEval_call2 hl hi rfl rfl ?_
      refine SEval.iteF (SEval_isNil_cons (.ref rfl)) ?_
      refine SEval_ite_of_bool (bb := decide (x = 0))
        (SEval_prim2 (.ref rfl) .lit rfl) ?_ ?_
      · intro hT
        have hx : x = 0 := of_decide_eq_true hT
        subst hx
        have hv : (MemImg.encMemImg
              (if (0:Int) ≤ (0:Int) then (a :: rest)[(0:Int).toNat]?.getD Hardware.absentImg
               else Hardware.absentImg))
            = MemImg.encMemImg a := by simp
        rw [hv]
        exact SEval_hd (a := MemImg.encMemImg a) (.ref rfl)
      · intro hF
        have hx : ¬ (x = 0) := of_decide_eq_false hF
        rw [← memAt_index_step a rest hx]
        exact ih (x - 1) (SEval_tl (.ref rfl)) (SEval_prim2 (.ref rfl) .lit rfl)

/-! ## 5. `romWords` preserves ROM contents

One word per content entry, in order, each `mk_bv dw c`.  So the table length is
`contents.size` and NOT `2 ^ aw` -- the reference bound an inferred table
actually has -- and every initial word is carried, not re-derived. -/

theorem romWords_sound (ct : List Int) (dw : Nat) {σ : SEnv} {ec ed : SExp}
    (hc : SEval hwMemS σ ec (encListG encInt ct))
    (hd : SEval hwMemS σ ed (encNat dw)) :
    SEval hwMemS σ (.call "romWords" [ec, ed]) (encListG ofBV (ct.map (mk_bv dw))) := by
  induction ct generalizing σ ec ed with
  | nil =>
      refine SEval_call2 hc hd rfl rfl ?_
      exact SEval.iteT (SEval_isNil_nil (.ref rfl)) .lit
  | cons c cs ih =>
      refine SEval_call2 hc hd rfl rfl ?_
      refine SEval.iteF (SEval_isNil_cons (.ref rfl)) ?_
      refine SEval_consP ?_ ?_
      · exact SEval_prim2 (.ref rfl) (SEval_hd (a := encInt c) (.ref rfl)) rfl
      · exact ih (SEval_tl (.ref rfl)) (.ref rfl)

/-- The ROM image `romWords` builds is exactly `MemImg.ofMemConst`'s word list,
so the already-proved `ofMemConst_denote` is the reference bridge. -/
theorem romWords_ofMemConst (dw : Nat) (contents : Array Int) :
    encListG ofBV (contents.toList.map (mk_bv dw))
      = encListG ofBV (MemImg.ofMemConst dw contents).words := rfl

theorem romWords_length (dw : Nat) (contents : Array Int) :
    (MemImg.ofMemConst dw contents).words.length = contents.size := by
  simp [MemImg.ofMemConst]


/-! ## 6. `srcValM` on the two memory forms

Both answer an ENCODED `MemImg`, and both are related to `Compiler.sourceValue`
through `MemImg.denote` by theorems proved earlier (`srcMemImg_denote`,
`ofMemConst_denote`).  The input and flop arguments are quantified over
ARBITRARY values here: the memory forms must not consult them, and taking them
as arbitrary is what proves they do not. -/

theorem srcValM_memImg (ms : List MemImg) (idx aw dw : Nat) {σ : SEnv}
    {esd einp efq ems : SExp} {vi vf : Val}
    (h1 : SEval hwMemS σ esd (encSource (.memImg idx aw dw)))
    (h2 : SEval hwMemS σ einp vi) (h3 : SEval hwMemS σ efq vf)
    (h4 : SEval hwMemS σ ems (encListG MemImg.encMemImg ms)) :
    SEval hwMemS σ (.call "srcValM" [esd, einp, efq, ems])
      (MemImg.encMemImg (ms[idx]?.getD Hardware.absentImg)) := by
  refine SEval_call4 h1 h2 h3 h4 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  have hv : (if (0:Int) ≤ (Int.ofNat idx)
             then ms[(Int.ofNat idx).toNat]?.getD Hardware.absentImg
             else Hardware.absentImg) = ms[idx]?.getD Hardware.absentImg := by simp
  rw [← hv]
  exact memAt_sound ms (Int.ofNat idx) (.ref rfl) (.ref rfl)

theorem srcValM_memConst (aw dw : Nat) (contents : Array Int) {σ : SEnv}
    {esd einp efq ems : SExp} {vi vf vm : Val}
    (h1 : SEval hwMemS σ esd (encSource (.memConst aw dw contents)))
    (h2 : SEval hwMemS σ einp vi) (h3 : SEval hwMemS σ efq vf)
    (h4 : SEval hwMemS σ ems vm) :
    SEval hwMemS σ (.call "srcValM" [esd, einp, efq, ems])
      (MemImg.encMemImg (MemImg.ofMemConst dw contents)) := by
  refine SEval_call4 h1 h2 h3 h4 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  have hv : MemImg.encMemImg (MemImg.ofMemConst dw contents)
      = Val.ctor memImgTag
          [encListG ofBV (contents.toList.map (mk_bv dw)), ofBV (mk_bv dw 0)] := by
    simp [MemImg.encMemImg, MemImg.ofMemConst, MemImg.tagMemImg, memImgTag]
  rw [hv]
  refine SEval.mk (.cons ?_ (.cons (SEval_prim2 (.ref rfl) .lit rfl) .nil))
  exact romWords_sound contents.toList dw (.ref rfl) (.ref rfl)

/-! ## 7. `srcValM` on the four scalar forms

Delegated to the ORIGINAL `srcVal` and its callees `nthD` and `nz`, replayed in
the extended program by `lift`.  Nothing here re-proves the scalar cases: the
point is that extending the program leaves them literally the same code, and the
asynchronous-reset case with its polarity comes across with the rest. -/

theorem srcValM_scalar {sd : SourceDesc} {i : RuntimeInput} {s : RuntimeState}
    {σ : SEnv} {esd einp efq ems : SExp} {vm : Val}
    (hsup : SourceSupported sd = true)
    (hin : SourceInputBound sd i.size) (hst : SourceFlopBound sd s.flops.size)
    (h1 : SEval hwMemS σ esd (encSource sd))
    (h2 : SEval hwMemS σ einp (encInput i))
    (h3 : SEval hwMemS σ efq (encBVs s.flops))
    (h4 : SEval hwMemS σ ems vm) :
    SEval hwMemS σ (.call "srcValM" [esd, einp, efq, ems])
      (encBV (sourceValue i s sd).asBV) := by
  refine SEval_call4 h1 h2 h3 h4 rfl rfl ?_
  cases sd with
  | input idx w =>
      refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
      exact lift (srcVal_agree hsup hin hst (.ref rfl) (.ref rfl) (.ref rfl))
  | const w v =>
      refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
      exact lift (srcVal_agree hsup hin hst (.ref rfl) (.ref rfl) (.ref rfl))
  | flopQ idx w =>
      refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
      exact lift (srcVal_agree hsup hin hst (.ref rfl) (.ref rfl) (.ref rfl))
  | flopQAsync idx w ri rv al =>
      refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
      exact lift (srcVal_agree hsup hin hst (.ref rfl) (.ref rfl) (.ref rfl))
  | memImg _ _ _   => exact absurd hsup (by simp [SourceSupported])
  | memConst _ _ _ => exact absurd hsup (by simp [SourceSupported])

/-! ## 8. The source layer, all six forms, against `Compiler.sourceValue`

`SrcRel` is the typed slot relation at the SOURCE boundary: a scalar slot holds
the encoded `BV`, a memory slot holds an encoded `MemImg` whose DENOTATION is
the reference's function-valued memory.  `SourceSupported` is deliberately NOT a
premise -- the whole point of the memory route is that the two memory forms are
now answered, so only the ORIGINAL input/flop bounds remain. -/

def SrcRel : Val → CertVal → Prop
  | v, .bv b  => v = encBV b
  | v, .mem f => ∃ m : MemImg, v = MemImg.encMemImg m ∧ MemImg.denote m = f

theorem srcValM_agree {sd : SourceDesc} {i : RuntimeInput} {st : StateM}
    {σ : SEnv} {esd einp efq ems : SExp}
    (hin : SourceInputBound sd i.size) (hst : SourceFlopBound sd st.flops.size)
    (h1 : SEval hwMemS σ esd (encSource sd))
    (h2 : SEval hwMemS σ einp (encInput i))
    (h3 : SEval hwMemS σ efq (encBVs st.flops))
    (h4 : SEval hwMemS σ ems (encListG MemImg.encMemImg st.mems.toList)) :
    ∃ v, SEval hwMemS σ (.call "srcValM" [esd, einp, efq, ems]) v
       ∧ SrcRel v (sourceValue i (StateM.denote st) sd) := by
  cases sd with
  | input idx w =>
      exact ⟨_, srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4, rfl⟩
  | const w v =>
      exact ⟨_, srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4, rfl⟩
  | flopQ idx w =>
      exact ⟨_, srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4, rfl⟩
  | flopQAsync idx w ri rv al =>
      exact ⟨_, srcValM_scalar (s := StateM.denote st) (by simp [SourceSupported])
        hin hst h1 h2 h3 h4, rfl⟩
  | memImg idx aw dw =>
      refine ⟨_, srcValM_memImg st.mems.toList idx aw dw h1 h2 h3 h4, ?_⟩
      refine ⟨st.mems.toList[idx]?.getD Hardware.absentImg, rfl, ?_⟩
      have hx : st.mems.toList[idx]?.getD Hardware.absentImg
          = Hardware.srcMemImg st idx := by
        simp [Hardware.srcMemImg]
      rw [hx]
      simpa [sourceValue, CertVal.asMem] using Hardware.srcMemImg_denote st idx
  | memConst aw dw contents =>
      refine ⟨_, srcValM_memConst aw dw contents h1 h2 h3 h4, ?_⟩
      refine ⟨MemImg.ofMemConst dw contents, rfl, ?_⟩
      funext x
      simpa [sourceValue, CertVal.asMem] using Hardware.ofMemConst_denote dw contents x

/-! ## 9. Crossing to the resolved program

`SEval_sound` is the repository's production bridge, proved without mentioning
`resolve` or `Eval` on its hypothesis side, so this crossing genuinely checks
binder order against the resolver rather than restating it. -/

theorem srcValM_agree_Eval {sd : SourceDesc} {i : RuntimeInput} {st : StateM}
    {esd einp efq ems : SExp} {t : Term}
    (hin : SourceInputBound sd i.size) (hst : SourceFlopBound sd st.flops.size)
    (h1 : SEval hwMemS [] esd (encSource sd))
    (h2 : SEval hwMemS [] einp (encInput i))
    (h3 : SEval hwMemS [] efq (encBVs st.flops))
    (h4 : SEval hwMemS [] ems (encListG MemImg.encMemImg st.mems.toList))
    (hr : resolve (funNames hwMemS) [] (.call "srcValM" [esd, einp, efq, ems]) = .ok t) :
    ∃ v, Eval hwMemP [] t v ∧ SrcRel v (sourceValue i (StateM.denote st) sd) := by
  obtain ⟨v, hv, hrel⟩ := srcValM_agree hin hst h1 h2 h3 h4
  exact ⟨v, by simpa using SEval_sound hwMemP_resolves [] _ v t hv hr, hrel⟩


/-- A call on LITERAL arguments resolves to the same call at the index `idxOf`
reports; no scope is consulted. -/
theorem resolve_call_lits {fns scope : List String} {f : String} {k : Nat}
    (hk : idxOf fns f = some k) (vs : List Val) :
    resolve fns scope (.call f (vs.map SExp.lit)) = .ok (.call k (vs.map Term.lit)) := by
  simp only [resolve, bind, Except.bind, resolveList_lits, hk]

/-- `srcValM` sits where `resolveProgram` put it.  Checked by reduction, not
assumed, and it is the SAME id `HardwareMemChecks` executes. -/
theorem srcValM_id : idOf "srcValM" = some 43 := rfl

/-- **The source layer, on the resolved program, unconditionally.**  No
`resolve` hypothesis is left to the caller: the argument list is literals, so
the resolved term is pinned here. -/
theorem srcValM_agree_resolved (sd : SourceDesc) (i : RuntimeInput) (st : StateM)
    (hin : SourceInputBound sd i.size) (hst : SourceFlopBound sd st.flops.size) :
    ∃ v, Eval hwMemP []
            (.call 43
               ([encSource sd, encInput i, encBVs st.flops,
                 encListG MemImg.encMemImg st.mems.toList].map Term.lit)) v
       ∧ SrcRel v (sourceValue i (StateM.denote st) sd) := by
  obtain ⟨v, hv, hrel⟩ :=
    srcValM_agree (σ := []) (esd := .lit (encSource sd)) (einp := .lit (encInput i))
      (efq := .lit (encBVs st.flops))
      (ems := .lit (encListG MemImg.encMemImg st.mems.toList))
      hin hst .lit .lit .lit .lit
  refine ⟨v, ?_, hrel⟩
  have hr := resolve_call_lits (fns := funNames hwMemS) (scope := []) (f := "srcValM")
    srcValM_id [encSource sd, encInput i, encBVs st.flops,
                encListG MemImg.encMemImg st.mems.toList]
  have := SEval_sound hwMemP_resolves [] _ v _ hv (by simpa using hr)
  simpa using this

/-! ## 10. The premises are satisfiable, and non-vacuous at the real shape

Every hypothesis of `srcValM_agree` is discharged by `SEval.lit` when the
arguments are literals, so none of them is a hidden unsatisfiable side
condition.  The two instances below then pin the statement at a REAL shape --
32-bit ROM words, a 32-word zero image, a 32-bit flop and an asynchronous reset
read from the input vector -- and the matching `#guard`s execute the SAME call
through `hwMemP`, so the derivation and the resolved program agree on the
answer, not merely on the statement being provable. -/

theorem srcValM_agree_lits (sd : SourceDesc) (i : RuntimeInput) (st : StateM)
    (hin : SourceInputBound sd i.size) (hst : SourceFlopBound sd st.flops.size) :
    ∃ v, SEval hwMemS []
            (.call "srcValM"
               [.lit (encSource sd), .lit (encInput i), .lit (encBVs st.flops),
                .lit (encListG MemImg.encMemImg st.mems.toList)]) v
       ∧ SrcRel v (sourceValue i (StateM.denote st) sd) :=
  srcValM_agree hin hst .lit .lit .lit .lit

/-- A Pico-shaped state: a 3-word 32-bit ROM, a 32-word 32-bit image, a 32-bit
flop and a 1-bit reset flop. -/
def picoState : StateM :=
  { flops := #[mk_bv 32 7, mk_bv 1 1]
  , mems  := #[MemImg.ofMemConst 32 #[11, 22, 33], MemImg.zeroImg 5 32] }

def picoInput : RuntimeInput := #[mk_bv 1 1, mk_bv 32 9]

/-- A memory source at the real shape: `SourceSupported` would REFUSE this
descriptor, and the theorem still answers it. -/
example : ∃ v, SEval hwMemS []
      (.call "srcValM"
         [.lit (encSource (.memImg 1 5 32)), .lit (encInput picoInput),
          .lit (encBVs picoState.flops),
          .lit (encListG MemImg.encMemImg picoState.mems.toList)]) v
    ∧ SrcRel v (sourceValue picoInput (StateM.denote picoState) (.memImg 1 5 32)) :=
  srcValM_agree_lits (.memImg 1 5 32) picoInput picoState trivial trivial

/-- An asynchronous-reset scalar source at the real shape, answered by the
ORIGINAL `srcVal` replayed in the extended program. -/
example : ∃ v, SEval hwMemS []
      (.call "srcValM"
         [.lit (encSource (.flopQAsync 0 32 0 0 false)), .lit (encInput picoInput),
          .lit (encBVs picoState.flops),
          .lit (encListG MemImg.encMemImg picoState.mems.toList)]) v
    ∧ SrcRel v (sourceValue picoInput (StateM.denote picoState)
                 (.flopQAsync 0 32 0 0 false)) :=
  srcValM_agree_lits (.flopQAsync 0 32 0 0 false) picoInput picoState
    (by decide) (by decide)

private def srcValMId : Nat := (idOf "srcValM").getD 999

private def runSrcM (sd : SourceDesc) : EvalResult :=
  evalFuel 40000 hwMemP []
    (.call srcValMId
       [ .lit (encSource sd), .lit (encInput picoInput)
       , .lit (encBVs picoState.flops)
       , .lit (encListG MemImg.encMemImg picoState.mems.toList) ])

/- the resolved program answers the memory source with the SELECTED image -/
#guard (match runSrcM (.memImg 1 5 32) with
        | .value v => v == MemImg.encMemImg (MemImg.zeroImg 5 32) | _ => false)

/- …the ROM at index 0 with its initialised words, `contents.size` long -/
#guard (match runSrcM (.memImg 0 5 32) with
        | .value v => v == MemImg.encMemImg (MemImg.ofMemConst 32 #[11, 22, 33])
        | _ => false)

/- …an absent index with the exact absent image -/
#guard (match runSrcM (.memImg 7 5 32) with
        | .value v => v == MemImg.encMemImg Hardware.absentImg | _ => false)

/- …and the asynchronous-reset scalar with the reset value, reset asserted
   active-high by `picoInput[0] = 1` -/
#guard (match runSrcM (.flopQAsync 0 32 0 0 false) with
        | .value v => v == encBV (mk_bv 32 0) | _ => false)

/- while with active-LOW polarity the same input leaves the flop's value -/
#guard (match runSrcM (.flopQAsync 0 32 0 0 true) with
        | .value v => v == encBV (bv_resize 32 (mk_bv 32 7)) | _ => false)

/-! ## 11. Axiom audit

`propext` and `Quot.sound` are Lean's own and arrive through `simp`/`omega` on
`List`, `Int` and `Array`.  What is checked here is that NO new unsafe axiom
appears: no `sorryAx`, no `Lean.ofReduceBool`, no `Lean.trustCompiler`. -/

#audit_axioms hwMemResolved_ok
#audit_axioms hwMemP_resolves
#audit_axioms sFn_of_prefix
#audit_axioms sFn_lift
#audit_axioms SEval_mono
#audit_axioms lift
#audit_axioms memAt_sound
#audit_axioms romWords_sound
#audit_axioms romWords_length
#audit_axioms srcValM_memImg
#audit_axioms srcValM_memConst
#audit_axioms srcValM_scalar
#audit_axioms srcValM_agree
#audit_axioms srcValM_agree_Eval
#audit_axioms srcValM_agree_lits
#audit_axioms srcValM_id
#audit_axioms resolve_call_lits
#audit_axioms srcValM_agree_resolved

end HwMem
end Projection
