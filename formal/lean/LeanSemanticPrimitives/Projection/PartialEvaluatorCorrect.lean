/-
  `mix_sound`: the residual program computes what the source computes.

  ############################################################################
  STATUS.  BOTH DIRECTIONS ARE PROVED.  `mixDriver_correct` says the residual
  entry and the source program compute the same thing:

    forward  (`mixDriver_sound`)     whatever the source computes, the residual
                                     computes -- indexed by SOURCE fuel
    converse (`mixDriver_complete`)  and nothing else -- indexed by RESIDUAL fuel

  ONE PIECE OF POLISH REMAINS.  The hypothesis side is stated with `evalFuel`
  rather than `Eval`, because that is where each half is indexed.  Turning it
  into the plan's literal iff between two `Eval`s needs
  `Eval P p t v → ∃ n, evalFuel n P p t = .value v`, the converse of
  `evalFuel_sound`, which is not proved here.

  AND THE PROJECTIONS THEMSELVES ARE STILL ONLY CHECKED.  That `mixProgram`
  computes what this specializer computes, and that the derived compiler's
  output matches, are separate theorems (`mixProgram_implements_mixHost`,
  `secondProjection_correct`).  Neither is written; Gate0's `#guard`s are the
  only evidence for them.
  ############################################################################

  THE STATEMENT IS ABOUT `erase A`, NOT ABOUT `A`.  Specialization is only
  meaningful relative to the program being specialized, and the annotated
  program is not that program -- it is a claim about it.  `bta_erases` is what
  ties the two together, and without it this theorem would be about nothing.

  THE CENTRAL DEFINITION IS `Compat`, below.  `mix` runs a term against a
  PARTIAL environment: some variables it knows the value of, the rest it knows
  only a residual index for.  `Compat ρr Δ env ρs` says that partial
  environment is an honest description of a real source environment `ρs`, given
  that the residual program will run in `ρr`.  Every case of the proof is then
  "the specializer extended the partial environment; show the extension is still
  honest".

  PROOFS GO BY INDUCTION ON THE EVALUATION DERIVATION, not on the term and not
  on `mix`'s recursion.  The residual program's functions are mutually recursive
  -- a specialized function's correctness depends on the correctness of the
  functions it calls, which may include itself -- so no structural induction
  closes the loop.  An `Eval` derivation does: a call's derivation strictly
  contains its callee's.
-/

import LeanSemanticPrimitives.Projection.PartialEvaluator
import LeanSemanticPrimitives.Projection.BTA

namespace Projection

/-! ## Erasure commutes with the things `Eval` looks up -/

theorem eraseFunDefs_get : ∀ (fds : List AFunDef) (i : Nat),
    (eraseFunDefs fds)[i]? = (fds[i]?).map eraseFunDef
  | [],        _     => by simp [eraseFunDefs]
  | fd :: fds, 0     => by simp [eraseFunDefs]
  | fd :: fds, n + 1 => by simp [eraseFunDefs, eraseFunDefs_get fds n]

theorem eraseProgram_fn {A : AProgram} {f : Nat} {fd : AFunDef} (h : A.fn f = some fd) :
    (eraseProgram A).fn f = some (eraseFunDef fd) := by
  simp only [Program.fn, eraseProgram, eraseFunDefs_get]
  simp only [AProgram.fn] at h
  rw [h]; rfl

theorem findAlt_eraseAlts : ∀ (as : List AAlt) (tag : Nat) (a : AAlt),
    findAAlt as tag = some a →
    findAlt (eraseAlts as) tag = some (a.tag, a.arity, erase a.body)
  | [],       _,   _, h => by simp [findAAlt] at h
  | b :: as, tag, a, h => by
      simp only [findAAlt] at h
      -- normalise the GOAL's `Alt.tag` projection first; unfolding `AAlt.tag`
      -- in the hypothesis instead leaves the two sides syntactically apart
      simp only [eraseAlts, findAlt, Alt.tag]
      split at h
      · rename_i htag; cases h; rw [if_pos htag]
      · rename_i htag; rw [if_neg htag]; exact findAlt_eraseAlts as tag a h

/-! ## Shifting the partial environment -/

@[simp] theorem PEnv.shiftBy_zero : ∀ env : PEnv, PEnv.shiftBy 0 env = env
  | []              => rfl
  | .stat _ :: rest => by simp [PEnv.shiftBy, PEnv.shiftBy_zero rest]
  | .dyn _ :: rest  => by simp [PEnv.shiftBy, PEnv.shiftBy_zero rest]

theorem PEnv.shiftBy_succ : ∀ (n : Nat) (env : PEnv),
    PEnv.shiftBy 1 (PEnv.shiftBy n env) = PEnv.shiftBy (n + 1) env
  | _, []              => rfl
  | n, .stat _ :: rest => by simp [PEnv.shiftBy, PEnv.shiftBy_succ n rest]
  | n, .dyn k :: rest  => by
      simp only [PEnv.shiftBy, PEnv.shiftBy_succ n rest]
      congr 1

theorem PEnv.shiftBy_append : ∀ (k : Nat) (a b : PEnv),
    PEnv.shiftBy k (a ++ b) = PEnv.shiftBy k a ++ PEnv.shiftBy k b
  | _, [],              _ => rfl
  | k, .stat _ :: rest, b => by simp [PEnv.shiftBy, PEnv.shiftBy_append k rest b]
  | k, .dyn _ :: rest,  b => by simp [PEnv.shiftBy, PEnv.shiftBy_append k rest b]

theorem PEnv.shiftBy_map_dyn : ∀ (l : List Nat) (k : Nat),
    PEnv.shiftBy k (l.map PVal.dyn) = l.map (fun i => PVal.dyn (i + k))
  | [],      _ => rfl
  | i :: is, k => by simp [PEnv.shiftBy, PEnv.shiftBy_map_dyn is k]

theorem freshDyns_succ (n : Nat) :
    freshDyns (n + 1) = .dyn 0 :: PEnv.shiftBy 1 (freshDyns n) := by
  simp [freshDyns, List.range_succ_eq_map, PEnv.shiftBy_map_dyn, Function.comp_def]

/-! ## Compatibility

`Compat ρr Δ env ρs`: `mix`'s partial environment `env`, described by the
division `Δ`, is an honest view of the source environment `ρs`, where residual
indices are resolved in `ρr`.

`ρr` is a parameter rather than an index because it is fixed along the list --
it changes only when a residual binder is entered, and that is exactly where
`Compat_shift` applies. -/

inductive Compat (ρr : Env) : Div → PEnv → Env → Prop where
  | nil  : Compat ρr [] [] []
  | stat : Compat ρr Δ env ρs → Compat ρr (.stat :: Δ) (.stat v :: env) (v :: ρs)
  | dyn  : ρr[k]? = some v → Compat ρr Δ env ρs →
           Compat ρr (.dyn :: Δ) (.dyn k :: env) (v :: ρs)

/-- Entering ONE residual binder.  Every residual index in scope moves out by
one, which is exactly what `PEnv.shiftBy 1` does to the partial environment --
so the two shifts cancel and the description stays honest. -/
theorem Compat_shift1 {ρr Δ env ρs} (w : Val) (h : Compat ρr Δ env ρs) :
    Compat (w :: ρr) Δ (env.shiftBy 1) ρs := by
  induction h with
  | nil => exact .nil
  | stat _ ih => exact .stat ih
  | dyn hk _ ih =>
      refine .dyn ?_ ih
      simpa using hk

/-- Entering `n` residual binders at once, as a `caseT` alternative does. -/
theorem Compat_shift {ρr Δ env ρs} : ∀ (ws : List Val),
    Compat ρr Δ env ρs → Compat (ws ++ ρr) Δ (env.shiftBy ws.length) ρs
  | [],      h => by simpa using h
  | w :: ws, h => by
      have h1 := Compat_shift1 w (Compat_shift ws h)
      rw [PEnv.shiftBy_succ] at h1
      simpa using h1

/-- A `caseT` alternative with a DYNAMIC scrutinee: the residual `caseT` binds
the same field values, so both environments grow by `vs` and the partial
environment gains one fresh residual index per field. -/
theorem Compat_fields_dyn {ρr Δ env ρs} : ∀ (vs : List Val), Compat ρr Δ env ρs →
    Compat (vs ++ ρr) (List.replicate vs.length .dyn ++ Δ)
           (freshDyns vs.length ++ env.shiftBy vs.length) (vs ++ ρs)
  | [],      h => by simpa [freshDyns] using h
  | v :: vs, h => by
      have h1 := Compat.dyn (ρr := v :: (vs ++ ρr)) (k := 0) (v := v)
        (by simp) (Compat_shift1 v (Compat_fields_dyn vs h))
      rw [PEnv.shiftBy_append, PEnv.shiftBy_succ] at h1
      simpa [freshDyns_succ, List.replicate_succ] using h1

/-- A `caseT` alternative with a STATIC scrutinee: `mix` knows every field, so
no residual binder is created and `ρr` does not move. -/
theorem Compat_fields_stat {ρr Δ env ρs} : ∀ (vs : List Val), Compat ρr Δ env ρs →
    Compat ρr (List.replicate vs.length .stat ++ Δ) (vs.map PVal.stat ++ env) (vs ++ ρs)
  | [],      h => h
  | v :: vs, h => by
      simpa [List.replicate_succ] using Compat.stat (Compat_fields_stat vs h)

/-! ### Reading a compatible environment -/

theorem Compat_stat_lookup {ρr Δ env ρs} (h : Compat ρr Δ env ρs) :
    ∀ (i : Nat) (v : Val), env[i]? = some (PVal.stat v) → ρs[i]? = some v := by
  induction h with
  | nil => intro i v hv; simp at hv
  | stat _ ih =>
      intro i v hv
      cases i with
      | zero   => simp only [List.getElem?_cons_zero, Option.some.injEq] at hv
                  cases hv; simp
      | succ n => simpa using ih n v (by simpa using hv)
  | dyn _ _ ih =>
      intro i v hv
      cases i with
      | zero   => simp at hv
      | succ n => simpa using ih n v (by simpa using hv)

theorem Compat_dyn_lookup {ρr Δ env ρs} (h : Compat ρr Δ env ρs) :
    ∀ (i k : Nat), env[i]? = some (PVal.dyn k) → ∃ v, ρs[i]? = some v ∧ ρr[k]? = some v := by
  induction h with
  | nil => intro i k hv; simp at hv
  | stat _ ih =>
      intro i k hv
      cases i with
      | zero   => simp at hv
      | succ n => have := ih n k (by simpa using hv)
                  obtain ⟨v, h1, h2⟩ := this
                  exact ⟨v, by simpa using h1, h2⟩
  | dyn hk _ ih =>
      intro i k hv
      cases i with
      | zero   => simp only [List.getElem?_cons_zero, Option.some.injEq] at hv
                  cases hv; exact ⟨_, by simp, hk⟩
      | succ n => have := ih n k (by simpa using hv)
                  obtain ⟨v, h1, h2⟩ := this
                  exact ⟨v, by simpa using h1, h2⟩

