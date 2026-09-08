/-
  `mix_sound`: the residual program computes what the source computes.

  ############################################################################
  STATUS.  ONE DIRECTION IS PROVED.  `mixDriver_sound` says: whatever the source
  program computes, the residual entry computes on the dynamic arguments alone.
  That is PRESERVATION, and it is what makes the projections mean something.

  TWO THINGS ARE STILL MISSING, both deliberate and both stated here rather than
  left to be discovered:

  * The CONVERSE -- that the residual computes nothing the source does not.  The
    plan states `mix_sound` as an iff; this is the forward half.  The mirror
    proof is indexed by RESIDUAL fuel where this one is indexed by source fuel.

  * The source side is stated with `evalFuel m`, not with `Eval`.  Upgrading
    needs `Eval P p t v → ∃ m, evalFuel m P p t = .value v`, the converse of
    `evalFuel_sound`, which is not proved here.

  Gate0's `#guard`s remain the only evidence for the PROJECTIONS themselves --
  that `mixProgram` computes what this specializer computes, and that the derived
  compiler's output matches.  Those are separate theorems (`mixProgram_implements_
  mixHost`, `secondProjection_correct`) and neither is written.
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

end Projection
