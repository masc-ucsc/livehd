/-
  `mix_sound`: the residual program computes what the source computes.

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
      ∀ (ds ρs : List Val) (v : Val),
        srcArgs afd.params req.staticArgs ds = some ρs →
        evalFuel m (eraseProgram A) ρs (erase afd.body) = .value v →
        Eval Pr ds fd.body v

end Projection
