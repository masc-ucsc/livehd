/-
  # A semantics for the NAMED surface syntax, and its bridge to `Eval`

  WHY THIS EXISTS.  `I_hw` and `mixProgram` are written in `Surface`'s named
  syntax and then RESOLVED to de Bruijn `Program`s.  Every theorem about what
  one of them computes is a statement about `Eval hwP …`, and `hwP` is
  `resolveProgram hwS` -- a list of 41 functions indexed by position, with every
  variable an integer.  Stating a per-function lemma over that directly needs
  `hwP.fn i = some ⟨n, <de Bruijn body>⟩` written out by hand: unreadable,
  invalidated by inserting a single helper function, and forcing the kernel to
  reduce `resolveProgram hwS` at every `rfl`.

  So the per-function reasoning happens HERE, over the named syntax, and crosses
  to `Eval` exactly once per function via `SEval_sound`.  `SIMULATOR_PLAN.md`'s
  appendix specifies this layer for `mixProgram` (Phase 8); it is the same layer
  `I_hw` adequacy needs, so it is built generically and reused rather than
  written twice.

  INDEPENDENT BY CONSTRUCTION.  `SEval` is defined without mentioning `resolve`
  or `Eval`.  If it were defined through either, `SEval_sound` would be a
  tautology and would check nothing -- in particular it would not catch a binder
  order that disagrees with the resolver, which is the single most likely way
  for this layer to be silently wrong.  The binder conventions are therefore
  restated here and checked against `Surface.resolve` by the bridge:

    * `scope` / environment are INNERMOST-FIRST, matching `Env`.
    * `letN x e b` puts `x` in front for `b` only.
    * `switch` binds a branch's field names IN ORDER, in front of the enclosing
      scope: `bs ++ scope`, mirroring `resolveAlts`.
    * a function body runs in its ARGUMENTS ALONE -- parameter `j` at index `j`,
      nothing captured -- mirroring `resolveProgram.resolveFuns` and
      `Eval.call`.
-/

import LeanSemanticPrimitives.Projection.Surface
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics

namespace Projection
namespace Surface

/-! ## Named environments -/

/-- A surface environment: names paired with values, innermost first. -/
abbrev SEnv := List (String × Val)

/-- Name lookup, mirroring `idxOf`'s first-match rule. -/
def slookup : SEnv → String → Option Val
  | [],             _ => none
  | (y, v) :: rest, x => if y == x then some v else slookup rest x

/-- Function lookup BY NAME, routed through `idxOf` so that the correspondence
with the resolved program's positional `Program.fn` is immediate. -/
def sFn (P : SProgram) (f : String) : Option SFun :=
  (idxOf (funNames P) f).bind (fun i => P.funs[i]?)

/-- Branch selection, mirroring `findAlt`'s first-match rule. -/
def sFindAlt : List SAlt → Nat → Option SAlt
  | [],               _   => none
  | (t, bs, b) :: as, tag => if t = tag then some (t, bs, b) else sFindAlt as tag

/-! ## The semantics -/

mutual

/-- Big-step evaluation of the NAMED syntax.  Mentions neither `resolve` nor
`Eval`. -/
inductive SEval (P : SProgram) : SEnv → SExp → Val → Prop where
  | lit    : SEval P σ (.lit v) v
  | ref    : slookup σ x = some v → SEval P σ (.ref x) v
  | letN   : SEval P σ e d → SEval P ((x, d) :: σ) b v → SEval P σ (.letN x e b) v
  | iteT   : SEval P σ c (.bool true)  → SEval P σ a v → SEval P σ (.ite c a b) v
  | iteF   : SEval P σ c (.bool false) → SEval P σ b v → SEval P σ (.ite c a b) v
  | prim   : SEvalList P σ es vs → evalPrim p vs = .ok v → SEval P σ (.prim p es) v
  | mk     : SEvalList P σ es vs → SEval P σ (.mk k es) (.ctor k vs)
  | switch : SEval P σ s (.ctor tag vs) → sFindAlt alts tag = some (tag, bs, body) →
             bs.length = vs.length →
             SEval P (bs.zip vs ++ σ) body v → SEval P σ (.switch s alts) v
  | call   : SEvalList P σ es vs → sFn P f = some fd → fd.params.length = vs.length →
             SEval P (fd.params.zip vs) fd.body v → SEval P σ (.call f es) v