/-! ## The source environment a specialization stands for

`buildEnv` builds `mix`'s partial environment for a specialized function: static
parameters hold their values, and the `j`-th dynamic parameter becomes residual
index `j`.  `srcArgs` is the corresponding SOURCE argument list -- the two
interleaved back together.  `buildEnv_Compat` says these are the same
description, which is what connects a specialized function to the function it
specializes. -/

def srcArgs : Div → List Val → List Val → Option (List Val)
  | [],          [],      []      => some []
  | .stat :: bs, v :: vs, ds      => (srcArgs bs vs ds).map (v :: ·)
  | .dyn  :: bs, vs,      d :: ds => (srcArgs bs vs ds).map (d :: ·)
  | _, _, _ => none

/-- Stated with a prefix `pre` already consumed, because `buildEnv`'s dynamic
index counts from the start of the residual argument list while the recursion
walks the division. -/
theorem buildEnv_Compat : ∀ (ps : Div) (svs pre ds : List Val) (env : PEnv) (ρs : List Val),
    buildEnv ps svs pre.length = .ok env → srcArgs ps svs ds = some ρs →
    Compat (pre ++ ds) ps env ρs
  | [], [], pre, [], env, ρs, he, hs => by
      simp only [buildEnv] at he; simp only [srcArgs] at hs
      cases he; cases hs; exact .nil
  | .stat :: bs, v :: svs, pre, ds, env, ρs, he, hs => by
      simp only [buildEnv] at he
      split at he <;> try contradiction
      rename_i rest hrest
      cases he
      simp only [srcArgs, Option.map_eq_some_iff] at hs
      obtain ⟨ρs', hρs, rfl⟩ := hs
      exact .stat (buildEnv_Compat bs svs pre ds rest ρs' hrest hρs)
  | .dyn :: bs, svs, pre, d :: ds, env, ρs, he, hs => by
      simp only [buildEnv] at he
      split at he <;> try contradiction
      rename_i rest hrest
      cases he
      simp only [srcArgs, Option.map_eq_some_iff] at hs
      obtain ⟨ρs', hρs, rfl⟩ := hs
      refine .dyn (k := pre.length) ?_ ?_
      · simp
      · have : (pre ++ [d]).length = pre.length + 1 := by simp
        have hc := buildEnv_Compat bs svs (pre ++ [d]) ds rest ρs' (by rw [this]; exact hrest) hρs
        simpa using hc
  -- the shapes `buildEnv` rejects
  | [],          _ :: _,  _, _,      _, _, he, _ => by simp [buildEnv] at he
  | .stat :: _,  [],      _, _,      _, _, he, _ => by simp [buildEnv] at he
  | .dyn :: _,   _,       _, [],     _, _, _,  hs => by simp [srcArgs] at hs
  | [],          [],      _, _ :: _, _, _, _,  hs => by simp [srcArgs] at hs

/-! ## The residual function table

`generate` produces one residual function per request, in order, so residual
function `i` is the specialization of request `i`. -/

theorem generateFrom_spec {stepFuel : Nat} {A : AProgram} {idx : SpecRequest → Option Nat} :
    ∀ (rs : List SpecRequest) (funs : List FunDef),
      generateFrom stepFuel A idx rs = .ok funs →
      ∀ (i : Nat) (req : SpecRequest), rs[i]? = some req →
        ∃ fd rq, mixFun stepFuel A idx req = .ok (fd, rq) ∧ funs[i]? = some fd
  | [],      _,    h, i, _,   hi => by simp at hi
  | r :: rs, funs, h, i, req, hi => by
      simp only [generateFrom] at h
      split at h <;> try contradiction
      rename_i fd rq fds hfd hfds
      cases h
      cases i with
      | zero =>
          simp only [List.getElem?_cons_zero, Option.some.injEq] at hi
          cases hi
          exact ⟨fd, rq, hfd, by simp⟩
      | succ n =>
          obtain ⟨fd', rq', h1, h2⟩ :=
            generateFrom_spec rs fds hfds n req (by simpa using hi)
          exact ⟨fd', rq', h1, by simpa using h2⟩

/-! ## What it means for the residual table to be right

`SpecOK … m` is the statement indexed by SOURCE FUEL, and that is what makes the
knot untieable.  A specialized function's correctness depends on the correctness
of the functions it calls -- including itself -- so no structural induction
closes the loop.  Source fuel does: a call evaluates its callee at strictly less
fuel, so `SpecOK m` needs only `SpecOK k` for `k < m`, and the whole family
follows by strong induction on `m`.

Note the direction: this is PRESERVATION -- whatever the source computes, the
residual computes too.  Soundness (the residual computes nothing else) is the
mirror statement indexed by residual fuel. -/

def SpecOK (A : AProgram) (Pr : Program) (reqs : List SpecRequest) (m : Nat) : Prop :=
  ∀ (i : Nat) (req : SpecRequest), reqs[i]? = some req →
    ∃ fd afd, Pr.funs[i]? = some fd ∧ A.fn req.funIdx = some afd ∧
      -- the residual function keeps exactly the dynamic parameters; a residual
      -- call site has to know that to justify its own argument count
      fd.arity = dynCount afd.params ∧
      ∀ (ds ρs : List Val) (v : Val),
        srcArgs afd.params req.staticArgs ds = some ρs →
        evalFuel m (eraseProgram A) ρs (erase afd.body) = .value v →
        Eval Pr ds fd.body v

/-! ## Partial results

`PResOK Pr ρr r v` -- `mix` produced `r` where the source produces `v`, and `r`
is an honest account of that: a static result IS the value, and residual code
EVALUATES to it. -/

def PResOK (Pr : Program) (ρr : Env) (r : PRes) (v : Val) : Prop :=
  (∀ w, r = .stat w → w = v) ∧ (∀ c, r = .code c → Eval Pr ρr c v)

/-- Pointwise `PResOK` over an argument list.  Written out rather than using
`List.Forall₂`, which is not in core. -/
inductive PResAll (Pr : Program) (ρr : Env) : List PRes → List Val → Prop where
  | nil  : PResAll Pr ρr [] []
  | cons : PResOK Pr ρr r v → PResAll Pr ρr rs vs → PResAll Pr ρr (r :: rs) (v :: vs)

theorem PResOK_toCode {Pr ρr r v} (h : PResOK Pr ρr r v) : Eval Pr ρr r.toCode v := by
  cases r with
  | stat w => have : w = v := h.1 w rfl
              subst this
              exact .lit
  | code c => exact h.2 c rfl

theorem allStatic_forall₂ : ∀ (Pr : Program) (ρr : Env) (rs : List PRes)
    (vs ws : List Val), PResAll Pr ρr rs vs → allStatic rs = .ok ws → ws = vs
  | _,  _,  [],            [],      ws, _, hw => by simp [allStatic] at hw; simp [hw]
  | Pr, ρr, .stat w :: rs, v :: vs, ws, h, hw => by
      cases h with
      | cons hr ht =>
        simp only [allStatic] at hw
        split at hw <;> try contradiction
        rename_i us hus
        cases hw
        rw [allStatic_forall₂ Pr ρr rs vs us ht hus, hr.1 w rfl]
  | _,  _,  .code _ :: _,  _,       _,  _, hw => by simp [allStatic] at hw
  | _,  _,  _ :: _,        [],      _,  h, _  => by cases h
  | _,  _,  [],            _ :: _,  _,  h, _  => by cases h

theorem toCode_forall₂ : ∀ (Pr : Program) (ρr : Env) (rs : List PRes) (vs : List Val),
    PResAll Pr ρr rs vs → EvalList Pr ρr (rs.map PRes.toCode) vs
  | _,  _,  [],      [],      _ => .nil
  | Pr, ρr, r :: rs, v :: vs, h => by
      cases h with
      | cons hr ht => exact .cons (PResOK_toCode hr) (toCode_forall₂ Pr ρr rs vs ht)
  | _,  _,  _ :: _,  [],      h => by cases h
  | _,  _,  [],      _ :: _,  h => by cases h

/-! ## The memo table names a real request -/

theorem indexOfReqFrom_spec : ∀ (rs : List SpecRequest) (r : SpecRequest) (i k : Nat),
    indexOfReqFrom r i rs = some k → i ≤ k ∧ rs[k - i]? = some r
  | [],      _, _, _, h => by simp [indexOfReqFrom] at h
  | q :: rs, r, i, k, h => by
      simp only [indexOfReqFrom] at h
      split at h
      · rename_i heq
        cases h
        refine ⟨Nat.le_refl _, ?_⟩
        simp only [Nat.sub_self, List.getElem?_cons_zero, Option.some.injEq]
        -- `beq` on requests is equality: the index really names THIS request
        simp only [BEq.beq, SpecRequest.beq, Bool.and_eq_true] at heq
        have h1 : r.funIdx = q.funIdx := by simpa using heq.1
        have h2 : r.staticArgs = q.staticArgs := Val.eqList_of_beqList _ _ heq.2
        cases q; cases r; simp_all
      · obtain ⟨hle, hget⟩ := indexOfReqFrom_spec rs r (i + 1) k h
        refine ⟨by omega, ?_⟩
        have : k - i = (k - (i + 1)) + 1 := by omega
        rw [this]
        simpa using hget

theorem indexOfReq_spec {rs : List SpecRequest} {r : SpecRequest} {k : Nat}
    (h : indexOfReq rs r = some k) : rs[k]? = some r := by
  have := indexOfReqFrom_spec rs r 0 k h
  simpa using this.2

/-! ## The `let`s an unfold wraps

`EvalLets` is what `wrapLets` means: the bound terms are evaluated one after
another, each in the environment the previous ones have already extended.  That
staircase is exactly why `mixUArgs` has to thread the residual scope. -/

inductive EvalLets (P : Program) : Env → List Term → Env → Prop where
  | nil  : EvalLets P ρ [] ρ
  | cons : Eval P ρ e d → EvalLets P (d :: ρ) es ρ' → EvalLets P ρ (e :: es) ρ'

theorem wrapLets_eval {P : Program} : ∀ (es : List Term) (ρ ρ' : Env) (body : Term) (v : Val),
    EvalLets P ρ es ρ' → Eval P ρ' body v → Eval P ρ (wrapLets es body) v
  | [],      _, _, _,    _, hl, hb => by cases hl; exact hb
  | e :: es, ρ, ρ', body, v, hl, hb => by
      cases hl with
      | cons he ht => exact .letIn he (wrapLets_eval es _ ρ' body v ht hb)

/-! ## A fully static call's environment -/

theorem Compat_allStat : ∀ (ρr : Env) (ps : Div) (ws : List Val),
    allStatDiv ps = true → ps.length = ws.length →
    Compat ρr ps (ws.map PVal.stat) ws
  | _,  [],          [],      _, _  => .nil
  | ρr, .stat :: ps, w :: ws, h, hl => by
      simp only [allStatDiv] at h
      simp only [List.length_cons, Nat.add_right_cancel_iff] at hl
      exact (Compat_allStat ρr ps ws h hl).stat
  | _,  .dyn :: _,   _,       h, _  => by simp [allStatDiv] at h
  | _,  [],          _ :: _,  _, hl => by simp at hl
  | _,  .stat :: _,  [],      _, hl => by simp at hl

/-! ## The claim, at one mix fuel and one source fuel

`TOK` is what the main induction proves.  `mixTerms`, `mixAlts` and `mixUArgs`
all call `mixTerm` at the SAME mix fuel, so none of them can be co-inducted with
it; each is derived from `TOK` at that fuel instead, exactly as
`evalFuelList_sound_of` is derived from the term case. -/

def TOK (A : AProgram) (Pr : Program) (reqs : List SpecRequest) (n m : Nat) : Prop :=
  ∀ (Δ : Div) (env : PEnv) (t : ATerm) (r : PRes) (rq : List SpecRequest)
    (ρr ρs : Env) (v : Val),
    Compat ρr Δ env ρs →
    mixTerm n A (indexOfReq reqs) Δ env t = .ok (r, rq) →
    evalFuel m (eraseProgram A) ρs (erase t) = .value v →
    PResOK Pr ρr r v

theorem mixTerms_ok {A Pr reqs n m} (h : TOK A Pr reqs n m) :
    ∀ (Δ : Div) (env : PEnv) (ts : List ATerm) (rs : List PRes) (rq : List SpecRequest)
      (ρr ρs : Env) (vs : List Val),
      Compat ρr Δ env ρs →
      mixTerms n A (indexOfReq reqs) Δ env ts = .ok (rs, rq) →
      evalFuelList m (eraseProgram A) ρs (eraseList ts) = .inl vs →
      PResAll Pr ρr rs vs := by
  intro Δ env ts
  induction ts with
  | nil =>
      intro rs rq ρr ρs vs _ hmix hsrc
      simp only [mixTerms] at hmix
      simp only [eraseList, evalFuelList] at hsrc
      cases hmix; cases hsrc; exact .nil
  | cons t ts ih =>
      intro rs rq ρr ρs vs hc hmix hsrc
      simp only [mixTerms] at hmix
      split at hmix <;> try contradiction
      rename_i r rq₁ rs' rq₂ ht hts
      cases hmix
      simp only [eraseList, evalFuelList] at hsrc
      split at hsrc <;> try contradiction
      rename_i v' hv'
      split at hsrc <;> try contradiction
      rename_i vs' hvs'
      cases hsrc
      exact .cons (h Δ env t r rq₁ ρr ρs v' hc ht hv') (ih rs' rq₂ ρr ρs vs' hc hts hvs')

/-! ## What a residual call site needs

`splitArgs` sends the static operands to the request and the dynamic ones into
the residual call.  This says the residual arguments evaluate to values that
`srcArgs` interleaves back into exactly the source argument list -- which is the
hypothesis `SpecOK` is stated against. -/

theorem splitArgs_spec {Pr : Program} {ρr : Env} :
    ∀ (ps : Div) (rs : List PRes) (vs svs : List Val) (dts : List Term),
      PResAll Pr ρr rs vs → splitArgs ps rs = .ok (svs, dts) →
      ∃ ds, EvalList Pr ρr dts ds ∧ srcArgs ps svs ds = some vs ∧
            ds.length = dynCount ps
  | [],          [],            [],      svs, dts, _, hsp => by
      simp only [splitArgs] at hsp; cases hsp
      exact ⟨[], .nil, rfl, rfl⟩
  | .stat :: ps, .stat w :: rs, v :: vs, svs, dts, hall, hsp => by
      cases hall with
      | cons hr ht =>
        simp only [splitArgs] at hsp
        split at hsp <;> try contradiction
        rename_i svs' dts' hrec
        -- recurse BEFORE `cases hsp`: that equation unifies away one of these
        -- names, and which one differs between the two branches
        obtain ⟨ds, hev, hsrc, hlen⟩ := splitArgs_spec ps rs vs svs' dts' ht hrec
        cases hsp
        refine ⟨ds, hev, ?_, by simpa [dynCount] using hlen⟩
        have : w = v := hr.1 w rfl
        subst this
        simp [srcArgs, hsrc]
  | .dyn :: ps,  r :: rs,       v :: vs, svs, dts, hall, hsp => by
      cases hall with
      | cons hr ht =>
        simp only [splitArgs] at hsp
        split at hsp <;> try contradiction
        rename_i svs' dts' hrec
        obtain ⟨ds, hev, hsrc, hlen⟩ := splitArgs_spec ps rs vs svs' dts' ht hrec
        cases hsp
        exact ⟨v :: ds, .cons (PResOK_toCode hr) hev, by simp [srcArgs, hsrc],
               by simpa [dynCount] using hlen⟩
  | .stat :: _,  .code _ :: _,  _,       _,   _,   _,    hsp => by simp [splitArgs] at hsp
  | [],          _ :: _,        _,       _,   _,   _,    hsp => by simp [splitArgs] at hsp
  | _ :: _,      [],            _,       _,   _,   _,    hsp => by simp [splitArgs] at hsp
  | .stat :: _,  .stat _ :: _,  [],      _,   _,   hall, _   => by cases hall
  | .dyn :: _,   _ :: _,        [],      _,   _,   hall, _   => by cases hall
  | [],          [],            _ :: _,  _,   _,   hall, _   => by cases hall

/-- The converse direction of `findAlt_eraseAlts`: whatever the SOURCE selected
came from an annotated alternative, which is the one `mix` looked at. -/
theorem findAAlt_of_findAlt : ∀ (as : List AAlt) (tag : Nat) (a' : Alt),
    findAlt (eraseAlts as) tag = some a' →
    ∃ af, findAAlt as tag = some af ∧ a' = (af.tag, af.arity, erase af.body)
  | [],       _,   _,  h => by simp [eraseAlts, findAlt] at h
  | a0 :: as, tag, a', h => by
      -- deliberately NOT unfolding `Alt.tag` in `h`: the two projections are
      -- definitionally equal but not syntactically, and `rw` needs the latter
      simp only [eraseAlts, findAlt] at h
      split at h
      · rename_i htag
        cases h
        refine ⟨a0, ?_, rfl⟩
        simp only [findAAlt]
        split
        · rfl
        · rename_i h2; exact absurd htag h2
      · rename_i htag
        obtain ⟨af, hf, he⟩ := findAAlt_of_findAlt as tag a' h
        refine ⟨af, ?_, he⟩
        simp only [findAAlt]
        split
        · rename_i h2; exact absurd h2 htag
        · exact hf

/-! ## Introducing a partial result -/

theorem PResOK_stat {Pr ρr w v} (h : w = v) : PResOK Pr ρr (.stat w) v :=
  ⟨(fun _ hw => by cases hw; exact h), (fun _ hc => by cases hc)⟩

theorem PResOK_code {Pr ρr c v} (h : Eval Pr ρr c v) : PResOK Pr ρr (.code c) v :=
  ⟨(fun _ hw => by cases hw), (fun _ hc => by cases hc; exact h)⟩

/-! ## Alternatives of a residualized `caseT`

`mixAlts` keeps each alternative's tag and arity and specializes its body under
the fields bound as fresh residual indices.  Stated as "whatever the source
selected, the residual selects an alternative that agrees with it", because that
is the shape `Eval.caseT` consumes. -/

theorem mixAlts_ok {A Pr reqs n m} (h : TOK A Pr reqs n m) :
    ∀ (Δ : Div) (env : PEnv) (as : List AAlt) (as' : List Alt) (rq : List SpecRequest)
      (ρr ρs : Env) (tag : Nat) (af : AAlt) (vs : List Val) (v : Val),
      Compat ρr Δ env ρs →
      mixAlts n A (indexOfReq reqs) Δ env as = .ok (as', rq) →
      findAAlt as tag = some af →
      af.arity = vs.length →
      evalFuel m (eraseProgram A) (vs ++ ρs) (erase af.body) = .value v →
      ∃ a'', findAlt as' tag = some a'' ∧ a''.arity = vs.length ∧
             Eval Pr (vs ++ ρr) a''.body v := by
  intro Δ env as
  induction as with
  | nil => intro _ _ _ _ _ _ _ _ _ _ hfind _ _; simp [findAAlt] at hfind
  | cons a0 as ih =>
      intro as' rq ρr ρs tag af vs v hc hmix hfind harity hsrc
      simp only [mixAlts] at hmix
      split at hmix <;> try contradiction
      rename_i r rq₁ as2 rq₂ hb has
      cases hmix
      simp only [findAAlt] at hfind
      split at hfind
      · -- the source selected the head alternative; so does the residual
        rename_i htag
        cases hfind
        -- `cases hfind` identified the found alternative with the head
        refine ⟨(a0.tag, a0.arity, r.toCode),
                by simp [findAlt, Alt.tag, htag], harity, ?_⟩
        have hcf := Compat_fields_dyn (Δ := Δ) (env := env) vs hc
        rw [← harity] at hcf
        exact PResOK_toCode (h _ _ a0.body r rq₁ (vs ++ ρr) (vs ++ ρs) v hcf hb hsrc)
      · rename_i htag
        obtain ⟨a'', hf, har, hev⟩ := ih as2 rq₂ ρr ρs tag af vs v hc has hfind harity hsrc
        exact ⟨a'', by simp [findAlt, Alt.tag, htag, hf], har, hev⟩

/-! ## Arguments of an unfolded call

The one place where the residual scope grows while `mix` is still walking, so
the statement has to say WHERE each argument's value ends up: `ws.reverse ++ ρr`
names the scope the `let`s build, and `inlineEnv`'s index into it is
`dynCount` of the parameters still to come. -/

theorem mixUArgs_ok {A Pr reqs n m} (h : TOK A Pr reqs n m) :
    ∀ (ps : Div) (ts : List ATerm) (Δ : Div) (env : PEnv) (rs : List PRes)
      (dts : List Term) (rq : List SpecRequest) (ρr ρs : Env) (vs : List Val)
      (env' : PEnv),
      Compat ρr Δ env ρs →
      mixUArgs n A (indexOfReq reqs) Δ env ps ts = .ok (rs, dts, rq) →
      evalFuelList m (eraseProgram A) ρs (eraseList ts) = .inl vs →
      inlineEnv ps rs = .ok env' →
      ∃ ws : List Val, ws.length = dynCount ps ∧
            EvalLets Pr ρr dts (ws.reverse ++ ρr) ∧
            Compat (ws.reverse ++ ρr) ps env' vs := by
  intro ps
  induction ps with
  | nil =>
      intro ts Δ env rs dts rq ρr ρs vs env' _ hmix hsrc hie
      cases ts with
      | nil =>
          simp only [mixUArgs] at hmix
          cases hmix
          simp only [eraseList, evalFuelList] at hsrc
          cases hsrc
          simp only [inlineEnv] at hie
          cases hie
          exact ⟨[], rfl, by simpa using EvalLets.nil, by simpa using Compat.nil⟩
      | cons _ _ => simp [mixUArgs] at hmix
  | cons b bs ih =>
      intro ts Δ env rs dts rq ρr ρs vs env' hc hmix hsrc hie
      cases ts with
      | nil => cases b <;> simp [mixUArgs] at hmix
      | cons t ts =>
        cases b with
        | stat =>
            simp only [mixUArgs] at hmix
            split at hmix <;> try contradiction
            rename_i r rq₁ rs' dts' rq₂ ht hrec
            cases hmix
            simp only [eraseList, evalFuelList] at hsrc
            split at hsrc <;> try contradiction
            rename_i v₀ hv₀
            split at hsrc <;> try contradiction
            rename_i vs' hvs'
            cases hsrc
            have hr := h Δ env t r rq₁ ρr ρs v₀ hc ht hv₀
            cases r with
            | code _ => simp [inlineEnv] at hie
            | stat w =>
                simp only [inlineEnv] at hie
                split at hie <;> try contradiction
                rename_i env'' hie'
                cases hie
                obtain ⟨ws, hlen, hlets, hcp⟩ :=
                  ih ts Δ env rs' _ rq₂ ρr ρs vs' env'' hc hrec hvs' hie'
                refine ⟨ws, by simpa [dynCount] using hlen, hlets, ?_⟩
                have hw : w = v₀ := hr.1 w rfl
                subst hw
                exact hcp.stat
        | dyn =>
            simp only [mixUArgs] at hmix
            split at hmix <;> try contradiction
            rename_i r rq₁ rs' dts' rq₂ ht hrec
            cases hmix
            simp only [eraseList, evalFuelList] at hsrc
            split at hsrc <;> try contradiction
            rename_i v₀ hv₀
            split at hsrc <;> try contradiction
            rename_i vs' hvs'
            cases hsrc
            have hr := h Δ env t r rq₁ ρr ρs v₀ hc ht hv₀
            simp only [inlineEnv] at hie
            split at hie <;> try contradiction
            rename_i env'' hie'
            cases hie
            obtain ⟨ws, hlen, hlets, hcp⟩ :=
              ih ts Δ (env.shiftBy 1) rs' dts' rq₂ (v₀ :: ρr) ρs vs' env''
                 (Compat_shift1 v₀ hc) hrec hvs' hie'
            have heq : (v₀ :: ws).reverse ++ ρr = ws.reverse ++ (v₀ :: ρr) := by simp
            refine ⟨v₀ :: ws, by simp [dynCount, hlen], ?_, ?_⟩
            · rw [heq]; exact .cons (PResOK_toCode hr) hlets
            · rw [heq]
              refine .dyn ?_ hcp
              have hl : ws.reverse.length = dynCount bs := by simp [hlen]
              rw [List.getElem?_append_right (by omega), hl]
              simp

/-! ## The main induction

Induction on MIX FUEL.  It is enough on its own: every recursive call `mixTerm`
makes decreases it, including the ones that cross a function boundary, and the
source fuel is universally quantified inside so a case that also consumes source
fuel can still appeal to the hypothesis.  Residual calls are not an induction
step at all -- they are discharged by `SpecOK`, which is where the mutual
recursion of the residual program is absorbed. -/

theorem mixTerm_complete (A : AProgram) (Pr : Program) (reqs : List SpecRequest) :
    ∀ (n m : Nat), (∀ k, k < m → SpecOK A Pr reqs k) → TOK A Pr reqs n m := by
  intro n
  induction n with
  | zero =>
      intro m _ Δ env t r rq ρr ρs v _ hmix _
      simp [mixTerm] at hmix
  | succ n ih =>
      intro m hspec Δ env t r rq ρr ρs v hc hmix hsrc
      cases m with
      | zero => simp [evalFuel] at hsrc
      | succ m' =>
        have ihm  : TOK A Pr reqs n (m' + 1) := ih (m' + 1) hspec
        have ihm' : TOK A Pr reqs n m' := ih m' (fun k hk => hspec k (by omega))
        have hsub : ∀ k, k < m' → SpecOK A Pr reqs k := fun k hk => hspec k (by omega)
        cases t with

        | lit w =>
            simp only [mixTerm] at hmix
            cases hmix
            simp only [erase, evalFuel] at hsrc
            cases hsrc
            exact PResOK_stat rfl

        | var i =>
            simp only [mixTerm] at hmix
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            rename_i u hu
            cases hsrc
            split at hmix <;> try contradiction
            · -- static variable: the division and the environment agree, and
              -- `Compat` says the environment agrees with the source
              rename_i w _ henv
              cases hmix
              exact PResOK_stat (by
                have := Compat_stat_lookup hc i w henv
                rw [hu] at this; exact (Option.some.inj this).symm)
            · rename_i k _ henv
              cases hmix
              obtain ⟨u', hs, hr⟩ := Compat_dyn_lookup hc i k henv
              rw [hu] at hs
              cases Option.some.inj hs
              exact PResOK_code (.var hr)

        | lift e =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i w rq' he
            cases hmix
            simp only [erase] at hsrc
            have := ihm Δ env e (.stat w) _ ρr ρs v hc he hsrc
            exact PResOK_code (by rw [this.1 w rfl]; exact .lit)

        | letIn _ e body =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i re rq₁ he
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            rename_i v₁ hv₁
            have hre := ihm' Δ env e re rq₁ ρr ρs v₁ hc he hv₁
            split at hmix <;> try contradiction
            · -- static binding: no residual binder, so `ρr` does not move
              rename_i w _
              split at hmix <;> try contradiction
              rename_i rb rq₂ hbody
              cases hmix
              have hw : w = v₁ := hre.1 w rfl
              subst hw
              exact ihm' _ _ body r _ ρr (w :: ρs) v (hc.stat) hbody hsrc
            · -- dynamic binding: one residual binder, so everything shifts
              rename_i _
              split at hmix <;> try contradiction
              rename_i b' rq₂ hbody
              cases hmix
              have hcb : Compat (v₁ :: ρr) (.dyn :: Δ) (.dyn 0 :: env.shiftBy 1) (v₁ :: ρs) :=
                .dyn (by simp) (Compat_shift1 v₁ hc)
              have hb := ihm' _ _ body (.code b') rq₂ (v₁ :: ρr) (v₁ :: ρs) v hcb hbody hsrc
              exact PResOK_code (.letIn (PResOK_toCode hre) (hb.2 b' rfl))

        | prim b p ts =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i rs rq' hts
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            rename_i vs hvs
            split at hsrc <;> try contradiction
            rename_i w hp
            cases hsrc
            have hall := mixTerms_ok ihm' Δ env ts rs rq' ρr ρs vs hc hts hvs
            split at hmix
            · split at hmix <;> try contradiction
              rename_i ws hws
              split at hmix <;> try contradiction
              rename_i w' hp'
              cases hmix
              rw [allStatic_forall₂ Pr ρr rs vs ws hall hws] at hp'
              rw [hp] at hp'
              exact PResOK_stat (Except.ok.inj hp').symm
            · cases hmix
              exact PResOK_code (.prim (toCode_forall₂ Pr ρr rs vs hall) hp)

        | ctorT b k ts =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i rs rq' hts
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            rename_i vs hvs
            cases hsrc
            have hall := mixTerms_ok ihm' Δ env ts rs rq' ρr ρs vs hc hts hvs
            split at hmix
            · split at hmix <;> try contradiction
              rename_i ws hws
              cases hmix
              exact PResOK_stat (by rw [allStatic_forall₂ Pr ρr rs vs ws hall hws])
            · cases hmix
              exact PResOK_code (.ctorT (toCode_forall₂ Pr ρr rs vs hall))

        | ite _ c a e =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i rc rq₁ hcm
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            · -- the source condition was `true`
              rename_i hcond
              have hrc := ihm' Δ env c rc rq₁ ρr ρs (.bool true) hc hcm hcond
              split at hmix <;> try contradiction
              · split at hmix <;> try contradiction
                rename_i ra rq₂ ha
                cases hmix
                exact ihm' _ _ a r _ ρr ρs v hc ha hsrc
              · -- `mix` decided `false`, the source went `true`
                rename_i _
                exact absurd (hrc.1 _ rfl) (by simp)
              · split at hmix <;> try contradiction
                rename_i ra rq₂ re' rq₃ ha he
                cases hmix
                exact PResOK_code (.iteT (PResOK_toCode hrc)
                  (PResOK_toCode (ihm' _ _ a ra _ ρr ρs v hc ha hsrc)))
            · -- the source condition was `false`
              rename_i hcond
              have hrc := ihm' Δ env c rc rq₁ ρr ρs (.bool false) hc hcm hcond
              split at hmix <;> try contradiction
              · rename_i _
                exact absurd (hrc.1 _ rfl) (by simp)
              · split at hmix <;> try contradiction
                rename_i re' rq₂ he
                cases hmix
                exact ihm' _ _ e r _ ρr ρs v hc he hsrc
              · split at hmix <;> try contradiction
                rename_i ra rq₂ re' rq₃ ha he
                cases hmix
                exact PResOK_code (.iteF (PResOK_toCode hrc)
                  (PResOK_toCode (ihm' _ _ e re' _ ρr ρs v hc he hsrc)))

        | caseT _ s alts =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i rsc rq₁ hsm
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            rename_i tag vs hscr
            split at hsrc <;> try contradiction
            rename_i a' hfa
            split at hsrc <;> try contradiction
            rename_i har
            have hs := ihm' Δ env s rsc rq₁ ρr ρs (.ctor tag vs) hc hsm hscr
            obtain ⟨af, hfaf, rfl⟩ := findAAlt_of_findAlt alts tag a' hfa
            simp only [Alt.arity] at har
            split at hmix <;> try contradiction
            · -- static scrutinee: `mix` selected the alternative itself
              rename_i tag' vs' _
              have hct : Val.ctor tag' vs' = Val.ctor tag vs := hs.1 _ rfl
              cases hct
              -- split the alternative lookup rather than rewriting into it: a
              -- `rw` leaves `match some af with …` unreduced
              split at hmix <;> try contradiction
              rename_i a ha
              rw [hfaf] at ha
              cases ha
              split at hmix <;> try contradiction
              rename_i _
              split at hmix <;> try contradiction
              rename_i rb rq₂ hbody
              cases hmix
              have hcf := Compat_fields_stat (Δ := Δ) (env := env) vs hc
              rw [← har] at hcf
              exact ihm' _ _ af.body r _ ρr (vs ++ ρs) v hcf hbody (by simpa [Alt.body] using hsrc)
            · -- dynamic scrutinee: a residual `caseT` survives
              rename_i _
              split at hmix <;> try contradiction
              rename_i alts' rq₂ halts
              cases hmix
              obtain ⟨a'', hf, harr, hev⟩ :=
                mixAlts_ok ihm' Δ env alts alts' rq₂ ρr ρs tag af vs v hc halts hfaf har
                  (by simpa [Alt.body] using hsrc)
              exact PResOK_code (.caseT (PResOK_toCode hs) hf harr hev)

        | call b f ts =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i rs rq₁ hts
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            rename_i vs hvs
            have hall := mixTerms_ok ihm' Δ env ts rs rq₁ ρr ρs vs hc hts hvs
            split at hmix <;> try contradiction
            rename_i fd hfn
            split at hsrc <;> try contradiction
            rename_i fd' hfn'
            have hfe : fd' = eraseFunDef fd := by
              have h1 := eraseProgram_fn hfn
              rw [hfn'] at h1; exact Option.some.inj h1
            subst hfe
            split at hsrc <;> try contradiction
            rename_i harity
            split at hmix
            · -- static: every argument is known, so unfold the callee here
              split at hmix <;> try contradiction
              rename_i hasd
              -- the argument-count test `mix` now performs
              split at hmix <;> try contradiction
              rename_i _
              split at hmix <;> try contradiction
              rename_i ws hws
              split at hmix <;> try contradiction
              rename_i rb rq₂ hbody
              cases hmix
              have hwv : ws = vs := allStatic_forall₂ Pr ρr rs vs ws hall hws
              subst hwv
              exact ihm' _ _ fd.body r _ ρr ws v
                (Compat_allStat ρr fd.params ws hasd (by simpa [eraseFunDef] using harity))
                hbody (by simpa [eraseFunDef] using hsrc)
            · -- dynamic: a residual call, discharged by `SpecOK` rather than by
              -- an induction step -- this is where the residual program's mutual
              -- recursion is absorbed
              split at hmix <;> try contradiction
              rename_i svs dts hsplit
              split at hmix <;> try contradiction
              rename_i k hidx
              cases hmix
              obtain ⟨ds, hev, hsrcargs, hlen⟩ := splitArgs_spec fd.params rs vs svs dts hall hsplit
              obtain ⟨fdr, afd, hfr, hafd, harr, hcall⟩ :=
                hspec m' (by omega) k ⟨f, svs⟩ (indexOfReq_spec hidx)
              rw [hfn] at hafd
              cases hafd
              exact PResOK_code (.call hev hfr (by rw [harr, hlen]) (hcall ds vs v hsrcargs
                (by simpa [eraseFunDef] using hsrc)))

        | ucall b f ts =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i fd hfn
            simp only [erase, evalFuel] at hsrc
            split at hsrc <;> try contradiction
            rename_i vs hvs
            split at hsrc <;> try contradiction
            rename_i fd' hfn'
            have hfe : fd' = eraseFunDef fd := by
              have h1 := eraseProgram_fn hfn
              rw [hfn'] at h1; exact Option.some.inj h1
            subst hfe
            split at hsrc <;> try contradiction
            rename_i harity
            split at hmix
            · -- static unfold: identical to a static `call`
              split at hmix <;> try contradiction
              rename_i rs rq₁ hts
              split at hmix <;> try contradiction
              rename_i hasd
              -- the argument-count test `mix` now performs
              split at hmix <;> try contradiction
              rename_i _
              split at hmix <;> try contradiction
              rename_i ws hws
              split at hmix <;> try contradiction
              rename_i rb rq₂ hbody
              cases hmix
              have hall := mixTerms_ok ihm' Δ env ts rs rq₁ ρr ρs vs hc hts hvs
              have hwv : ws = vs := allStatic_forall₂ Pr ρr rs vs ws hall hws
              subst hwv
              exact ihm' _ _ fd.body r _ ρr ws v
                (Compat_allStat ρr fd.params ws hasd (by simpa [eraseFunDef] using harity))
                hbody (by simpa [eraseFunDef] using hsrc)
            · -- inline: the `let`s build the scope the body is specialized in
              split at hmix <;> try contradiction
              rename_i rs' dts rq₂ hua
              split at hmix <;> try contradiction
              rename_i env' hie
              split at hmix <;> try contradiction
              rename_i b' rq₃ hbody
              cases hmix
              obtain ⟨ws, _, hlets, hcp⟩ :=
                mixUArgs_ok ihm' fd.params ts Δ env rs' dts rq₂ ρr ρs vs env' hc hua hvs hie
              have hb := ihm' _ _ fd.body (.code b') rq₃ (ws.reverse ++ ρr) vs v hcp hbody
                           (by simpa [eraseFunDef] using hsrc)
              exact PResOK_code
                (wrapLets_eval dts ρr (ws.reverse ++ ρr) b' v hlets (hb.2 b' rfl))

/-! ## Tying the knot

`mixTerm_complete` assumed `SpecOK` at every smaller source fuel.  Here that
assumption is discharged: `SpecOK m` is proved from `SpecOK k` for `k < m`, and
strong induction on `m` gives the whole family.  This is the step the residual
program's mutual recursion lives in -- a specialized function may call itself,
but only after consuming source fuel. -/

theorem mixFun_spec {stepFuel A idx req fd rq}
    (h : mixFun stepFuel A idx req = .ok (fd, rq)) :
    ∃ afd env r, A.fn req.funIdx = some afd ∧
      buildEnv afd.params req.staticArgs 0 = .ok env ∧
      mixTerm stepFuel A idx afd.params env afd.body = .ok (r, rq) ∧
      fd = ⟨dynCount afd.params, r.toCode⟩ := by
  simp only [mixFun] at h
  split at h <;> try contradiction
  rename_i afd hfn
  split at h <;> try contradiction
  rename_i env hbe
  split at h <;> try contradiction
  rename_i r rq' hmt
  cases h
  exact ⟨afd, env, r, hfn, hbe, hmt, rfl⟩

theorem specOK_all (A : AProgram) (Pr : Program) (reqs : List SpecRequest) (stepFuel : Nat)
    (hgen : generateFrom stepFuel A (indexOfReq reqs) reqs = .ok Pr.funs) :
    ∀ m, SpecOK A Pr reqs m := by
  intro m
  induction m using Nat.strongRecOn with
  | _ m IH =>
    intro i req hreq
    obtain ⟨fd, rq, hmf, hfuns⟩ := generateFrom_spec reqs Pr.funs hgen i req hreq
    obtain ⟨afd, env, r, hfn, hbe, hmt, rfl⟩ := mixFun_spec hmf
    refine ⟨⟨dynCount afd.params, r.toCode⟩, afd, hfuns, hfn, rfl, ?_⟩
    intro ds ρs v hsrcargs hev
    have hc : Compat ds afd.params env ρs := by
      have := buildEnv_Compat afd.params req.staticArgs [] ds env ρs (by simpa using hbe) hsrcargs
      simpa using this
    exact PResOK_toCode
      (mixTerm_complete A Pr reqs stepFuel m IH afd.params env afd.body r rq ds ρs v hc hmt hev)

/-! ## The driver

`discover` only ever appends, so the entry request stays at index 0 and the
residual entry really is the specialization of the program's entry. -/

theorem discover_prefix {stepFuel : Nat} {A : AProgram} :
    ∀ (k : Nat) (work seen reqs : List SpecRequest),
      discover stepFuel k A work seen = .ok reqs → ∃ suf, reqs = seen ++ suf
  | _,     [],          seen, reqs, h => by
      simp only [discover] at h; cases h; exact ⟨[], by simp⟩
  | 0,     _ :: _,      _,    _,    h => by simp [discover] at h
  | k + 1, req :: work, seen, reqs, h => by
      simp only [discover] at h
      split at h <;> try contradiction
      rename_i rq hmf
      obtain ⟨suf, hsuf⟩ :=
        discover_prefix k (work ++ addNew seen rq) (seen ++ addNew seen rq) reqs h
      exact ⟨addNew seen rq ++ suf, by simpa using hsuf⟩

/-- `mix_sound`, at the driver.

Whatever the source program computes on the combined arguments, the residual
entry computes on the dynamic ones alone.  `srcArgs` is the combination:
it interleaves the static arguments `mix` was given back with the dynamic ones
the residual is called with, in the order the callee's division says. -/
theorem mixDriver_sound {stepFuel wlFuel : Nat} {A : AProgram} {statics : List Val}
    {Pr : Program} (h : mixDriver stepFuel wlFuel A statics = .ok Pr) :
    ∀ (m : Nat) (ds ρs : List Val) (v : Val) (fd : FunDef) (afd : AFunDef),
      Pr.fn Pr.entry = some fd →
      A.fn A.entry = some afd →
      srcArgs afd.params statics ds = some ρs →
      evalFuel m (eraseProgram A) ρs (erase afd.body) = .value v →
      Eval Pr ds fd.body v := by
  intro m ds ρs v fd afd hpf haf hsa hev
  simp only [mixDriver] at h
  split at h <;> try contradiction
  rename_i reqs hdisc
  split at h <;> try contradiction
  rename_i funs hgenf
  cases h
  -- the entry request is still first
  obtain ⟨suf, hsuf⟩ := discover_prefix _ _ _ reqs hdisc
  have hzero : reqs[0]? = some ⟨A.entry, statics⟩ := by rw [hsuf]; simp
  obtain ⟨fd', afd', hfd', hafd', _, hbody⟩ :=
    specOK_all A ⟨funs, 0⟩ reqs stepFuel (by simpa [generate] using hgenf) m 0 _ hzero
  simp only [Program.fn] at hpf
  rw [hfd'] at hpf
  cases hpf
  rw [haf] at hafd'
  cases hafd'
  exact hbody ds ρs v hsa hev

/-! # The converse: the residual computes nothing the source does not

The mirror of everything above, indexed by RESIDUAL fuel where the forward half
was indexed by source fuel.  The asymmetry that makes it more than a
transcription: forward, the source evaluation is a hypothesis, so the source
values are handed to you; backward there is no source evaluation to take apart,
so every source value has to be CONSTRUCTED from the residual one.  That is why
the call cases below produce `∃ vs` where the forward ones consumed a given
`vs`, and why `mix` had to start checking argument counts itself. -/

/-- `mix` produced `r` for source term `t`.  Whatever `r` yields -- a value
outright, or residual code that evaluates -- the source yields the same. -/
def PResSound (A : AProgram) (Pr : Program) (mr : Nat) (ρr ρs : Env)
    (r : PRes) (t : ATerm) : Prop :=
  (∀ w, r = .stat w → Eval (eraseProgram A) ρs (erase t) w) ∧
  (∀ c v, r = .code c → evalFuel mr Pr ρr c = .value v →
            Eval (eraseProgram A) ρs (erase t) v)

theorem PResSound_toCode {A Pr mr ρr ρs r t w}
    (h : PResSound A Pr mr ρr ρs r t)
    (hev : evalFuel mr Pr ρr r.toCode = .value w) :
    Eval (eraseProgram A) ρs (erase t) w := by
  cases r with
  | stat u =>
      -- `toCode` of a static result is `lit u`, so the residual just returns it
      cases mr with
      | zero => simp [evalFuel] at hev
      | succ mq =>
          simp only [PRes.toCode, evalFuel] at hev
          cases hev
          exact h.1 _ rfl
  | code c => exact h.2 c w rfl hev

def SOK (A : AProgram) (Pr : Program) (reqs : List SpecRequest) (n mr : Nat) : Prop :=
  ∀ (Δ : Div) (env : PEnv) (t : ATerm) (r : PRes) (rq : List SpecRequest) (ρr ρs : Env),
    Compat ρr Δ env ρs →
    mixTerm n A (indexOfReq reqs) Δ env t = .ok (r, rq) →
    PResSound A Pr mr ρr ρs r t

/-- The dual of `SpecOK`. -/
def SpecSound (A : AProgram) (Pr : Program) (reqs : List SpecRequest) (mr : Nat) : Prop :=
  ∀ (i : Nat) (req : SpecRequest), reqs[i]? = some req →
    ∃ fd afd, Pr.funs[i]? = some fd ∧ A.fn req.funIdx = some afd ∧
      ∀ (ds ρs : List Val) (v : Val),
        srcArgs afd.params req.staticArgs ds = some ρs →
        evalFuel mr Pr ds fd.body = .value v →
        Eval (eraseProgram A) ρs (erase afd.body) v

/-! ## Operand lists, both ways they are consumed -/

theorem mixTerms_sound_stat {A Pr reqs n mr} (h : SOK A Pr reqs n mr) :
    ∀ (Δ : Div) (env : PEnv) (ts : List ATerm) (rs : List PRes) (rq : List SpecRequest)
      (ρr ρs : Env) (ws : List Val),
      Compat ρr Δ env ρs →
      mixTerms n A (indexOfReq reqs) Δ env ts = .ok (rs, rq) →
      allStatic rs = .ok ws →
      EvalList (eraseProgram A) ρs (eraseList ts) ws := by
  intro Δ env ts
  induction ts with
  | nil =>
      intro rs rq ρr ρs ws _ hmix hst
      simp only [mixTerms] at hmix; cases hmix
      simp only [allStatic] at hst; cases hst
      exact .nil
  | cons t ts ih =>
      intro rs rq ρr ρs ws hc hmix hst
      simp only [mixTerms] at hmix
      split at hmix <;> try contradiction
      rename_i r rq₁ rs' rq₂ ht hts
      cases hmix
      cases r with
      | code _ => simp [allStatic] at hst
      | stat u =>
          simp only [allStatic] at hst
          split at hst <;> try contradiction
          rename_i us hus
          cases hst
          exact .cons ((h Δ env t (.stat u) rq₁ ρr ρs hc ht).1 u rfl)
                      (ih rs' rq₂ ρr ρs us hc hts hus)

theorem mixTerms_sound_code {A Pr reqs n mr} (h : SOK A Pr reqs n mr) :
    ∀ (Δ : Div) (env : PEnv) (ts : List ATerm) (rs : List PRes) (rq : List SpecRequest)
      (ρr ρs : Env) (ds : List Val),
      Compat ρr Δ env ρs →
      mixTerms n A (indexOfReq reqs) Δ env ts = .ok (rs, rq) →
      evalFuelList mr Pr ρr (rs.map PRes.toCode) = .inl ds →
      EvalList (eraseProgram A) ρs (eraseList ts) ds := by
  intro Δ env ts
  induction ts with
  | nil =>
      intro rs rq ρr ρs ds _ hmix hev
      simp only [mixTerms] at hmix; cases hmix
      simp only [List.map_nil, evalFuelList] at hev; cases hev
      exact .nil
  | cons t ts ih =>
      intro rs rq ρr ρs ds hc hmix hev
      simp only [mixTerms] at hmix
      split at hmix <;> try contradiction
      rename_i r rq₁ rs' rq₂ ht hts
      cases hmix
      simp only [List.map_cons, evalFuelList] at hev
      split at hev <;> try contradiction
      rename_i d hd
      split at hev <;> try contradiction
      rename_i ds' hds
      cases hev
      exact .cons (PResSound_toCode (h Δ env t r rq₁ ρr ρs hc ht) hd)
                  (ih rs' rq₂ ρr ρs ds' hc hts hds)

/-! ## Alternatives, backwards -/

theorem mixAlts_sound {A Pr reqs n mr} (h : SOK A Pr reqs n mr) :
    ∀ (Δ : Div) (env : PEnv) (as : List AAlt) (as' : List Alt) (rq : List SpecRequest)
      (ρr ρs : Env) (tag : Nat) (a'' : Alt) (ds : List Val) (v : Val),
      Compat ρr Δ env ρs →
      mixAlts n A (indexOfReq reqs) Δ env as = .ok (as', rq) →
      findAlt as' tag = some a'' →
      a''.arity = ds.length →
      evalFuel mr Pr (ds ++ ρr) a''.body = .value v →
      ∃ af, findAAlt as tag = some af ∧ af.arity = ds.length ∧
            Eval (eraseProgram A) (ds ++ ρs) (erase af.body) v := by
  intro Δ env as
  induction as with
  | nil =>
      intro as' _ _ _ _ _ _ _ _ hmix hfind _ _
      simp only [mixAlts] at hmix; cases hmix
      simp [findAlt] at hfind
  | cons a0 as ih =>
      intro as' rq ρr ρs tag a'' ds v hc hmix hfind har hev
      simp only [mixAlts] at hmix
      split at hmix <;> try contradiction
      rename_i r rq₁ as2 rq₂ hb has
      cases hmix
      simp only [findAlt] at hfind
      split at hfind
      · rename_i htag
        cases hfind
        refine ⟨a0, ?_, by simpa [Alt.arity] using har, ?_⟩
        · simp only [findAAlt]
          split
          · rfl
          · rename_i h2; exact absurd htag h2
        · have hcf := Compat_fields_dyn (Δ := Δ) (env := env) ds hc
          rw [← (show a0.arity = ds.length by simpa [Alt.arity] using har)] at hcf
          exact PResSound_toCode (h _ _ a0.body r rq₁ (ds ++ ρr) (ds ++ ρs) hcf hb)
            (by simpa [Alt.body] using hev)
      · rename_i htag
        obtain ⟨af, hf, ha, he⟩ := ih as2 rq₂ ρr ρs tag a'' ds v hc has hfind har hev
        refine ⟨af, ?_, ha, he⟩
        simp only [findAAlt]
        split
        · rename_i h2; exact absurd h2 htag
        · exact hf

/-! ## A residual call's arguments, backwards

Forward, `splitArgs_spec` took the source values and showed the residual ones
interleave back to them.  Backwards there are no source values yet, so this
CONSTRUCTS them from the residual ones. -/

theorem splitArgs_sound {A Pr reqs n mr} (h : SOK A Pr reqs n mr) :
    ∀ (Δ : Div) (env : PEnv) (ts : List ATerm) (rs : List PRes) (rq : List SpecRequest)
      (ρr ρs : Env) (ps : Div) (svs : List Val) (dts : List Term) (ds : List Val),
      Compat ρr Δ env ρs →
      mixTerms n A (indexOfReq reqs) Δ env ts = .ok (rs, rq) →
      splitArgs ps rs = .ok (svs, dts) →
      evalFuelList mr Pr ρr dts = .inl ds →
      ∃ vs, srcArgs ps svs ds = some vs ∧
            EvalList (eraseProgram A) ρs (eraseList ts) vs ∧ vs.length = ps.length := by
  intro Δ env ts
  induction ts with
  | nil =>
      intro rs rq ρr ρs ps svs dts ds _ hmix hsp hev
      simp only [mixTerms] at hmix; cases hmix
      cases ps with
      | nil =>
          simp only [splitArgs] at hsp; cases hsp
          simp only [evalFuelList] at hev; cases hev
          exact ⟨[], rfl, .nil, rfl⟩
      | cons _ _ => simp [splitArgs] at hsp
  | cons t ts ih =>
      intro rs rq ρr ρs ps svs dts ds hc hmix hsp hev
      simp only [mixTerms] at hmix
      split at hmix <;> try contradiction
      rename_i r rq₁ rs' rq₂ ht hts
      cases hmix
      have hr := h Δ env t r rq₁ ρr ρs hc ht
      cases ps with
      | nil => simp [splitArgs] at hsp
      | cons b bs =>
        cases b with
        | stat =>
            cases r with
            | code _ => simp [splitArgs] at hsp
            | stat w =>
                simp only [splitArgs] at hsp
                split at hsp <;> try contradiction
                rename_i svs' dts' hrec
                cases hsp
                obtain ⟨vs, hsrc, hel, hlen⟩ :=
                  ih rs' rq₂ ρr ρs bs svs' _ ds hc hts hrec hev
                exact ⟨w :: vs, by simp [srcArgs, hsrc],
                       .cons (hr.1 w rfl) hel, by simp [hlen]⟩
        | dyn =>
            simp only [splitArgs] at hsp
            split at hsp <;> try contradiction
            rename_i svs' dts' hrec
            cases hsp
            simp only [evalFuelList] at hev
            split at hev <;> try contradiction
            rename_i d hd
            split at hev <;> try contradiction
            rename_i ds' hds
            cases hev
            obtain ⟨vs, hsrc, hel, hlen⟩ :=
              ih rs' rq₂ ρr ρs bs _ _ ds' hc hts hrec hds
            exact ⟨d :: vs, by simp [srcArgs, hsrc],
                   .cons (PResSound_toCode hr hd) hel, by simp [hlen]⟩

/-! ## An unfolded call's arguments, backwards

`mr` is quantified INSIDE the induction on `ps`, because peeling one residual
`let` costs a unit of residual fuel -- so the recursive use is at a smaller
fuel, and a statement with `mr` fixed outside could not make it. -/

theorem mixUArgs_sound {A Pr reqs n} :
    ∀ (ps : Div) (mr : Nat), (∀ mr', mr' ≤ mr → SOK A Pr reqs n mr') →
    ∀ (ts : List ATerm) (Δ : Div) (env : PEnv) (rs : List PRes)
      (dts : List Term) (rq : List SpecRequest) (ρr ρs : Env) (env' : PEnv)
      (body : Term) (v : Val),
      Compat ρr Δ env ρs →
      mixUArgs n A (indexOfReq reqs) Δ env ps ts = .ok (rs, dts, rq) →
      inlineEnv ps rs = .ok env' →
      evalFuel mr Pr ρr (wrapLets dts body) = .value v →
      ∃ (vs ws : List Val) (mr' : Nat),
        ws.length = dynCount ps ∧ mr' ≤ mr ∧
        Compat (ws.reverse ++ ρr) ps env' vs ∧
        EvalList (eraseProgram A) ρs (eraseList ts) vs ∧
        vs.length = ps.length ∧
        evalFuel mr' Pr (ws.reverse ++ ρr) body = .value v := by
  intro ps
  induction ps with
  | nil =>
      intro mr _ ts Δ env rs dts rq ρr ρs env' body v _ hmix hie hev
      cases ts with
      | nil =>
          simp only [mixUArgs] at hmix; cases hmix
          simp only [inlineEnv] at hie; cases hie
          exact ⟨[], [], mr, rfl, Nat.le_refl _, by simpa using Compat.nil, .nil, rfl,
                 by simpa [wrapLets] using hev⟩
      | cons _ _ => simp [mixUArgs] at hmix
  | cons b bs ih =>
      intro mr hsok ts Δ env rs dts rq ρr ρs env' body v hc hmix hie hev
      cases ts with
      | nil => cases b <;> simp [mixUArgs] at hmix
      | cons t ts =>
        cases b with
        | stat =>
            simp only [mixUArgs] at hmix
            split at hmix <;> try contradiction
            rename_i r rq₁ rs' dts' rq₂ ht hrec
            cases hmix
            cases r with
            | code _ => simp [inlineEnv] at hie
            | stat w =>
                simp only [inlineEnv] at hie
                split at hie <;> try contradiction
                rename_i env'' hie'
                cases hie
                have hr := hsok mr (Nat.le_refl _) Δ env t (.stat w) rq₁ ρr ρs hc ht
                obtain ⟨vs, ws, mr', hlen, hle, hcp, hel, hvl, hbody⟩ :=
                  ih mr hsok ts Δ env rs' _ rq₂ ρr ρs env'' body v hc hrec hie' hev
                exact ⟨w :: vs, ws, mr', by simpa [dynCount] using hlen, hle,
                       hcp.stat, .cons (hr.1 w rfl) hel, by simp [hvl], hbody⟩
        | dyn =>
            simp only [mixUArgs] at hmix
            split at hmix <;> try contradiction
            rename_i r rq₁ rs' dts' rq₂ ht hrec
            cases hmix
            simp only [inlineEnv] at hie
            split at hie <;> try contradiction
            rename_i env'' hie'
            cases hie
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
              simp only [wrapLets, evalFuel] at hev
              split at hev <;> try contradiction
              rename_i d hd
              have hr := hsok mq (by omega) Δ env t r rq₁ ρr ρs hc ht
              obtain ⟨vs, ws, mr', hlen, hle, hcp, hel, hvl, hbody⟩ :=
                ih mq (fun mr'' hmr'' => hsok mr'' (by omega)) ts Δ (env.shiftBy 1) rs' dts'
                   rq₂ (d :: ρr) ρs env'' body v (Compat_shift1 d hc) hrec hie' hev
              have heq : (d :: ws).reverse ++ ρr = ws.reverse ++ (d :: ρr) := by simp
              refine ⟨d :: vs, d :: ws, mr', by simp [dynCount, hlen], by omega, ?_,
                      .cons (PResSound_toCode hr hd) hel, by simp [hvl], ?_⟩
              · rw [heq]
                refine .dyn ?_ hcp
                have hl : ws.reverse.length = dynCount bs := by simp [hlen]
                rw [List.getElem?_append_right (by omega), hl]
                simp
              · rw [heq]; exact hbody

theorem allStatic_length : ∀ (rs : List PRes) (ws : List Val),
    allStatic rs = .ok ws → ws.length = rs.length
  | [],            ws, h => by simp only [allStatic] at h; cases h; rfl
  | .stat _ :: rs, ws, h => by
      simp only [allStatic] at h
      split at h <;> try contradiction
      rename_i us hus
      cases h
      simp [allStatic_length rs us hus]
  | .code _ :: _,  _,  h => by simp [allStatic] at h

/-! ## The converse main induction -/

theorem mixTerm_sound (A : AProgram) (Pr : Program) (reqs : List SpecRequest) :
    ∀ (n mr : Nat), (∀ k, k < mr → SpecSound A Pr reqs k) → SOK A Pr reqs n mr := by
  intro n
  induction n with
  | zero => intro mr _ Δ env t r rq ρr ρs _ hmix; simp [mixTerm] at hmix
  | succ n ih =>
      intro mr hspec Δ env t r rq ρr ρs hc hmix
      have ihle : ∀ mr', mr' ≤ mr → SOK A Pr reqs n mr' :=
        fun mr' hle => ih mr' (fun k hk => hspec k (by omega))
      have ihm : SOK A Pr reqs n mr := ihle mr (Nat.le_refl _)
      cases t with

      | lit w =>
          simp only [mixTerm] at hmix; cases hmix
          exact ⟨(fun _ hw => by cases hw; exact .lit), (fun _ _ hcd => by cases hcd)⟩

      | var i =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          · rename_i w _ henv
            cases hmix
            exact ⟨(fun _ hw => by cases hw; exact .var (Compat_stat_lookup hc i w henv)),
                   (fun _ _ hcd => by cases hcd)⟩
          · rename_i k _ henv
            cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c v hcd hev => ?_)⟩
            cases hcd
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
                simp only [evalFuel] at hev
                split at hev <;> try contradiction
                rename_i u hu
                cases hev
                obtain ⟨u', hs, hr⟩ := Compat_dyn_lookup hc i k henv
                rw [hu] at hr
                cases Option.some.inj hr
                exact .var hs

      | lift e =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i w rq' he
          cases hmix
          refine ⟨(fun _ hw => by cases hw), (fun c v hcd hev => ?_)⟩
          cases hcd
          cases mr with
          | zero => simp [evalFuel] at hev
          | succ mq =>
              simp only [evalFuel] at hev
              cases hev
              exact (ihm Δ env e (.stat w) _ ρr ρs hc he).1 w rfl

      | letIn _ e body =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i re rq₁ he
          have hre := ihm Δ env e re rq₁ ρr ρs hc he
          split at hmix <;> try contradiction
          · -- static binding
            rename_i w _
            split at hmix <;> try contradiction
            rename_i rb rq₂ hbody
            cases hmix
            have hb := ihm _ _ body r _ ρr (w :: ρs) (hc.stat) hbody
            refine ⟨(fun u hw => ?_), (fun c v hcd hev => ?_)⟩
            · exact .letIn (hre.1 w rfl) (hb.1 u hw)
            · exact .letIn (hre.1 w rfl) (hb.2 c v hcd hev)
          · -- dynamic binding
            rename_i _
            split at hmix <;> try contradiction
            rename_i b' rq₂ hbody
            cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c v hcd hev => ?_)⟩
            cases hcd
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
                simp only [evalFuel] at hev
                split at hev <;> try contradiction
                rename_i d hd
                have hcb : Compat (d :: ρr) (.dyn :: Δ) (.dyn 0 :: env.shiftBy 1) (d :: ρs) :=
                  .dyn (by simp) (Compat_shift1 d hc)
                have hb := ihle mq (by omega) _ _ body (.code b') rq₂ (d :: ρr) (d :: ρs) hcb hbody
                exact .letIn (PResSound_toCode (ihle mq (by omega) Δ env e _ rq₁ ρr ρs hc he) hd)
                             (hb.2 b' v rfl hev)

      | prim b p ts =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i rs rq' hts
          split at hmix
          · split at hmix <;> try contradiction
            rename_i ws hws
            split at hmix <;> try contradiction
            rename_i w hp
            cases hmix
            exact ⟨(fun _ hw => by cases hw
                                   exact .prim (mixTerms_sound_stat ihm Δ env ts rs _ ρr ρs ws hc hts hws) hp),
                   (fun _ _ hcd => by cases hcd)⟩
          · cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c v hcd hev => ?_)⟩
            cases hcd
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
                simp only [evalFuel] at hev
                split at hev <;> try contradiction
                rename_i ds hds
                split at hev <;> try contradiction
                rename_i w hp
                cases hev
                exact .prim (mixTerms_sound_code (ihle mq (by omega)) Δ env ts rs _ ρr ρs ds hc hts hds) hp

      | ctorT b k ts =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i rs rq' hts
          split at hmix
          · split at hmix <;> try contradiction
            rename_i ws hws
            cases hmix
            exact ⟨(fun _ hw => by cases hw
                                   exact .ctorT (mixTerms_sound_stat ihm Δ env ts rs _ ρr ρs ws hc hts hws)),
                   (fun _ _ hcd => by cases hcd)⟩
          · cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c v hcd hev => ?_)⟩
            cases hcd
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
                simp only [evalFuel] at hev
                split at hev <;> try contradiction
                rename_i ds hds
                cases hev
                exact .ctorT (mixTerms_sound_code (ihle mq (by omega)) Δ env ts rs _ ρr ρs ds hc hts hds)

      | ite _ c a e =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i rc rq₁ hcm
          have hrc := ihm Δ env c rc rq₁ ρr ρs hc hcm
          split at hmix <;> try contradiction
          · rename_i _
            split at hmix <;> try contradiction
            rename_i ra rq₂ ha
            cases hmix
            have hb := ihm Δ env a r _ ρr ρs hc ha
            exact ⟨(fun u hw => .iteT (hrc.1 _ rfl) (hb.1 u hw)),
                   (fun c' v hcd hev => .iteT (hrc.1 _ rfl) (hb.2 c' v hcd hev))⟩
          · rename_i _
            split at hmix <;> try contradiction
            rename_i re' rq₂ he
            cases hmix
            have hb := ihm Δ env e r _ ρr ρs hc he
            exact ⟨(fun u hw => .iteF (hrc.1 _ rfl) (hb.1 u hw)),
                   (fun c' v hcd hev => .iteF (hrc.1 _ rfl) (hb.2 c' v hcd hev))⟩
          · rename_i _
            split at hmix <;> try contradiction
            rename_i ra rq₂ re' rq₃ ha he
            cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c' v hcd hev => ?_)⟩
            cases hcd
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
                simp only [evalFuel] at hev
                split at hev <;> try contradiction
                · rename_i hcv
                  exact .iteT (PResSound_toCode (ihle mq (by omega) Δ env c _ rq₁ ρr ρs hc hcm) hcv)
                    (PResSound_toCode (ihle mq (by omega) Δ env a ra rq₂ ρr ρs hc ha) hev)
                · rename_i hcv
                  exact .iteF (PResSound_toCode (ihle mq (by omega) Δ env c _ rq₁ ρr ρs hc hcm) hcv)
                    (PResSound_toCode (ihle mq (by omega) Δ env e re' rq₃ ρr ρs hc he) hev)

      | caseT _ s alts =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i rsc rq₁ hsm
          split at hmix <;> try contradiction
          · rename_i tag vs _
            split at hmix <;> try contradiction
            rename_i af hfaf
            split at hmix <;> try contradiction
            rename_i har
            split at hmix <;> try contradiction
            rename_i rb rq₂ hbody
            cases hmix
            have hs := ihm Δ env s (.stat (.ctor tag vs)) rq₁ ρr ρs hc hsm
            have hcf := Compat_fields_stat (Δ := Δ) (env := env) vs hc
            rw [← har] at hcf
            have hb := ihm _ _ af.body r _ ρr (vs ++ ρs) hcf hbody
            have hfa := findAlt_eraseAlts alts tag af hfaf
            exact ⟨(fun u hw => .caseT (hs.1 _ rfl) hfa (by simpa [Alt.arity] using har)
                                  (by simpa [Alt.body] using hb.1 u hw)),
                   (fun c' v hcd hev => .caseT (hs.1 _ rfl) hfa (by simpa [Alt.arity] using har)
                                  (by simpa [Alt.body] using hb.2 c' v hcd hev))⟩
          · rename_i _
            split at hmix <;> try contradiction
            rename_i alts' rq₂ halts
            cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c' v hcd hev => ?_)⟩
            cases hcd
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
                simp only [evalFuel] at hev
                split at hev <;> try contradiction
                rename_i tag ds hsv
                split at hev <;> try contradiction
                rename_i a'' hfa''
                split at hev <;> try contradiction
                rename_i har''
                obtain ⟨af, hfaf, hara, hbe⟩ :=
                  mixAlts_sound (ihle mq (by omega)) Δ env alts alts' rq₂ ρr ρs tag a'' ds v
                    hc halts hfa'' har'' hev
                exact .caseT
                  (PResSound_toCode (ihle mq (by omega) Δ env s _ rq₁ ρr ρs hc hsm) hsv)
                  (findAlt_eraseAlts alts tag af hfaf)
                  (by simpa [Alt.arity] using hara)
                  (by simpa [Alt.body] using hbe)

      | call b f ts =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i rs rq₁ hts
          split at hmix <;> try contradiction
          rename_i fd hfn
          split at hmix
          · split at hmix <;> try contradiction
            rename_i hasd
            split at hmix <;> try contradiction
            rename_i hlen
            split at hmix <;> try contradiction
            rename_i ws hws
            split at hmix <;> try contradiction
            rename_i rb rq₂ hbody
            cases hmix
            have hwl : fd.params.length = ws.length := by
              rw [hlen, allStatic_length rs ws hws]
            have hb := ihm _ _ fd.body r _ ρr ws (Compat_allStat ρr fd.params ws hasd hwl) hbody
            have hel := mixTerms_sound_stat ihm Δ env ts rs _ ρr ρs ws hc hts hws
            exact ⟨(fun u hw => .call hel (eraseProgram_fn hfn)
                        (by simpa [eraseFunDef] using hwl) (by simpa [eraseFunDef] using hb.1 u hw)),
                   (fun c' v hcd hev => .call hel (eraseProgram_fn hfn)
                        (by simpa [eraseFunDef] using hwl)
                        (by simpa [eraseFunDef] using hb.2 c' v hcd hev))⟩
          · split at hmix <;> try contradiction
            rename_i svs dts hsplit
            split at hmix <;> try contradiction
            rename_i k hidx
            cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c' v hcd hev => ?_)⟩
            cases hcd
            cases mr with
            | zero => simp [evalFuel] at hev
            | succ mq =>
                simp only [evalFuel] at hev
                split at hev <;> try contradiction
                rename_i ds hds
                split at hev <;> try contradiction
                rename_i fdr hfr
                split at hev <;> try contradiction
                rename_i harr
                obtain ⟨vs, hsrc, hel, hvl⟩ :=
                  splitArgs_sound (ihle mq (by omega)) Δ env ts rs _ ρr ρs fd.params svs dts ds
                    hc hts hsplit hds
                obtain ⟨fdr', afd, hfr', hafd, hcall⟩ :=
                  hspec mq (by omega) k ⟨f, svs⟩ (indexOfReq_spec hidx)
                rw [hfn] at hafd; cases hafd
                simp only [Program.fn] at hfr
                rw [hfr'] at hfr; cases hfr
                exact .call hel (eraseProgram_fn hfn) (by simpa [eraseFunDef] using hvl.symm)
                  (hcall ds vs v hsrc hev)

      | ucall b f ts =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i fd hfn
          split at hmix
          · split at hmix <;> try contradiction
            rename_i rs rq₁ hts
            split at hmix <;> try contradiction
            rename_i hasd
            split at hmix <;> try contradiction
            rename_i hlen
            split at hmix <;> try contradiction
            rename_i ws hws
            split at hmix <;> try contradiction
            rename_i rb rq₂ hbody
            cases hmix
            have hwl : fd.params.length = ws.length := by
              rw [hlen, allStatic_length rs ws hws]
            have hb := ihm _ _ fd.body r _ ρr ws (Compat_allStat ρr fd.params ws hasd hwl) hbody
            have hel := mixTerms_sound_stat ihm Δ env ts rs _ ρr ρs ws hc hts hws
            exact ⟨(fun u hw => .call hel (eraseProgram_fn hfn)
                        (by simpa [eraseFunDef] using hwl) (by simpa [eraseFunDef] using hb.1 u hw)),
                   (fun c' v hcd hev => .call hel (eraseProgram_fn hfn)
                        (by simpa [eraseFunDef] using hwl)
                        (by simpa [eraseFunDef] using hb.2 c' v hcd hev))⟩
          · split at hmix <;> try contradiction
            rename_i rs' dts rq₂ hua
            split at hmix <;> try contradiction
            rename_i env' hie
            split at hmix <;> try contradiction
            rename_i b' rq₃ hbody
            cases hmix
            refine ⟨(fun _ hw => by cases hw), (fun c' v hcd hev => ?_)⟩
            cases hcd
            obtain ⟨vs, ws, mr', hwlen, hle, hcp, hel, hvl, hbev⟩ :=
              mixUArgs_sound fd.params mr ihle ts Δ env rs' dts rq₂ ρr ρs env' b' v
                hc hua hie hev
            have hb := (ihle mr' hle) _ _ fd.body (.code b') rq₃ (ws.reverse ++ ρr) vs hcp hbody
            exact .call hel (eraseProgram_fn hfn) (by simpa [eraseFunDef] using hvl.symm)
              (by simpa [eraseFunDef] using hb.2 b' v rfl hbev)

/-! ## Tying the converse knot -/

theorem specSound_all (A : AProgram) (Pr : Program) (reqs : List SpecRequest) (stepFuel : Nat)
    (hgen : generateFrom stepFuel A (indexOfReq reqs) reqs = .ok Pr.funs) :
    ∀ mr, SpecSound A Pr reqs mr := by
  intro mr
  induction mr using Nat.strongRecOn with
  | _ mr IH =>
    intro i req hreq
    obtain ⟨fd, rq, hmf, hfuns⟩ := generateFrom_spec reqs Pr.funs hgen i req hreq
    obtain ⟨afd, env, r, hfn, hbe, hmt, rfl⟩ := mixFun_spec hmf
    refine ⟨⟨dynCount afd.params, r.toCode⟩, afd, hfuns, hfn, ?_⟩
    intro ds ρs v hsrcargs hev
    have hc : Compat ds afd.params env ρs := by
      have := buildEnv_Compat afd.params req.staticArgs [] ds env ρs (by simpa using hbe) hsrcargs
      simpa using this
    exact PResSound_toCode
      (mixTerm_sound A Pr reqs stepFuel mr IH afd.params env afd.body r rq ds ρs hc hmt) hev

/-- The converse at the driver: the residual entry computes nothing the source
does not. -/
theorem mixDriver_complete {stepFuel wlFuel : Nat} {A : AProgram} {statics : List Val}
    {Pr : Program} (h : mixDriver stepFuel wlFuel A statics = .ok Pr) :
    ∀ (mr : Nat) (ds ρs : List Val) (v : Val) (fd : FunDef) (afd : AFunDef),
      Pr.fn Pr.entry = some fd →
      A.fn A.entry = some afd →
      srcArgs afd.params statics ds = some ρs →
      evalFuel mr Pr ds fd.body = .value v →
      Eval (eraseProgram A) ρs (erase afd.body) v := by
  intro mr ds ρs v fd afd hpf haf hsa hev
  simp only [mixDriver] at h
  split at h <;> try contradiction
  rename_i reqs hdisc
  split at h <;> try contradiction
  rename_i funs hgenf
  cases h
  obtain ⟨suf, hsuf⟩ := discover_prefix _ _ _ reqs hdisc
  have hzero : reqs[0]? = some ⟨A.entry, statics⟩ := by rw [hsuf]; simp
  obtain ⟨fd', afd', hfd', hafd', hbody⟩ :=
    specSound_all A ⟨funs, 0⟩ reqs stepFuel (by simpa [generate] using hgenf) mr 0 _ hzero
  simp only [Program.fn] at hpf
  rw [hfd'] at hpf
  cases hpf
  rw [haf] at hafd'
  cases hafd'
  exact hbody ds ρs v hsa hev

/-! ## `mix_sound`, both directions

The residual entry and the source program compute the same thing.  Stated with
fuel on the hypothesis side, because that is where the two proofs are indexed:
forward by source fuel, backward by residual fuel. -/
theorem mixDriver_correct {stepFuel wlFuel : Nat} {A : AProgram} {statics : List Val}
    {Pr : Program} (h : mixDriver stepFuel wlFuel A statics = .ok Pr)
    (ds ρs : List Val) (v : Val) (fd : FunDef) (afd : AFunDef)
    (hpf : Pr.fn Pr.entry = some fd) (haf : A.fn A.entry = some afd)
    (hsa : srcArgs afd.params statics ds = some ρs) :
    (∀ m,  evalFuel m (eraseProgram A) ρs (erase afd.body) = .value v → Eval Pr ds fd.body v) ∧
    (∀ mr, evalFuel mr Pr ds fd.body = .value v →
             Eval (eraseProgram A) ρs (erase afd.body) v) :=
  ⟨fun m  => mixDriver_sound    h m  ds ρs v fd afd hpf haf hsa,
   fun mr => mixDriver_complete h mr ds ρs v fd afd hpf haf hsa⟩

end Projection