inductive SEvalList (P : SProgram) : SEnv → List SExp → List Val → Prop where
  | nil  : SEvalList P σ [] []
  | cons : SEval P σ e v → SEvalList P σ es vs → SEvalList P σ (e :: es) (v :: vs)

end

/-! ## Lookup bridges

Three correspondences, one per thing `resolve` turns from a name into a number. -/

/-- A name found in the surface environment is found at the index `idxOf`
reports, holding the same value. -/
theorem idxOf_slookup : ∀ {σ : SEnv} {x : String} {i : Nat} {v : Val},
    idxOf (σ.map Prod.fst) x = some i → slookup σ x = some v →
    (σ.map Prod.snd)[i]? = some v
  | [],             _, _, _, hi, _  => by simp [idxOf] at hi
  | (y, w) :: rest, x, i, v, hi, hv => by
      by_cases h : y == x
      · simp [idxOf, h] at hi
        simp [slookup, h] at hv
        subst hi; subst hv; rfl
      · simp [idxOf, h] at hi
        simp [slookup, h] at hv
        obtain ⟨j, hj, rfl⟩ := hi
        simpa using idxOf_slookup hj hv

/-- `zip` against a same-length list recovers each side. -/
theorem zip_map_fst : ∀ {bs : List String} {vs : List Val}, bs.length = vs.length →
    (bs.zip vs).map Prod.fst = bs
  | [],      [],      _ => rfl
  | _ :: bs, _ :: vs, h => by
      simp only [List.zip_cons_cons, List.map_cons, List.cons.injEq, true_and]
      exact zip_map_fst (by simpa using h)
  | [],      _ :: _,  h => by simp at h
  | _ :: _,  [],      h => by simp at h

theorem zip_map_snd : ∀ {bs : List String} {vs : List Val}, bs.length = vs.length →
    (bs.zip vs).map Prod.snd = vs
  | [],      [],      _ => rfl
  | _ :: bs, _ :: vs, h => by
      simp only [List.zip_cons_cons, List.map_cons, List.cons.injEq, true_and]
      exact zip_map_snd (by simpa using h)
  | [],      _ :: _,  h => by simp at h
  | _ :: _,  [],      h => by simp at h

/-- Resolution preserves the function table POSITIONALLY, so a surface function
found by name resolves to the entry at the same index, with its body resolved in
its own parameters. -/
theorem resolveFuns_get {fns : List String} :
    ∀ {fs : List SFun} {fs' : List FunDef},
      resolveProgram.resolveFuns fns fs = .ok fs' →
      ∀ {i : Nat} {fd : SFun}, fs[i]? = some fd →
        ∃ b', resolve fns fd.params fd.body = .ok b' ∧
              fs'[i]? = some ⟨fd.params.length, b'⟩
  | [],      _,  h, i, _,  hi => by simp at hi
  | f :: fs, fs', h, i, fd, hi => by
      simp only [resolveProgram.resolveFuns, bind, Except.bind] at h
      split at h <;> try simp at h
      rename_i b' hb
      split at h <;> try simp at h
      rename_i rest hrest
      cases h
      cases i with
      | zero => cases hi; exact ⟨b', hb, rfl⟩
      | succ j =>
          simp only [List.getElem?_cons_succ] at hi ⊢
          exact resolveFuns_get hrest hi

/-- Resolution preserves branch ORDER and TAGS, so the branch `sFindAlt`
selects is the one `findAlt` selects, with the same arity and a resolved body
under `bs ++ scope`. -/
theorem resolveAlts_findAlt {fns scope : List String} :
    ∀ {alts : List SAlt} {alts' : List Alt},
      resolveAlts fns scope alts = .ok alts' →
      ∀ {tag : Nat} {bs : List String} {body : SExp},
        sFindAlt alts tag = some (tag, bs, body) →
        ∃ b', resolve fns (bs ++ scope) body = .ok b' ∧
              findAlt alts' tag = some (tag, bs.length, b')
  | [],                _,     h, _,   _,  _,    hf => by simp [sFindAlt] at hf
  | (t, bs0, b0) :: as, alts', h, tag, bs, body, hf => by
      simp only [resolveAlts, bind, Except.bind] at h
      split at h <;> try simp at h
      rename_i b0' hb0
      split at h <;> try simp at h
      rename_i as' has
      cases h
      by_cases ht : t = tag
      · subst ht
        simp only [sFindAlt] at hf
        cases hf
        exact ⟨b0', hb0, by simp [findAlt, Alt.tag]⟩
      · simp only [sFindAlt, if_neg ht] at hf
        obtain ⟨b', hb', hfind⟩ := resolveAlts_findAlt has hf
        exact ⟨b', hb', by simp [findAlt, Alt.tag, ht, hfind]⟩

/-- The resolved program's function table is exactly the resolved surface one.
Extracted so the bridge can use it WITHOUT destructing its `resolveProgram`
hypothesis, which it still needs for the recursive appeals. -/
theorem resolveProgram_funs {P : SProgram} {Pr : Program} {inl : List Bool}
    (h : resolveProgram P = .ok (Pr, inl)) :
    resolveProgram.resolveFuns (funNames P) P.funs = .ok Pr.funs := by
  cases hfuns : resolveProgram.resolveFuns (funNames P) P.funs with
  | error m => simp [resolveProgram, bind, Except.bind, hfuns] at h
  | ok funs =>
    cases he : idxOf (funNames P) P.entry with
    | none => simp [resolveProgram, bind, Except.bind, hfuns, he] at h
    | some e0 =>
      simp [resolveProgram, bind, Except.bind, hfuns, he] at h
      obtain ⟨h1, -⟩ := h
      subst h1
      rfl

/-- The entry INDEX is the position of the entry name, so a named entry call
crosses to the resolved program's own `entry` without anyone computing it. -/
theorem resolveProgram_entry {P : SProgram} {Pr : Program} {inl : List Bool}
    (h : resolveProgram P = .ok (Pr, inl)) :
    idxOf (funNames P) P.entry = some Pr.entry := by
  cases hfuns : resolveProgram.resolveFuns (funNames P) P.funs with
  | error m => simp [resolveProgram, bind, Except.bind, hfuns] at h
  | ok funs =>
    cases he : idxOf (funNames P) P.entry with
    | none => simp [resolveProgram, bind, Except.bind, hfuns, he] at h
    | some e0 =>
      simp [resolveProgram, bind, Except.bind, hfuns, he] at h
      obtain ⟨h1, -⟩ := h
      subst h1
      rfl

/-- A list of literals resolves to itself; no scope is consulted. -/
theorem resolveList_lits {fns scope : List String} :
    ∀ (vs : List Val), resolveList fns scope (vs.map SExp.lit) = .ok (vs.map Term.lit)
  | []      => rfl
  | v :: vs => by
      simp only [List.map_cons, resolveList, resolve, bind, Except.bind,
                 resolveList_lits vs]

theorem SEvalList_lits {P : SProgram} {σ : SEnv} :
    ∀ (vs : List Val), SEvalList P σ (vs.map SExp.lit) vs
  | []      => .nil
  | _ :: vs => .cons .lit (SEvalList_lits vs)

/-! ## The bridge

The ONLY place the named world meets the resolved one.  Everything a program
proves about itself is proved over `SEval` and crossed here once. -/

mutual

/-- Everything proved over the named syntax crosses to `Eval` here, once.

Written by RECURSION ON THE DERIVATION rather than with `cases`, so the
recursive appeals are structural: each sub-derivation is a strict subterm of the
one being analysed. -/
theorem SEval_sound {P : SProgram} {Pr : Program} {inl : List Bool}
    (hres : resolveProgram P = .ok (Pr, inl)) :
    ∀ (σ : SEnv) (e : SExp) (v : Val) (t : Term),
      SEval P σ e v →
      resolve (funNames P) (σ.map Prod.fst) e = .ok t →
      Eval Pr (σ.map Prod.snd) t v
  | _, _, _, _, .lit, hr => by
      simp only [resolve] at hr; cases hr; exact .lit
  | σ, _, v, t, .ref hx, hr => by
      simp only [resolve] at hr
      split at hr <;> try simp at hr
      rename_i i hi
      cases hr
      exact .var (idxOf_slookup hi hx)
  | σ, _, v, t, .letN he hb, hr => by
      simp only [resolve, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i e' he'
      split at hr <;> try simp at hr
      rename_i b' hb'
      cases hr
      exact .letIn (SEval_sound hres _ _ _ _ he he')
                   (SEval_sound hres _ _ _ _ hb hb')
  | σ, _, v, t, .iteT hc ha, hr => by
      simp only [resolve, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i c' hc'
      split at hr <;> try simp at hr
      rename_i a' ha'
      split at hr <;> try simp at hr
      rename_i b' hb'
      cases hr
      exact .iteT (SEval_sound hres _ _ _ _ hc hc')
                  (SEval_sound hres _ _ _ _ ha ha')
  | σ, _, v, t, .iteF hc hb, hr => by
      simp only [resolve, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i c' hc'
      split at hr <;> try simp at hr
      rename_i a' ha'
      split at hr <;> try simp at hr
      rename_i b' hb'
      cases hr
      exact .iteF (SEval_sound hres _ _ _ _ hc hc')
                  (SEval_sound hres _ _ _ _ hb hb')
  | σ, _, v, t, .prim hes hp, hr => by
      simp only [resolve, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i es' hes'
      split at hr <;> try simp at hr
      cases hr
      exact .prim (SEvalList_sound hres _ _ _ _ hes hes') hp
  | σ, _, _, t, SEval.mk hes, hr => by
      simp only [resolve, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i es' hes'
      cases hr
      exact .ctorT (SEvalList_sound hres _ _ _ _ hes hes')
  | σ, _, v, t, .switch hsc hfind hlen hbody, hr => by
      simp only [resolve, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i s' hs'
      split at hr <;> try simp at hr
      rename_i alts' halts
      cases hr
      obtain ⟨b', hb', hfind'⟩ := resolveAlts_findAlt halts hfind
      refine .caseT (SEval_sound hres _ _ _ _ hsc hs') hfind'
        (by simp [Alt.arity, hlen]) ?_
      have := SEval_sound hres _ _ _ _ hbody
        (by simpa [List.map_append, zip_map_fst hlen] using hb')
      simpa [Alt.body, List.map_append, zip_map_snd hlen] using this
  | σ, _, v, t, .call hes hfn hlen hbody, hr => by
      simp only [resolve, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i es' hes'
      split at hr <;> try simp at hr
      rename_i i hi
      cases hr
      simp only [sFn, hi, Option.bind_some] at hfn
      obtain ⟨b', hb', hget⟩ := resolveFuns_get (resolveProgram_funs hres) hfn
      refine .call (SEvalList_sound hres _ _ _ _ hes hes') hget
        (by simpa using hlen) ?_
      have := SEval_sound hres _ _ _ _ hbody (by simpa [zip_map_fst hlen] using hb')
      simpa [zip_map_snd hlen] using this

theorem SEvalList_sound {P : SProgram} {Pr : Program} {inl : List Bool}
    (hres : resolveProgram P = .ok (Pr, inl)) :
    ∀ (σ : SEnv) (es : List SExp) (vs : List Val) (ts : List Term),
      SEvalList P σ es vs →
      resolveList (funNames P) (σ.map Prod.fst) es = .ok ts →
      EvalList Pr (σ.map Prod.snd) ts vs
  | _, _, _, _, .nil, hr => by
      simp only [resolveList] at hr; cases hr; exact .nil
  | σ, _, _, t, .cons he hes, hr => by
      simp only [resolveList, bind, Except.bind] at hr
      split at hr <;> try simp at hr
      rename_i e' he'
      split at hr <;> try simp at hr
      rename_i es' hes'
      cases hr
      exact .cons (SEval_sound hres _ _ _ _ he he')
                  (SEvalList_sound hres _ _ _ _ hes hes')

end

/-! ## `Eval` is deterministic

Needed for the FORWARD direction of an adequacy `iff`.  `SEval_sound` only
produces SOME evaluation reaching the expected value; to conclude that an
arbitrary `Eval … r` has `r` equal to it, the relation must be single-valued.

The only case with content is `ite`: the two derivations may take different
branches, and what rules that out is the induction hypothesis on the CONDITION,
which cannot be both `true` and `false`. -/

mutual

theorem Eval_det {P : Program} :
    ∀ {ρ : Env} {t : Term} {v₁ v₂ : Val}, Eval P ρ t v₁ → Eval P ρ t v₂ → v₁ = v₂
  | _, _, _, _, .lit, h2 => by cases h2; rfl
  | _, _, _, _, .var h1, h2 => by
      cases h2 with
      | var h2' => rw [h1] at h2'; exact Option.some.inj h2'
  | _, _, _, _, .letIn he hb, h2 => by
      cases h2 with
      | letIn he' hb' =>
          cases Eval_det he he'
          exact Eval_det hb hb'
  | _, _, _, _, .iteT hc ha, h2 => by
      cases h2 with
      | iteT hc' ha' => exact Eval_det ha ha'
      | iteF hc' hb' => exact absurd (Eval_det hc hc') (by simp)
  | _, _, _, _, .iteF hc hb, h2 => by
      cases h2 with
      | iteT hc' ha' => exact absurd (Eval_det hc hc') (by simp)
      | iteF hc' hb' => exact Eval_det hb hb'
  | _, _, _, _, .prim hts hp, h2 => by
      cases h2 with
      | prim hts' hp' =>
          cases EvalList_det hts hts'
          rw [hp] at hp'
          exact Except.ok.inj hp'
  | _, _, _, _, .ctorT hts, h2 => by
      cases h2 with
      | ctorT hts' => cases EvalList_det hts hts'; rfl
  | _, _, _, _, .caseT hs hf har hb, h2 => by
      cases h2 with
      | caseT hs' hf' har' hb' =>
          have hc := Eval_det hs hs'
          cases hc
          rw [hf] at hf'
          cases Option.some.inj hf'
          exact Eval_det hb hb'
  | _, _, _, _, .call hts hfn har hb, h2 => by
      cases h2 with
      | call hts' hfn' har' hb' =>
          cases EvalList_det hts hts'
          rw [hfn] at hfn'
          cases Option.some.inj hfn'
          exact Eval_det hb hb'

theorem EvalList_det {P : Program} :
    ∀ {ρ : Env} {ts : List Term} {vs₁ vs₂ : List Val},
      EvalList P ρ ts vs₁ → EvalList P ρ ts vs₂ → vs₁ = vs₂
  | _, _, _, _, .nil, h2 => by cases h2; rfl
  | _, _, _, _, .cons ht hts, h2 => by
      cases h2 with
      | cons ht' hts' =>
          cases Eval_det ht ht'
          cases EvalList_det hts hts'
          rfl

end

/-! ## Micro-kit

Thin introduction rules for the constructs `hwS` is written in, so a per-
function proof reads like the surface text instead of like a nest of
`SEvalList` conses.  Deliberately only what is needed now; Phase 8 extends this
list rather than duplicating it. -/

theorem SEval_ref' {P : SProgram} {σ : SEnv} {x : String} {v : Val}
    (h : slookup σ x = some v) : SEval P σ (.ref x) v := .ref h

theorem SEval_prim1 {P : SProgram} {σ : SEnv} {p : Prim} {e : SExp} {a v : Val}
    (he : SEval P σ e a) (h : evalPrim p [a] = .ok v) :
    SEval P σ (.prim p [e]) v := .prim (.cons he .nil) h

theorem SEval_prim2 {P : SProgram} {σ : SEnv} {p : Prim} {e₁ e₂ : SExp} {a b v : Val}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h : evalPrim p [a, b] = .ok v) :
    SEval P σ (.prim p [e₁, e₂]) v := .prim (.cons h₁ (.cons h₂ .nil)) h

theorem SEval_prim3 {P : SProgram} {σ : SEnv} {p : Prim} {e₁ e₂ e₃ : SExp}
    {a b c v : Val}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h₃ : SEval P σ e₃ c)
    (h : evalPrim p [a, b, c] = .ok v) :
    SEval P σ (.prim p [e₁, e₂, e₃]) v :=
  .prim (.cons h₁ (.cons h₂ (.cons h₃ .nil))) h

theorem SEval_hd {P : SProgram} {σ : SEnv} {e : SExp} {a b : Val}
    (he : SEval P σ e (.cons a b)) : SEval P σ (.prim .hd [e]) a :=
  SEval_prim1 he rfl

theorem SEval_tl {P : SProgram} {σ : SEnv} {e : SExp} {a b : Val}
    (he : SEval P σ e (.cons a b)) : SEval P σ (.prim .tl [e]) b :=
  SEval_prim1 he rfl

theorem SEval_consP {P : SProgram} {σ : SEnv} {e₁ e₂ : SExp} {a b : Val}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) :
    SEval P σ (.prim .consP [e₁, e₂]) (.cons a b) := SEval_prim2 h₁ h₂ rfl

theorem SEval_isNil_nil {P : SProgram} {σ : SEnv} {e : SExp}
    (he : SEval P σ e .nil) : SEval P σ (.prim .isNil [e]) (.bool true) :=
  SEval_prim1 he rfl

theorem SEval_isNil_cons {P : SProgram} {σ : SEnv} {e : SExp} {a b : Val}
    (he : SEval P σ e (.cons a b)) : SEval P σ (.prim .isNil [e]) (.bool false) :=
  SEval_prim1 he rfl

/-- `ite` on a `Bool`-valued condition, without case-splitting at every site. -/
theorem SEval_ite_of_bool {P : SProgram} {σ : SEnv} {c a b : SExp} {bb : Bool} {v : Val}
    (hc : SEval P σ c (.bool bb))
    (ha : bb = true → SEval P σ a v) (hb : bb = false → SEval P σ b v) :
    SEval P σ (.ite c a b) v := by
  cases bb
  · exact .iteF hc (hb rfl)
  · exact .iteT hc (ha rfl)

/-- Named entry point for `switch`; the alternative is located by tag. -/
theorem SEval_switch_of_tag {P : SProgram} {σ : SEnv} {s : SExp} {alts : List SAlt}
    {tag : Nat} {vs : List Val} {bs : List String} {body : SExp} {v : Val}
    (hs : SEval P σ s (.ctor tag vs)) (hf : sFindAlt alts tag = some (tag, bs, body))
    (hlen : bs.length = vs.length)
    (hb : SEval P (bs.zip vs ++ σ) body v) : SEval P σ (.switch s alts) v :=
  .switch hs hf hlen hb

theorem SEval_call1 {P : SProgram} {σ : SEnv} {f : String} {e : SExp} {a v : Val}
    {fd : SFun} (he : SEval P σ e a) (hf : sFn P f = some fd)
    (hp : fd.params.length = 1)
    (hb : SEval P (fd.params.zip [a]) fd.body v) : SEval P σ (.call f [e]) v :=
  .call (.cons he .nil) hf (by simpa using hp) hb

theorem SEval_call2 {P : SProgram} {σ : SEnv} {f : String} {e₁ e₂ : SExp}
    {a b v : Val} {fd : SFun}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (hf : sFn P f = some fd)
    (hp : fd.params.length = 2)
    (hb : SEval P (fd.params.zip [a, b]) fd.body v) : SEval P σ (.call f [e₁, e₂]) v :=
  .call (.cons h₁ (.cons h₂ .nil)) hf (by simpa using hp) hb

theorem SEval_call3 {P : SProgram} {σ : SEnv} {f : String} {e₁ e₂ e₃ : SExp}
    {a b c v : Val} {fd : SFun}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h₃ : SEval P σ e₃ c)
    (hf : sFn P f = some fd) (hp : fd.params.length = 3)
    (hb : SEval P (fd.params.zip [a, b, c]) fd.body v) :
    SEval P σ (.call f [e₁, e₂, e₃]) v :=
  .call (.cons h₁ (.cons h₂ (.cons h₃ .nil))) hf (by simpa using hp) hb

theorem SEval_call4 {P : SProgram} {σ : SEnv} {f : String} {e₁ e₂ e₃ e₄ : SExp}
    {a b c d v : Val} {fd : SFun}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h₃ : SEval P σ e₃ c)
    (h₄ : SEval P σ e₄ d)
    (hf : sFn P f = some fd) (hp : fd.params.length = 4)
    (hb : SEval P (fd.params.zip [a, b, c, d]) fd.body v) :
    SEval P σ (.call f [e₁, e₂, e₃, e₄]) v :=
  .call (.cons h₁ (.cons h₂ (.cons h₃ (.cons h₄ .nil)))) hf (by simpa using hp) hb

theorem SEval_call5 {P : SProgram} {σ : SEnv} {f : String} {e₁ e₂ e₃ e₄ e₅ : SExp}
    {a b c d g v : Val} {fd : SFun}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h₃ : SEval P σ e₃ c)
    (h₄ : SEval P σ e₄ d) (h₅ : SEval P σ e₅ g)
    (hf : sFn P f = some fd) (hp : fd.params.length = 5)
    (hb : SEval P (fd.params.zip [a, b, c, d, g]) fd.body v) :
    SEval P σ (.call f [e₁, e₂, e₃, e₄, e₅]) v :=
  .call (.cons h₁ (.cons h₂ (.cons h₃ (.cons h₄ (.cons h₅ .nil))))) hf (by simpa using hp) hb

theorem SEval_call6 {P : SProgram} {σ : SEnv} {f : String} {e₁ e₂ e₃ e₄ e₅ e₆ : SExp}
    {a b c d g k v : Val} {fd : SFun}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h₃ : SEval P σ e₃ c)
    (h₄ : SEval P σ e₄ d) (h₅ : SEval P σ e₅ g) (h₆ : SEval P σ e₆ k)
    (hf : sFn P f = some fd) (hp : fd.params.length = 6)
    (hb : SEval P (fd.params.zip [a, b, c, d, g, k]) fd.body v) :
    SEval P σ (.call f [e₁, e₂, e₃, e₄, e₅, e₆]) v :=
  .call (.cons h₁ (.cons h₂ (.cons h₃ (.cons h₄ (.cons h₅ (.cons h₆ .nil)))))) hf
    (by simpa using hp) hb

theorem SEval_call7 {P : SProgram} {σ : SEnv} {f : String} {e₁ e₂ e₃ e₄ e₅ e₆ e₇ : SExp}
    {a b c d g k m v : Val} {fd : SFun}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h₃ : SEval P σ e₃ c)
    (h₄ : SEval P σ e₄ d) (h₅ : SEval P σ e₅ g) (h₆ : SEval P σ e₆ k)
    (h₇ : SEval P σ e₇ m)
    (hf : sFn P f = some fd) (hp : fd.params.length = 7)
    (hb : SEval P (fd.params.zip [a, b, c, d, g, k, m]) fd.body v) :
    SEval P σ (.call f [e₁, e₂, e₃, e₄, e₅, e₆, e₇]) v :=
  .call (.cons h₁ (.cons h₂ (.cons h₃ (.cons h₄ (.cons h₅ (.cons h₆ (.cons h₇ .nil))))))) hf
    (by simpa using hp) hb

theorem SEval_call8 {P : SProgram} {σ : SEnv} {f : String}
    {e₁ e₂ e₃ e₄ e₅ e₆ e₇ e₈ : SExp} {a b c d g k m q v : Val} {fd : SFun}
    (h₁ : SEval P σ e₁ a) (h₂ : SEval P σ e₂ b) (h₃ : SEval P σ e₃ c)
    (h₄ : SEval P σ e₄ d) (h₅ : SEval P σ e₅ g) (h₆ : SEval P σ e₆ k)
    (h₇ : SEval P σ e₇ m) (h₈ : SEval P σ e₈ q)
    (hf : sFn P f = some fd) (hp : fd.params.length = 8)
    (hb : SEval P (fd.params.zip [a, b, c, d, g, k, m, q]) fd.body v) :
    SEval P σ (.call f [e₁, e₂, e₃, e₄, e₅, e₆, e₇, e₈]) v :=
  .call
    (.cons h₁ (.cons h₂ (.cons h₃ (.cons h₄ (.cons h₅ (.cons h₆ (.cons h₇ (.cons h₈ .nil))))))))
    hf (by simpa using hp) hb

/-- The shape every whole-program adequacy proof needs: a named entry call on
literal arguments, crossed once.  `Pr.entry` is never computed. -/
theorem SEval_entry {P : SProgram} {Pr : Program} {inl : List Bool}
    (hres : resolveProgram P = .ok (Pr, inl)) {vs : List Val} {v : Val}
    (h : SEval P [] (.call P.entry (vs.map SExp.lit)) v) :
    Eval Pr [] (.call Pr.entry (vs.map Term.lit)) v := by
  have hr : resolve (funNames P) [] (.call P.entry (vs.map SExp.lit))
      = .ok (.call Pr.entry (vs.map Term.lit)) := by
    simp only [resolve, bind, Except.bind, resolveList_lits,
               resolveProgram_entry hres]
  have hE := SEval_sound hres [] _ v _ h hr
  simpa using hE

end Surface

/-! ## Calling an entry point

`Eval` evaluates a function BODY in an environment; the object programs are
driven by a `call` to the entry.  These are the same thing, and the projection
statements are more readable in the second form. -/

theorem EvalList_lits {P : Program} : ∀ (args : List Val),
    EvalList P [] (args.map Term.lit) args
  | []      => .nil
  | a :: as => .cons .lit (EvalList_lits as)

/-- An argument list of literals evaluates to itself. -/
theorem EvalList_lits_inj {P : Program} : ∀ (args vs : List Val),
    EvalList P [] (args.map Term.lit) vs → vs = args
  | [],      _, h => by cases h; rfl
  | a :: as, _, h => by
      cases h with
      | cons ha ht => cases ha; rw [EvalList_lits_inj as _ ht]

theorem Eval_entry {P : Program} {fd : FunDef} {args : List Val} {v : Val}
    (hf : P.fn P.entry = some fd) (har : fd.arity = args.length) :
    Eval P args fd.body v ↔ Eval P [] (.call P.entry (args.map Term.lit)) v := by
  constructor
  · intro h; exact .call (EvalList_lits args) hf har h
  · intro h
    cases h with
    | call hargs hfd harr hbody =>
        rename_i vs fdx
        rw [hf] at hfd
        cases hfd
        -- the argument list is literals, so it evaluates to itself
        have : vs = args := EvalList_lits_inj args vs hargs
        subst this
        exact hbody

end Projection
