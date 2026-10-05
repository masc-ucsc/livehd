/-
  `mix_sound`: the residual program computes what the source computes.

  ############################################################################
  STATUS.  `mix_sound` IS PROVED, in the form the plan states it:

    mixDriver_iff :  Eval Pr ds fd.body v  ↔  Eval ⟦A⟧ ρs afd.body v

  an iff between two denotations, no fuel anywhere.  The two halves are indexed
  by different fuels -- forward by SOURCE fuel (`mixDriver_sound`), backward by
  RESIDUAL fuel (`mixDriver_complete`) -- and `evalFuel_complete` is what turns
  "terminates at some fuel" back into `Eval` on both sides.

  PROJECTION STATUS.  `secondProjection_correct` is now proved: the derived
  compiler agrees with the object `mixProgram` on every design value.  What is
  still only checked is that `mixProgram` computes what this Lean specializer
  computes (`mixProgram_implements_mixHost`).  Consequently Gate0's concrete
  equality with the host-produced residual is still a `#guard` rather than the
  consequence of an end-to-end self-application theorem.
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

-- `PVal.shift_zero` MOVED to `PartialEvaluator.lean` -- the `@[csimp]` fast
-- path needs it there.  Still `@[simp]`, still in this namespace.

theorem PVal.shift_succ : ∀ (n : Nat) (v : PVal),
    PVal.shift 1 (PVal.shift n v) = PVal.shift (n + 1) v
  | _, .stat _   => rfl
  | n, .dyn _    => by simp [PVal.shift]; omega
  | n, .cons a b => by simp [PVal.shift, PVal.shift_succ n a, PVal.shift_succ n b]

-- `PEnv.shiftBy_zero` MOVED to `PartialEvaluator.lean` for the same reason.

theorem PEnv.shiftBy_succ : ∀ (n : Nat) (env : PEnv),
    PEnv.shiftBy 1 (PEnv.shiftBy n env) = PEnv.shiftBy (n + 1) env
  | _, []        => rfl
  | n, v :: rest => by
      simp only [PEnv.shiftBy, PEnv.shiftBy_succ n rest, PVal.shift_succ n v]

theorem PEnv.shiftBy_append : ∀ (k : Nat) (a b : PEnv),
    PEnv.shiftBy k (a ++ b) = PEnv.shiftBy k a ++ PEnv.shiftBy k b
  | _, [],        _ => rfl
  | k, _ :: rest, b => by simp [PEnv.shiftBy, PEnv.shiftBy_append k rest b]

theorem PEnv.shiftBy_map_dyn : ∀ (l : List Nat) (k : Nat),
    PEnv.shiftBy k (l.map PVal.dyn) = l.map (fun i => PVal.dyn (i + k))
  | [],      _ => rfl
  | i :: is, k => by simp [PEnv.shiftBy, PVal.shift, PEnv.shiftBy_map_dyn is k]

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

/-- What a partial environment entry DENOTES, against the residual environment.

`Compat`'s dynamic case is stated through this rather than committing to a bare
`dyn` index, so that a PRESERVED SPINE has a meaning in the invariant: its
leaves name residual bindings, and the whole denotes the cons value built from
what they name.  Without this a spine is not merely unproved, it is
unstatable — and the totality guard on `hd`/`tl` would have nothing to discharge
against.

This GENERALIZES the old `dyn` constructor rather than adding a fourth one:
`PValOK ρr (.dyn k) v` unfolds to exactly the old `ρr[k]? = some v`, so every
induction over `Compat` still has one dynamic case. -/
def PValOK (ρr : Env) : PVal → Val → Prop
  | .stat w,   v => w = v
  | .dyn k,    v => ρr[k]? = some v
  | .cons a b, v => ∃ x y, v = .cons x y ∧ PValOK ρr a x ∧ PValOK ρr b y

theorem PValOK_shift1 {ρr pv v} (w : Val) : PValOK ρr pv v → PValOK (w :: ρr) (PVal.shift 1 pv) v := by
  induction pv generalizing v with
  | stat _   => intro h; exact h
  | dyn _    => intro h; simpa [PValOK, PVal.shift] using h
  | cons a b iha ihb =>
      intro h
      obtain ⟨x, y, hv, ha, hb⟩ := h
      exact ⟨x, y, hv, iha ha, ihb hb⟩

inductive Compat (ρr : Env) : Div → PEnv → Env → Prop where
  | nil  : Compat ρr [] [] []
  | stat : Compat ρr Δ env ρs → Compat ρr (.stat :: Δ) (.stat v :: env) (v :: ρs)
  | dyn  : PValOK ρr pv v → Compat ρr Δ env ρs →
           Compat ρr (.dyn :: Δ) (pv :: env) (v :: ρs)

/-- Entering ONE residual binder.  Every residual index in scope moves out by
one, which is exactly what `PEnv.shiftBy 1` does to the partial environment --
so the two shifts cancel and the description stays honest. -/
theorem Compat_shift1 {ρr Δ env ρs} (w : Val) (h : Compat ρr Δ env ρs) :
    Compat (w :: ρr) Δ (env.shiftBy 1) ρs := by
  induction h with
  | nil => exact .nil
  | stat _ ih => exact .stat ih
  | dyn hk _ ih =>
      exact .dyn (PValOK_shift1 w hk) ih

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
      have h1 := Compat.dyn (ρr := v :: (vs ++ ρr)) (pv := .dyn 0) (v := v)
        (by simp [PValOK]) (Compat_shift1 v (Compat_fields_dyn vs h))
      rw [PEnv.shiftBy_append, PEnv.shiftBy_succ] at h1
      simpa [freshDyns_succ, List.replicate_succ] using h1

/-- A `caseT` alternative with a STATIC scrutinee: `mix` knows every field, so
no residual binder is created and `ρr` does not move. -/
theorem Compat_fields_stat {ρr Δ env ρs} : ∀ (vs : List Val), Compat ρr Δ env ρs →
    Compat ρr (List.replicate vs.length .stat ++ Δ) (vs.map PVal.stat ++ env) (vs ++ ρs)
  | [],      h => h
  | v :: vs, h => by
      simpa [List.replicate_succ] using Compat.stat (Compat_fields_stat vs h)

/-! ### Scope

The second half of what makes a component discardable.  `PRes.total` says a
discarded subtree holds no computation; this says its references actually denote
residual values.  **Neither implies the other** — a well-scoped `.code <loop>`
is in scope and must still not be dropped, and a `total` spine of variables
still needs its indices in range.

Binder-aware throughout: `letIn` and each `caseT` alternative move the depth, and
a package's bindings each run one binder deeper than the last. -/

mutual

def Term.Scoped (d : Nat) : Term → Prop
  | .lit _      => True
  | .var i      => i < d
  | .letIn e b  => Term.Scoped d e ∧ Term.Scoped (d + 1) b
  | .ite c a b  => Term.Scoped d c ∧ Term.Scoped d a ∧ Term.Scoped d b
  | .prim _ ts  => Term.ScopedList d ts
  | .ctorT _ ts => Term.ScopedList d ts
  | .caseT s as => Term.Scoped d s ∧ Term.ScopedAlts d as
  | .call _ ts  => Term.ScopedList d ts

def Term.ScopedList (d : Nat) : List Term → Prop
  | []      => True
  | t :: ts => Term.Scoped d t ∧ Term.ScopedList d ts

def Term.ScopedAlts (d : Nat) : List Alt → Prop
  | []      => True
  | a :: as => Term.Scoped (d + a.2.1) a.2.2 ∧ Term.ScopedAlts d as

end

def PVal.Scoped (depth : Nat) : PVal → Prop
  | .stat _   => True
  | .dyn k    => k < depth
  | .cons a b => PVal.Scoped depth a ∧ PVal.Scoped depth b

/-- Each binding runs under the ones before it, so the depth grows as the list
is walked.  This is the piece a flat `∀ t ∈ bs` would get wrong. -/
def ScopedLets : Nat → List Term → Prop
  | _, []      => True
  | d, t :: ts => Term.Scoped d t ∧ ScopedLets (d + 1) ts

def PRes.Scoped (depth : Nat) : PRes → Prop
  | .stat _    => True
  | .code t    => Term.Scoped depth t
  | .cons a b  => PRes.Scoped depth a ∧ PRes.Scoped depth b
  | .lets bs r => ScopedLets depth bs ∧ PRes.Scoped (depth + bs.length) r

def PEnv.Scoped (depth : Nat) : PEnv → Prop
  | []      => True
  | v :: vs => PVal.Scoped depth v ∧ PEnv.Scoped depth vs

/-! #### Scope helpers

The three facts everything downstream needs: shifting moves the bound, reifying
a scoped result gives a scoped term, and wrapping bindings closes a body that was
scoped under them. -/

theorem PVal.Scoped_shift : ∀ {pv : PVal} {d : Nat} (k : Nat),
    PVal.Scoped d pv → PVal.Scoped (d + k) (PVal.shift k pv)
  | .stat _,   _, _, _ => trivial
  | .dyn _,    _, k, h => by simp only [PVal.Scoped, PVal.shift] at *; omega
  | .cons a b, _, k, h => ⟨PVal.Scoped_shift k h.1, PVal.Scoped_shift k h.2⟩

theorem Term.Scoped_wrapLets : ∀ (bs : List Term) (d : Nat) (body : Term),
    ScopedLets d bs → Term.Scoped (d + bs.length) body → Term.Scoped d (wrapLets bs body)
  | [],      d, body, _,  hb => by simpa [wrapLets] using hb
  | t :: ts, d, body, hs, hb => by
      refine ⟨hs.1, Term.Scoped_wrapLets ts (d + 1) body hs.2 ?_⟩
      have : d + 1 + ts.length = d + (t :: ts).length := by simp; omega
      rw [this]; exact hb

theorem PRes.Scoped_toCode : ∀ {r : PRes} {d : Nat},
    PRes.Scoped d r → Term.Scoped d r.toCode
  | .stat _,    _, _ => trivial
  | .code _,    _, h => h
  | .cons _ _,  _, h => ⟨PRes.Scoped_toCode h.1, PRes.Scoped_toCode h.2, trivial⟩
  | .lets bs r, d, h =>
      Term.Scoped_wrapLets bs d (PRes.toCode r) h.1 (PRes.Scoped_toCode h.2)

/-! #### Scope, structurally

The small facts the fuel induction uses repeatedly.  Proved first and built
first, so that binder arithmetic fails here rather than inside the induction. -/

theorem PEnv.Scoped_lookup {d : Nat} : ∀ {env : PEnv} {i : Nat} {pv : PVal},
    PEnv.Scoped d env → env[i]? = some pv → PVal.Scoped d pv
  | [],      _, _,  _, hv => by simp at hv
  | _ :: _,  i, _,  h, hv => by
      cases i with
      | zero   => simp only [List.getElem?_cons_zero, Option.some.injEq] at hv
                  subst hv; exact h.1
      | succ k => exact PEnv.Scoped_lookup h.2 (by simpa using hv)

theorem PEnv.Scoped_shift {d : Nat} (k : Nat) : ∀ {env : PEnv},
    PEnv.Scoped d env → PEnv.Scoped (d + k) (PEnv.shiftBy k env)
  | [],     _ => trivial
  | _ :: _, h => ⟨PVal.Scoped_shift k h.1, PEnv.Scoped_shift k h.2⟩

theorem PEnv.Scoped_append {d : Nat} : ∀ {a b : PEnv},
    PEnv.Scoped d a → PEnv.Scoped d b → PEnv.Scoped d (a ++ b)
  | [],     _, _,  hb => hb
  | _ :: _, _, ha, hb => ⟨ha.1, PEnv.Scoped_append ha.2 hb⟩

theorem freshDyns_scoped (d n : Nat) : PEnv.Scoped (d + n) (freshDyns n) := by
  have : ∀ (l : List Nat), (∀ i ∈ l, i < d + n) → PEnv.Scoped (d + n) (l.map PVal.dyn) := by
    intro l
    induction l with
    | nil => intro _; trivial
    | cons a as ih =>
        intro hb
        exact ⟨hb a (by simp), ih (fun i hi => hb i (by simp [hi]))⟩
  exact this (List.range n) (fun i hi => by simp at hi; omega)

def PRes.ScopedList (d : Nat) : List PRes → Prop
  | []      => True
  | r :: rs => PRes.Scoped d r ∧ PRes.ScopedList d rs

theorem PRes.ScopedList_toCode {d : Nat} : ∀ {rs : List PRes},
    PRes.ScopedList d rs → Term.ScopedList d (rs.map PRes.toCode)
  | [],     _ => trivial
  | _ :: _, h => ⟨PRes.Scoped_toCode h.1, PRes.ScopedList_toCode h.2⟩

theorem splitArgs_scoped {d : Nat} : ∀ (ps : Div) (rs : List PRes)
    (svs : List Val) (dts : List Term),
    PRes.ScopedList d rs → splitArgs ps rs = .ok (svs, dts) → Term.ScopedList d dts := by
  intro ps
  induction ps with
  | nil =>
      intro rs svs dts _ hsp
      cases rs with
      | nil      => simp only [splitArgs, Except.ok.injEq, Prod.mk.injEq] at hsp
                    rw [← hsp.2]; trivial
      | cons _ _ => simp [splitArgs] at hsp
  | cons b bs ih =>
      intro rs svs dts h hsp
      cases rs with
      | nil => cases b <;> simp [splitArgs] at hsp
      | cons r rs' =>
          cases b with
          | stat =>
              cases r with
              | stat _ =>
                  simp only [splitArgs] at hsp
                  cases hres : splitArgs bs rs' with
                  | error _ => rw [hres] at hsp; simp at hsp
                  | ok pr =>
                      obtain ⟨vs, ts⟩ := pr
                      rw [hres] at hsp
                      simp only [Except.ok.injEq, Prod.mk.injEq] at hsp
                      rw [← hsp.2]
                      exact ih rs' vs ts h.2 hres
              | code _   => simp [splitArgs] at hsp
              | cons _ _ => simp [splitArgs] at hsp
              | lets _ _ => simp [splitArgs] at hsp
          | dyn =>
              simp only [splitArgs] at hsp
              cases hres : splitArgs bs rs' with
              | error _ => rw [hres] at hsp; simp at hsp
              | ok pr =>
                  obtain ⟨vs, ts⟩ := pr
                  rw [hres] at hsp
                  simp only [Except.ok.injEq, Prod.mk.injEq] at hsp
                  rw [← hsp.2]
                  exact ⟨PRes.Scoped_toCode h.1, ih rs' vs ts h.2 hres⟩

/-- The unwired structural rules preserve scope, which makes the switch's
primitive branch mechanical when it lands.  Each peel either returns a
sub-result at the same depth or re-wraps a package, so the depth bookkeeping is
exactly `PRes.Scoped`'s own. -/
theorem peelHd_scoped : ∀ {r r' : PRes} {d : Nat},
    peelHd r = some r' → PRes.Scoped d r → PRes.Scoped d r' := by
  intro r
  induction r with
  | stat v => intro r' d hp h
              cases v <;> simp only [peelHd] at hp <;> try cases hp
              all_goals trivial
  | code _ => intro r' d hp _; simp [peelHd] at hp
  | cons a b _ _ =>
      intro r' d hp h
      simp only [peelHd] at hp
      split at hp <;> try contradiction
      cases hp; exact h.1
  | lets bs r ih =>
      intro r' d hp h
      simp only [peelHd, Option.map_eq_some_iff] at hp
      obtain ⟨r'', hr'', rfl⟩ := hp
      exact ⟨h.1, ih hr'' h.2⟩

theorem peelTl_scoped : ∀ {r r' : PRes} {d : Nat},
    peelTl r = some r' → PRes.Scoped d r → PRes.Scoped d r' := by
  intro r
  induction r with
  | stat v => intro r' d hp h
              cases v <;> simp only [peelTl] at hp <;> try cases hp
              all_goals trivial
  | code _ => intro r' d hp _; simp [peelTl] at hp
  | cons a b _ _ =>
      intro r' d hp h
      simp only [peelTl] at hp
      split at hp <;> try contradiction
      cases hp; exact h.2
  | lets bs r ih =>
      intro r' d hp h
      simp only [peelTl, Option.map_eq_some_iff] at hp
      obtain ⟨r'', hr'', rfl⟩ := hp
      exact ⟨h.1, ih hr'' h.2⟩

theorem peelIsNil_scoped : ∀ {r r' : PRes} {d : Nat},
    peelIsNil r = some r' → PRes.Scoped d r → PRes.Scoped d r' := by
  intro r
  induction r with
  | stat v => intro r' d hp h
              cases v <;> simp only [peelIsNil] at hp <;> try cases hp
              all_goals trivial
  | code _ => intro r' d hp _; simp [peelIsNil] at hp
  | cons a b _ _ =>
      intro r' d hp h
      simp only [peelIsNil] at hp
      split at hp <;> try contradiction
      cases hp; trivial
  | lets bs r ih =>
      intro r' d hp h
      simp only [peelIsNil, Option.map_eq_some_iff] at hp
      obtain ⟨r'', hr'', rfl⟩ := hp
      exact ⟨h.1, ih hr'' h.2⟩

theorem primStruct_scoped {d : Nat} {p : Prim} {rs : List PRes} {r : PRes}
    (hp : primStruct p rs = some r) (h : PRes.ScopedList d rs) : PRes.Scoped d r := by
  unfold primStruct at hp
  split at hp
  · cases hp; trivial
  · cases hp; exact ⟨h.1, h.2.1⟩
  · exact peelHd_scoped hp h.1
  · exact peelTl_scoped hp h.1
  · exact peelIsNil_scoped hp h.1
  · simp at hp

/-! #### The hot path

`prepare`'s conservative fallback binds a whole subtree and loses its spine.
That is sound but slow, so the shape `I_hw` actually produces must be proved
NOT to reach it -- otherwise a later change could silently restore quadratic
residuals while every correctness theorem stayed green. -/

/-- A pure spine -- values and references only -- prepares to ITSELF, emitting
no bindings.  This is why the environment argument never needs one. -/
theorem prepare_toPRes : ∀ (pv : PVal), prepare pv.toPRes = ⟨[], pv⟩
  | .stat _   => rfl
  | .dyn _    => rfl
  | .cons a b => by
      simp only [PVal.toPRes, prepare, prepare_toPRes a, prepare_toPRes b]
      simp

/-- THE structural property responsible for linear specialization: extending the
environment by one node costs exactly ONE binding, the spine survives, and its
references shift by exactly that one binding.

The fallback is not selected, so the residual grows by a constant per node
rather than by the length of the environment. -/
theorem prepare_hot_path {t : Term} (hnv : ∀ i, t ≠ .var i) (spine : PVal) :
    prepare (.cons (.code t) spine.toPRes)
      = ⟨[t], .cons (.dyn 0) (PVal.shift 1 spine)⟩ := by
  have hb := prepare_toPRes spine
  cases t with
  | var i => exact absurd rfl (hnv i)
  | lit _ | letIn _ _ | ite _ _ _ | prim _ _ | ctorT _ _ | caseT _ _ | call _ _ =>
      simp only [prepare, hb]
      simp

/-! #### Layer 2: the same-fuel companions

`mixTerm` recurses by REDUCING fuel, while `mixTerms`, `mixPArgs` and `mixAlts`
call it at the SAME fuel.  So the architecture is a fuel induction with these
three as companions derived from its hypothesis -- an induction on `ATerm` would
leave them nothing to appeal to.  This mirrors the existing `TOK` organization
with every semantic parameter removed: nothing here mentions `Compat`,
evaluation, or well-annotatedness. -/

def ScopeOK (n : Nat) : Prop :=
  ∀ (A : AProgram) (idx : SpecRequest → Option Nat)
    (Δ : Div) (env : PEnv) (t : ATerm)
    (r : PRes) (rq : List SpecRequest) (d : Nat),
    PEnv.Scoped d env →
    mixTerm n A idx Δ env t = .ok (r, rq) →
    PRes.Scoped d r

/-- Every operand is specialized in the SAME environment, so all of them land at
the same depth. -/
theorem mixTerms_scoped {n : Nat} (h : ScopeOK n) :
    ∀ A idx Δ env ts rs rq d,
      PEnv.Scoped d env →
      mixTerms n A idx Δ env ts = .ok (rs, rq) →
      PRes.ScopedList d rs := by
  intro A idx Δ env ts
  induction ts with
  | nil =>
      intro rs rq d _ hm
      simp only [mixTerms] at hm
      cases hm
      trivial
  | cons t ts ih =>
      intro rs rq d henv hm
      simp only [mixTerms] at hm
      cases ht : mixTerm n A idx Δ env t with
      | error e => rw [ht] at hm; simp at hm
      | ok x =>
          obtain ⟨r, rq₁⟩ := x
          rw [ht] at hm
          cases hts : mixTerms n A idx Δ env ts with
          | error e => rw [hts] at hm; simp at hm
          | ok y =>
              obtain ⟨rs', rq₂⟩ := y
              rw [hts] at hm
              cases hm
              exact ⟨h A idx Δ env t r rq₁ d henv ht, ih rs' rq₂ d henv hts⟩

/-- Each alternative's body is scoped under its own fields, which is why the
result is `Term.ScopedAlts` rather than a flat list property. -/
theorem mixAlts_scoped {n : Nat} (h : ScopeOK n) :
    ∀ A idx Δ env alts alts' rq d,
      PEnv.Scoped d env →
      mixAlts n A idx Δ env alts = .ok (alts', rq) →
      Term.ScopedAlts d alts' := by
  intro A idx Δ env alts
  induction alts with
  | nil =>
      intro alts' rq d _ hm
      simp only [mixAlts] at hm
      cases hm
      trivial
  | cons a as ih =>
      intro alts' rq d henv hm
      simp only [mixAlts] at hm
      cases ht : mixTerm n A idx (List.replicate a.arity .dyn ++ Δ)
                   (freshDyns a.arity ++ PEnv.shiftBy a.arity env) a.body with
      | error e => rw [ht] at hm; simp at hm
      | ok x =>
          obtain ⟨r, rq₁⟩ := x
          rw [ht] at hm
          cases has : mixAlts n A idx Δ env as with
          | error e => rw [has] at hm; simp at hm
          | ok y =>
              obtain ⟨as', rq₂⟩ := y
              rw [has] at hm
              cases hm
              have hext : PEnv.Scoped (d + a.arity)
                  (freshDyns a.arity ++ PEnv.shiftBy a.arity env) :=
                PEnv.Scoped_append (freshDyns_scoped d a.arity) (PEnv.Scoped_shift a.arity henv)
              exact ⟨PRes.Scoped_toCode (h A idx _ _ a.body r rq₁ (d + a.arity) hext ht),
                     ih as' rq₂ d henv has⟩

/-! #### Prepared arguments -/

def Prepared.Scoped (d : Nat) (p : Prepared) : Prop :=
  ScopedLets d p.binds ∧ p.value.Scoped (d + p.binds.length)

theorem PVal.Scoped_toPRes : ∀ {pv : PVal} {d : Nat},
    PVal.Scoped d pv → PRes.Scoped d pv.toPRes
  | .stat _,   _, _ => trivial
  | .dyn _,    _, h => h
  | .cons _ _, _, h => ⟨PVal.Scoped_toPRes h.1, PVal.Scoped_toPRes h.2⟩

theorem Prepared.Scoped_toPRes {p : Prepared} {d : Nat} (h : Prepared.Scoped d p) :
    PRes.Scoped d p.toPRes :=
  ⟨h.1, PVal.Scoped_toPRes h.2⟩

/-- Appending binding lists: the second runs UNDER the first, which is why the
depth advances by the first's length.  This is the one place concatenation is
sound, and `prepare`'s `lets` case is the one caller entitled to it -- an inner
package's bindings genuinely live under its outer ones, so no weakening is owed.
-/
theorem ScopedLets_append : ∀ (a b : List Term) (d : Nat),
    ScopedLets d a → ScopedLets (d + a.length) b → ScopedLets d (a ++ b)
  | [],      _, _, _,  hb => by simpa using hb
  | t :: ts, b, d, ha, hb => by
      refine ⟨ha.1, ScopedLets_append ts b (d + 1) ha.2 ?_⟩
      have he : d + 1 + ts.length = d + (t :: ts).length := by simp; omega
      rw [he]; exact hb

/-! #### Preparation, verified

Built from small lemmas rather than one induction: each shape of `prepare`'s
answer gets its own, and the arithmetic is isolated in `PVal.Scoped.dyn` so it
never has to be done inside a structural proof. -/

theorem PVal.Scoped.dyn {k d : Nat} (h : k < d) : PVal.Scoped d (PVal.dyn k) := h

theorem prepare_stat_scoped {v : Val} {d : Nat} :
    Prepared.Scoped d ⟨[], .stat v⟩ := ⟨trivial, trivial⟩

/-- A reference the caller already held needs no binding. -/
theorem prepare_ref_scoped {i d : Nat} (h : i < d) :
    Prepared.Scoped d ⟨[], .dyn i⟩ := by
  refine ⟨trivial, PVal.Scoped.dyn ?_⟩
  have hidx : i < d + ([] : List Term).length := by simp; omega
  exact hidx

/-- Binding arbitrary scoped code exactly once. -/
theorem prepare_bind_scoped {t : Term} {d : Nat} (h : Term.Scoped d t) :
    Prepared.Scoped d ⟨[t], .dyn 0⟩ := by
  refine ⟨⟨h, trivial⟩, PVal.Scoped.dyn ?_⟩
  have hidx : 0 < d + ([t] : List Term).length := by simp
  exact hidx

/-- The conservative whole-subtree fallback. -/
theorem prepare_fallback_scoped {r : PRes} {d : Nat} (h : PRes.Scoped d r) :
    Prepared.Scoped d ⟨[r.toCode], .dyn 0⟩ :=
  prepare_bind_scoped (PRes.Scoped_toCode h)

/-- Only the RIGHT side needed bindings, so the left's references shift by them. -/
theorem prepare_cons_left {d : Nat} {bb : List Term} {va vb : PVal}
    (ha : Prepared.Scoped d (⟨[], va⟩ : Prepared))
    (hb : Prepared.Scoped d (⟨bb, vb⟩ : Prepared)) :
    Prepared.Scoped d (⟨bb, .cons (PVal.shift bb.length va) vb⟩ : Prepared) := by
  refine ⟨hb.1, ⟨?_, hb.2⟩⟩
  have hva : PVal.Scoped d va := by simpa [Prepared.Scoped] using ha.2
  exact PVal.Scoped_shift _ hva

/-- Only the LEFT side needed bindings, so the right's references shift by them. -/
theorem prepare_cons_right {d : Nat} {ba : List Term} {va vb : PVal}
    (ha : Prepared.Scoped d (⟨ba, va⟩ : Prepared))
    (hb : Prepared.Scoped d (⟨[], vb⟩ : Prepared)) :
    Prepared.Scoped d (⟨ba, .cons va (PVal.shift ba.length vb)⟩ : Prepared) := by
  refine ⟨ha.1, ⟨ha.2, ?_⟩⟩
  have hvb : PVal.Scoped d vb := by simpa [Prepared.Scoped] using hb.2
  exact PVal.Scoped_shift _ hvb

/-- Appending a package's own bindings, the one safe concatenation. -/
theorem prepare_lets_scoped {d : Nat} {bs : List Term} {p : Prepared}
    (hbs : ScopedLets d bs) (hp : Prepared.Scoped (d + bs.length) p) :
    Prepared.Scoped d (⟨bs ++ p.binds, p.value⟩ : Prepared) := by
  refine ⟨ScopedLets_append bs p.binds d hbs hp.1, ?_⟩
  have he : d + bs.length + p.binds.length = d + (bs ++ p.binds).length := by
    simp; omega
  exact he ▸ hp.2

/-- Preparation preserves scope. -/
theorem prepare_scoped : ∀ {r : PRes} {d : Nat},
    PRes.Scoped d r → Prepared.Scoped d (prepare r) := by
  intro r
  induction r with
  | stat v => intro d _; exact prepare_stat_scoped
  | code t =>
      intro d h
      cases t with
      | var i => exact prepare_ref_scoped h
      | lit _ | letIn _ _ | ite _ _ _ | prim _ _ | ctorT _ _ | caseT _ _ | call _ _ =>
          exact prepare_bind_scoped h
  | cons a b iha ihb =>
      intro d h
      have ha := iha h.1
      have hb := ihb h.2
      cases hpa : prepare a with
      | mk abs av =>
        cases hpb : prepare b with
        | mk bbs bv =>
          rw [hpa] at ha
          rw [hpb] at hb
          cases abs with
          | nil => simp only [prepare, hpa, hpb]; exact prepare_cons_left ha hb
          | cons _ _ =>
              cases bbs with
              | nil => simp only [prepare, hpa, hpb]; exact prepare_cons_right ha hb
              | cons _ _ =>
                  simp only [prepare, hpa, hpb]
                  exact prepare_fallback_scoped h
  | lets bs r ih =>
      intro d h
      simp only [prepare]
      exact prepare_lets_scoped h.1 (ih h.2)

/-- Scope for the single-pass transfer.  The two-function transfer this replaced
needed a private length equality to reach the same conclusion; here the binder
depth is `bs.length` by construction rather than by arithmetic that happens to
match. -/
theorem mixPArgs_scoped {n : Nat} (h : ScopeOK n) :
    ∀ A idx Δ ps ts env d bs env' rq,
      PEnv.Scoped d env →
      mixPArgs n A idx Δ env ps ts = .ok (bs, env', rq) →
      ScopedLets d bs ∧ PEnv.Scoped (d + bs.length) env' := by
  intro A idx Δ ps
  induction ps with
  | nil =>
      intro ts env d bs env' rq _ hm
      cases ts with
      | nil      => simp only [mixPArgs] at hm; cases hm; exact ⟨trivial, trivial⟩
      | cons _ _ => simp [mixPArgs] at hm
  | cons b ps' ih =>
      intro ts env d bs env' rq henv hm
      cases ts with
      | nil => cases b <;> simp [mixPArgs] at hm
      | cons t ts' =>
          cases b with
          | stat =>
              simp only [mixPArgs] at hm
              cases ht : mixTerm n A idx Δ env t with
              | error e => simp only [ht] at hm; simp at hm
              | ok x =>
                  obtain ⟨r, rq₁⟩ := x
                  simp only [ht] at hm
                  cases r with
                  | stat v =>
                      cases hp : mixPArgs n A idx Δ env ps' ts' with
                      | error e => simp only [hp] at hm; simp at hm
                      | ok y =>
                          obtain ⟨bs', env'', rq₂⟩ := y
                          simp only [hp] at hm
                          cases hm
                          obtain ⟨h1, h2⟩ := ih ts' env d _ env'' rq₂ henv hp
                          exact ⟨h1, ⟨trivial, h2⟩⟩
                  | code _   => simp at hm
                  | cons _ _ => simp at hm
                  | lets _ _ => simp at hm
          | dyn =>
              simp only [mixPArgs] at hm
              cases ht : mixTerm n A idx Δ env t with
              | error e => simp only [ht] at hm; simp at hm
              | ok x =>
                  obtain ⟨r, rq₁⟩ := x
                  simp only [ht] at hm
                  have hpr : Prepared.Scoped d (prepare r) :=
                    prepare_scoped (h A idx Δ env t r rq₁ d henv ht)
                  cases hp : mixPArgs n A idx Δ (PEnv.shiftBy (prepare r).binds.length env)
                                ps' ts' with
                  | error e => simp only [hp] at hm; simp at hm
                  | ok y =>
                      obtain ⟨bs', env'', rq₂⟩ := y
                      simp only [hp] at hm
                      cases hm
                      obtain ⟨h1, h2⟩ :=
                        ih ts' (PEnv.shiftBy (prepare r).binds.length env)
                           (d + (prepare r).binds.length) _ env'' rq₂
                           (PEnv.Scoped_shift _ henv) hp
                      refine ⟨ScopedLets_append _ _ d hpr.1 h1, ⟨?_, ?_⟩⟩
                      · have := PVal.Scoped_shift bs'.length hpr.2
                        have he : d + (prepare r).binds.length + bs'.length
                                = d + ((prepare r).binds ++ bs').length := by simp; omega
                        exact he ▸ this
                      · have he : d + (prepare r).binds.length + bs'.length
                                = d + ((prepare r).binds ++ bs').length := by simp; omega
                        exact he ▸ h2



theorem PEnv.Scoped_stat_map (d : Nat) : ∀ vs : List Val, PEnv.Scoped d (vs.map PVal.stat)
  | []      => trivial
  | _ :: vs => ⟨trivial, PEnv.Scoped_stat_map d vs⟩

/-! #### Layer 3: the fuel induction

Every list recursion and every binder-layout computation was discharged in
layers 1 and 2, so each branch here is a one-liner over them.  In particular NO
`dynCount` reasoning appears: the unfolded-call branch receives `ScopedLets` and
the extended environment ready-made. -/
theorem mixTerm_scoped : ∀ n, ScopeOK n := by
  intro n
  induction n with
  | zero =>
      intro A idx Δ env t r rq d _ hm
      simp [mixTerm] at hm
  | succ n ih =>
      intro A idx Δ env t r rq d henv hm
      cases t with
      | lit v => simp only [mixTerm] at hm; cases hm; trivial
      | var i =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          · rename_i _ _ _; cases hm; trivial
          · rename_i k _ hv; cases hm; exact PEnv.Scoped_lookup henv hv
          · rename_i a b _ hv; cases hm
            exact PVal.Scoped_toPRes (PEnv.Scoped_lookup henv hv)
      | lift e =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i _ _ _; cases hm; trivial
      | letIn b e body =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i re rq₁ he
          split at hm <;> try contradiction
          · rename_i v _
            split at hm <;> try contradiction
            rename_i rb rq₂ hb
            cases hm
            have henv2 : PEnv.Scoped d (PVal.stat v :: env) := ⟨trivial, henv⟩
            exact ih A idx _ _ body _ _ d henv2 hb
          · rename_i _
            split at hm <;> try contradiction
            rename_i rb rq₂ _hne hb
            cases hm
            have hre := ih A idx Δ env e _ _ d henv he
            have hbodyEnv : PEnv.Scoped (d + 1) (PVal.dyn 0 :: PEnv.shiftBy 1 env) :=
              ⟨by simp [PVal.Scoped], PEnv.Scoped_shift 1 henv⟩
            exact ⟨⟨PRes.Scoped_toCode hre, trivial⟩,
                   by simpa using ih A idx _ _ body _ _ (d + 1) hbodyEnv hb⟩
      | ite b c a e =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i rc rq₁ hc
          split at hm <;> try contradiction
          · rename_i _
            split at hm <;> try contradiction
            rename_i ra rq₂ ha
            cases hm
            exact ih A idx Δ env a _ _ d henv ha
          · rename_i _
            split at hm <;> try contradiction
            rename_i re' rq₂ he'
            cases hm
            exact ih A idx Δ env e _ _ d henv he'
          · rename_i _
            split at hm <;> try contradiction
            rename_i ra rq₂ re' rq₃ ha he'
            cases hm
            exact ⟨PRes.Scoped_toCode (ih A idx Δ env c _ _ d henv hc),
                   PRes.Scoped_toCode (ih A idx Δ env a _ _ d henv ha),
                   PRes.Scoped_toCode (ih A idx Δ env e _ _ d henv he')⟩
      | prim b p ts =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i rs rq' hts
          have hall := mixTerms_scoped ih A idx Δ env ts rs rq' d henv hts
          split at hm
          · split at hm <;> try contradiction
            split at hm <;> try contradiction
            rename_i _ _ _ _; cases hm; trivial
          · split at hm
            · rename_i r' hstruct
              cases hm
              exact primStruct_scoped hstruct hall
            · cases hm
              exact PRes.ScopedList_toCode hall
      | ctorT b k ts =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i rs rq' hts
          have hall := mixTerms_scoped ih A idx Δ env ts rs rq' d henv hts
          split at hm
          · split at hm <;> try contradiction
            rename_i _ _; cases hm; trivial
          · cases hm; exact PRes.ScopedList_toCode hall
      | caseT b sc alts =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i rsc rq₁ hsc
          split at hm <;> try contradiction
          · rename_i tag vs _
            split at hm <;> try contradiction
            rename_i a _
            split at hm <;> try contradiction
            split at hm <;> try contradiction
            rename_i rb rq₂ hb
            cases hm
            exact ih A idx _ _ a.body _ _ d
              (PEnv.Scoped_append (PEnv.Scoped_stat_map d vs) henv) hb
          · rename_i _
            split at hm <;> try contradiction
            rename_i alts' rq₂ has
            cases hm
            exact ⟨PRes.Scoped_toCode (ih A idx Δ env sc _ _ d henv hsc),
                   mixAlts_scoped ih A idx Δ env alts alts' rq₂ d henv has⟩
      | call b f ts =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i rs rq₁ hts
          split at hm <;> try contradiction
          rename_i fd _
          split at hm
          · split at hm <;> try contradiction
            split at hm <;> try contradiction
            split at hm <;> try contradiction
            rename_i vs _
            split at hm <;> try contradiction
            rename_i rb rq₂ hb
            cases hm
            exact ih A idx _ _ fd.body _ _ d (PEnv.Scoped_stat_map d vs) hb
          · split at hm <;> try contradiction
            rename_i svs dts hsp
            split at hm <;> try contradiction
            rename_i k _
            cases hm
            exact splitArgs_scoped fd.params rs svs dts
              (mixTerms_scoped ih A idx Δ env ts rs rq₁ d henv hts) hsp
      | ucall b f ts =>
          simp only [mixTerm] at hm
          split at hm <;> try contradiction
          rename_i fd _
          split at hm
          · split at hm <;> try contradiction
            rename_i rs rq₁ hts
            split at hm <;> try contradiction
            split at hm <;> try contradiction
            split at hm <;> try contradiction
            rename_i vs _
            split at hm <;> try contradiction
            rename_i rb rq₂ hb
            cases hm
            exact ih A idx _ _ fd.body _ _ d (PEnv.Scoped_stat_map d vs) hb
          · split at hm <;> try contradiction
            rename_i bs env' rq₂ hu
            split at hm <;> try contradiction
            rename_i rb rq₃ _hne hb
            cases hm
            obtain ⟨hbinds, henv'⟩ :=
              mixPArgs_scoped ih A idx Δ fd.params ts env d bs env' rq₂ henv hu
            exact ⟨hbinds, ih A idx _ _ fd.body _ rq₃ _ henv' hb⟩

/-! #### Layer 4: the whole generated program

`mixTerm_scoped` is LOCAL -- it says a result is scoped relative to the
environment it was given.  That does not say a generated residual FUNCTION is
scoped, because a function body runs in a fresh argument environment rather than
the caller's.  This layer supplies the missing half, and together they say that
successful specialization emits no dangling de Bruijn reference anywhere. -/

/-- Every residual function body is closed at its own arity. -/
def Program.Scoped (P : Program) : Prop :=
  ∀ fd ∈ P.funs, Term.Scoped fd.arity fd.body

/-- The environment a specialized function body starts in.  Static parameters
name no residual variable; the dynamic ones are numbered upwards from `j`, and
there are exactly `dynCount bs` of them, so every index lands below
`j + dynCount bs`. -/
theorem buildEnv_scoped : ∀ (bs : Div) (vs : List Val) (j : Nat) (env : PEnv),
    buildEnv bs vs j = .ok env → PEnv.Scoped (j + dynCount bs) env := by
  intro bs
  induction bs with
  | nil =>
      intro vs j env he
      cases vs with
      | nil      => simp only [buildEnv] at he; cases he; trivial
      | cons _ _ => simp [buildEnv] at he
  | cons b bs' ih =>
      intro vs j env he
      cases b with
      | stat =>
          cases vs with
          | nil => simp [buildEnv] at he
          | cons v vs' =>
              simp only [buildEnv] at he
              cases hr : buildEnv bs' vs' j with
              | error _ => rw [hr] at he; simp at he
              | ok rest =>
                  rw [hr] at he
                  cases he
                  exact ⟨trivial, by simpa [dynCount] using ih vs' j rest hr⟩
      | dyn =>
          simp only [buildEnv] at he
          cases hr : buildEnv bs' vs (j + 1) with
          | error _ => rw [hr] at he; simp at he
          | ok rest =>
              rw [hr] at he
              cases he
              refine ⟨?_, ?_⟩
              · simp only [PVal.Scoped, dynCount]; omega
              · have hih := ih vs (j + 1) rest hr
                have heq : j + 1 + dynCount bs' = j + dynCount (BT.dyn :: bs') := by
                  simp [dynCount]; omega
                rw [heq] at hih
                exact hih

theorem mixFun_scoped {stepFuel : Nat} {A : AProgram} {idx : SpecRequest → Option Nat}
    {req : SpecRequest} {fd : FunDef} {rq : List SpecRequest}
    (h : mixFun stepFuel A idx req = .ok (fd, rq)) : Term.Scoped fd.arity fd.body := by
  simp only [mixFun] at h
  split at h <;> try contradiction
  rename_i sfd _
  split at h <;> try contradiction
  rename_i env he
  split at h <;> try contradiction
  rename_i r rq' hmix
  cases h
  exact PRes.Scoped_toCode
    (mixTerm_scoped stepFuel A idx sfd.params env sfd.body r _ (dynCount sfd.params)
      (by simpa using buildEnv_scoped sfd.params req.staticArgs 0 env he) hmix)

theorem generateFrom_scoped {stepFuel : Nat} {A : AProgram} {idx : SpecRequest → Option Nat} :
    ∀ (reqs : List SpecRequest) (funs : List FunDef),
      generateFrom stepFuel A idx reqs = .ok funs →
      ∀ fd ∈ funs, Term.Scoped fd.arity fd.body := by
  intro reqs
  induction reqs with
  | nil => intro funs hg fd hfd; simp only [generateFrom] at hg; cases hg; simp at hfd
  | cons r rs ih =>
      intro funs hg fd hfd
      simp only [generateFrom] at hg
      cases hf : mixFun stepFuel A idx r with
      | error _ => rw [hf] at hg; simp at hg
      | ok pr =>
          obtain ⟨fd', rq'⟩ := pr
          rw [hf] at hg
          cases hgs : generateFrom stepFuel A idx rs with
          | error _ => rw [hgs] at hg; simp at hg
          | ok fds =>
              rw [hgs] at hg
              cases hg
              cases hfd with
              | head     => exact mixFun_scoped hf
              | tail _ m => exact ih fds hgs fd m

/-- Successful specialization emits no dangling de Bruijn reference. -/
theorem mixDriver_scoped {stepFuel wlFuel : Nat} {A : AProgram} {statics : List Val}
    {Pr : Program} (h : mixDriver stepFuel wlFuel A statics = .ok Pr) : Program.Scoped Pr := by
  simp only [mixDriver] at h
  split at h <;> try contradiction
  rename_i reqs _
  split at h <;> try contradiction
  rename_i funs hgen
  split at h <;> try contradiction
  rename_i e _
  cases h
  intro fd hfd
  exact generateFrom_scoped reqs funs hgen fd hfd

/-- The bridge that makes the invariant free where it is already established:
a partial value that DENOTES something names indices that exist, because
`ρr[k]? = some v` already says `k < ρr.length`. -/
theorem PValOK_Scoped {ρr : Env} : ∀ {pv : PVal} {v : Val},
    PValOK ρr pv v → PVal.Scoped ρr.length pv
  | .stat _,   _, _ => trivial
  | .dyn _,    _, h => by
      simp only [PValOK] at h
      exact List.getElem?_eq_some_iff.mp h |>.1
  | .cons a b, _, h => by
      obtain ⟨_, _, _, ha, hb⟩ := h
      exact ⟨PValOK_Scoped ha, PValOK_Scoped hb⟩

/-- The converse, and the half of the discard guard that `PRes.total` does NOT
supply: a scoped partial value DENOTES something.  Computation-freeness says
nothing may be lost by not running it; this says its references actually resolve.
Neither implies the other, and the soundness direction needs both. -/
theorem PValOK_of_Scoped {ρ : Env} : ∀ {pv : PVal},
    PVal.Scoped ρ.length pv → ∃ v, PValOK ρ pv v
  | .stat w,   _ => ⟨w, rfl⟩
  | .dyn k,    h => by
      have hk : k < ρ.length := h
      exact ⟨ρ[k], List.getElem?_eq_some_iff.mpr ⟨hk, rfl⟩⟩
  | .cons a b, h => by
      obtain ⟨ha, hb⟩ := h
      obtain ⟨x, hx⟩ := PValOK_of_Scoped (pv := a) ha
      obtain ⟨y, hy⟩ := PValOK_of_Scoped (pv := b) hb
      exact ⟨.cons x y, x, y, rfl, hx, hy⟩

/-- …and therefore a compatible environment is a scoped one, which is how the
specializer obtains the hypothesis it needs at the top of every walk. -/
theorem Compat_Scoped {ρr Δ env ρs} (h : Compat ρr Δ env ρs) :
    PEnv.Scoped ρr.length env := by
  induction h with
  | nil => trivial
  | stat _ ih => exact ⟨trivial, ih⟩
  | dyn hk _ ih => exact ⟨PValOK_Scoped hk, ih⟩

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
  | dyn hk _ ih =>
      intro i v hv
      cases i with
      | zero   =>
          -- the generalized dynamic case admits a `stat` entry; `PValOK` then
          -- says it names the same value, so the lookup still agrees
          simp only [List.getElem?_cons_zero, Option.some.injEq] at hv
          subst hv
          simp only [PValOK] at hk
          simp [hk]
      | succ n => simpa using ih n v (by simpa using hv)

/-- Reading a DYNAMIC slot, whatever partial shape it holds.  This is what a
preserved spine needs: `Compat_dyn_lookup` only speaks about a bare index. -/
theorem Compat_pval_lookup {ρr Δ env ρs} (h : Compat ρr Δ env ρs) :
    ∀ (i : Nat) (pv : PVal), env[i]? = some pv → Δ[i]? = some BT.dyn →
      ∃ v, ρs[i]? = some v ∧ PValOK ρr pv v := by
  induction h with
  | nil => intro i pv hv _; simp at hv
  | stat _ ih =>
      intro i pv hv hd
      cases i with
      | zero   => simp at hd
      | succ n => obtain ⟨v, h1, h2⟩ := ih n pv (by simpa using hv) (by simpa using hd)
                  exact ⟨v, by simpa using h1, h2⟩
  | dyn hk _ ih =>
      intro i pv hv hd
      cases i with
      | zero   => simp only [List.getElem?_cons_zero, Option.some.injEq] at hv
                  subst hv
                  exact ⟨_, by simp, hk⟩
      | succ n => obtain ⟨v, h1, h2⟩ := ih n pv (by simpa using hv) (by simpa using hd)
                  exact ⟨v, by simpa using h1, h2⟩

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
      refine .dyn (pv := .dyn pre.length) ?_ ?_
      · simp [PValOK]
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

inductive EvalLets (P : Program) : Env → List Term → Env → Prop where
  | nil  : EvalLets P ρ [] ρ
  | cons : Eval P ρ e d → EvalLets P (d :: ρ) es ρ' → EvalLets P ρ (e :: es) ρ'

theorem wrapLets_eval {P : Program} : ∀ (es : List Term) (ρ ρ' : Env) (body : Term) (v : Val),
    EvalLets P ρ es ρ' → Eval P ρ' body v → Eval P ρ (wrapLets es body) v
  | [],      _, _, _,    _, hl, hb => by cases hl; exact hb
  | e :: es, ρ, ρ', body, v, hl, hb => by
      cases hl with
      | cons he ht => exact .letIn he (wrapLets_eval es _ ρ' body v ht hb)

/-- `ρr` is a recursion argument rather than a parameter, because the PACKAGE
case evaluates its bindings and continues under the EXTENDED residual
environment. -/
def PResOK (Pr : Program) : Env → PRes → Val → Prop
  | ρr, .stat w,    v => w = v
  | ρr, .code c,    v => Eval Pr ρr c v
  | ρr, .cons a b,  v => ∃ x y, v = .cons x y ∧ PResOK Pr ρr a x ∧ PResOK Pr ρr b y
  | ρr, .lets bs r, v => ∃ ρ', EvalLets Pr ρr bs ρ' ∧ PResOK Pr ρ' r v

/-! The two projections the old conjunctive definition offered, kept so that
every existing proof reads the same. -/

theorem PResOK.statEq {Pr ρr r v} (h : PResOK Pr ρr r v) : ∀ w, r = .stat w → w = v := by
  intro w hw; subst hw; exact h

theorem PResOK.codeEval {Pr ρr r v} (h : PResOK Pr ρr r v) :
    ∀ c, r = .code c → Eval Pr ρr c v := by
  intro c hc; subst hc; exact h

/-- Pointwise relation over two lists.  Core has no `List.Forall₂` and this
development is Mathlib-free, so it is defined ONCE here and both pointwise
relations are instances of it: `PResAll` on the completeness side and
`PResSoundAll` on the soundness side.  One relation, one recursion. -/
inductive Forall₂ {α β : Type} (R : α → β → Prop) : List α → List β → Prop where
  | nil  : Forall₂ R [] []
  | cons : R a b → Forall₂ R as bs → Forall₂ R (a :: as) (b :: bs)

abbrev PResAll (Pr : Program) (ρr : Env) : List PRes → List Val → Prop :=
  Forall₂ (PResOK Pr ρr)

theorem PResOK_toCode : ∀ {Pr ρr r v}, PResOK Pr ρr r v → Eval Pr ρr r.toCode v
  | _, _, .stat w, v, h => by
      have : w = v := h.statEq w rfl
      subst this; exact .lit
  | _, _, .code c, _, h => h.codeEval c rfl
  | Pr, ρr, .cons a b, _, h => by
      obtain ⟨x, y, hv, ha, hb⟩ := h
      subst hv
      exact .prim (.cons (PResOK_toCode ha) (.cons (PResOK_toCode hb) .nil)) rfl
  | Pr, ρr, .lets bs r, v, h => by
      obtain ⟨ρ', hl, hr⟩ := h
      exact wrapLets_eval bs ρr ρ' (PRes.toCode r) v hl (PResOK_toCode hr)

/-! #### Structural answers, forwards

`hd`/`tl`/`isNil` answered from a spine `mix` already holds.  Forwards the
source value is GIVEN, so the peel only has to agree with it; the guard on the
discarded component is what the other direction needs, and it is not consulted
here. -/

theorem evalPrim_hd_inv : ∀ {u v : Val}, evalPrim .hd [u] = .ok v → ∃ y, u = .cons v y
  | .cons _ _, _, h => by simp only [evalPrim] at h; cases h; exact ⟨_, rfl⟩
  | .int _,  _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .bool _, _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .nil,    _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .ctor _ _, _, h => by simp only [evalPrim] at h; split at h <;> cases h

theorem evalPrim_tl_inv : ∀ {u v : Val}, evalPrim .tl [u] = .ok v → ∃ x, u = .cons x v
  | .cons _ _, _, h => by simp only [evalPrim] at h; cases h; exact ⟨_, rfl⟩
  | .int _,  _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .bool _, _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .nil,    _, h => by simp only [evalPrim] at h; split at h <;> cases h
  | .ctor _ _, _, h => by simp only [evalPrim] at h; split at h <;> cases h

theorem peelHd_ok {Pr} : ∀ {r r' : PRes} {ρr : Env} {x y : Val},
    peelHd r = some r' → PResOK Pr ρr r (.cons x y) → PResOK Pr ρr r' x := by
  intro r
  induction r with
  | stat w =>
      intro r' ρr x y hp _
      simp [peelHd] at hp
  | code c => intro r' ρr x y hp _; cases c <;> simp [peelHd] at hp
  | cons a b _ _ =>
      intro r' ρr x y hp hok
      simp only [peelHd] at hp
      split at hp
      · cases hp
        obtain ⟨_, _, he, ha, _⟩ := hok
        cases he
        exact ha
      · simp at hp
  | lets bs rr ih =>
      intro r' ρr x y hp hok
      simp only [peelHd, Option.map_eq_some_iff] at hp
      obtain ⟨r'', hp'', hr'⟩ := hp
      subst hr'
      obtain ⟨ρ', hl, hrr⟩ := hok
      exact ⟨ρ', hl, ih hp'' hrr⟩

theorem peelTl_ok {Pr} : ∀ {r r' : PRes} {ρr : Env} {x y : Val},
    peelTl r = some r' → PResOK Pr ρr r (.cons x y) → PResOK Pr ρr r' y := by
  intro r
  induction r with
  | stat w =>
      intro r' ρr x y hp _
      simp [peelTl] at hp
  | code c => intro r' ρr x y hp _; cases c <;> simp [peelTl] at hp
  | cons a b _ _ =>
      intro r' ρr x y hp hok
      simp only [peelTl] at hp
      split at hp
      · cases hp
        obtain ⟨_, _, he, _, hb⟩ := hok
        cases he
        exact hb
      · simp at hp
  | lets bs rr ih =>
      intro r' ρr x y hp hok
      simp only [peelTl, Option.map_eq_some_iff] at hp
      obtain ⟨r'', hp'', hr'⟩ := hp
      subst hr'
      obtain ⟨ρ', hl, hrr⟩ := hok
      exact ⟨ρ', hl, ih hp'' hrr⟩

theorem peelIsNil_ok {Pr} : ∀ {r r' : PRes} {ρr : Env} {u v : Val},
    peelIsNil r = some r' → PResOK Pr ρr r u → evalPrim .isNil [u] = .ok v →
    PResOK Pr ρr r' v := by
  intro r
  induction r with
  | stat w =>
      intro r' ρr u v hp _ _
      simp [peelIsNil] at hp
  | code c => intro r' ρr u v hp _ _; cases c <;> simp [peelIsNil] at hp
  | cons a b _ _ =>
      intro r' ρr u v hp hok hv
      simp only [peelIsNil] at hp
      split at hp
      · cases hp
        obtain ⟨x, y, he, _, _⟩ := hok
        subst he
        simp only [evalPrim] at hv
        cases hv
        rfl
      · simp at hp
  | lets bs rr ih =>
      intro r' ρr u v hp hok hv
      simp only [peelIsNil, Option.map_eq_some_iff] at hp
      obtain ⟨r'', hp'', hr'⟩ := hp
      subst hr'
      obtain ⟨ρ', hl, hrr⟩ := hok
      exact ⟨ρ', hl, ih hp'' hrr hv⟩

/-- A structural answer agrees with the source primitive. -/
theorem primStruct_ok {Pr : Program} {ρr : Env} {p : Prim} {rs : List PRes}
    {vs : List Val} {r : PRes} {v : Val}
    (hps : primStruct p rs = some r) (hrs : PResAll Pr ρr rs vs)
    (hp : evalPrim p vs = .ok v) :
    PResOK Pr ρr r v := by
  cases p with
  | addI | subI | mulI | divI | modI | ltI | leI | eqI | andB | orB | notB
  | eqV | mkCtorP | ctorTagP | ctorFieldsP | bvMk | bvWidth | bvUint | bvBit
  | bvAnd | bvOr | bvXor | bvNot | bvResize
  | bvSra | bvGetMask | bvSint | bvShl => simp [primStruct] at hps
  | consP =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons ra rest =>
        cases rest with
        | nil => simp [primStruct] at hps
        | cons rb rest2 =>
          cases rest2 with
          | cons _ _ => simp [primStruct] at hps
          | nil =>
            cases hrs with
            | cons hA ht =>
              cases ht with
              | cons hB hn =>
                cases hn
                simp only [evalPrim] at hp
                cases hp
                cases ra with
                | stat a =>
                  cases rb with
                  | stat b =>
                      -- both operands static: `consP` answers with a VALUE
                      simp only [primStruct] at hps
                      cases hps
                      show Val.cons a b = _
                      rw [(hA : a = _), (hB : b = _)]
                  | code _ | cons _ _ | lets _ _ =>
                      simp only [primStruct] at hps
                      cases hps
                      exact ⟨_, _, rfl, hA, hB⟩
                | code _ | cons _ _ | lets _ _ =>
                    simp only [primStruct] at hps
                    cases hps
                    exact ⟨_, _, rfl, hA, hB⟩
  | hd =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons r₀ rest =>
        cases rest with
        | cons _ _ => simp [primStruct] at hps
        | nil =>
          cases hrs with
          | cons hA ht =>
            cases ht
            obtain ⟨y, hu⟩ := evalPrim_hd_inv hp
            subst hu
            simp only [primStruct] at hps
            exact peelHd_ok hps hA
  | tl =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons r₀ rest =>
        cases rest with
        | cons _ _ => simp [primStruct] at hps
        | nil =>
          cases hrs with
          | cons hA ht =>
            cases ht
            obtain ⟨x, hu⟩ := evalPrim_tl_inv hp
            subst hu
            simp only [primStruct] at hps
            exact peelTl_ok hps hA
  | isNil =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons r₀ rest =>
        cases rest with
        | cons _ _ => simp [primStruct] at hps
        | nil =>
          cases hrs with
          | cons hA ht =>
            cases ht
            simp only [primStruct] at hps
            exact peelIsNil_ok hps hA hp

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
        rw [allStatic_forall₂ Pr ρr rs vs us ht hus, hr.statEq w rfl]
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
staircase is exactly why argument transfer has to thread the residual scope. -/

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

`TOK` is what the main induction proves.  `mixTerms`, `mixAlts` and `mixPArgs`
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
        have : w = v := hr.statEq w rfl
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

theorem PResOK_stat {Pr ρr w v} (h : w = v) : PResOK Pr ρr (.stat w) v := h

theorem PResOK_code {Pr ρr c v} (h : Eval Pr ρr c v) : PResOK Pr ρr (.code c) v := h

/-- …and the partial-structure introduction, which is what the `consP` rule
needs: a known spine denotes a cons value built from what its parts denote. -/
theorem PResOK_cons {Pr ρr a b x y} (ha : PResOK Pr ρr a x) (hb : PResOK Pr ρr b y) :
    PResOK Pr ρr (.cons a b) (.cons x y) :=
  ⟨x, y, rfl, ha, hb⟩

/-- A preserved spine's residual code can only produce the value the spine
denotes.  Needed by the COMPLETENESS direction, which starts from the residual
value and has to land on the source's.

Stated over `Eval` rather than `evalFuel`: the fuel form forces the proof to
unfold a nested `evalFuelList` match at every level of the spine, while the
relation gives one constructor per level. -/
theorem PValOK_Eval_eq {Pr ρr} : ∀ {pv : PVal} {u v : Val},
    PValOK ρr pv u → Eval Pr ρr (PVal.toPRes pv).toCode v → v = u
  | .stat _,   _, _, h, hev => by cases hev; exact h
  | .dyn _,    _, _, h, hev => by
      cases hev
      rename_i hk
      rw [h] at hk
      exact (Option.some.inj hk).symm
  | .cons a b, _, _, h, hev => by
      obtain ⟨x, y, hu, ha, hb⟩ := h
      subst hu
      cases hev
      rename_i vs hl hp
      cases hl
      rename_i vA vsB hA hl2
      cases hl2
      rename_i vB vsN hB hnil
      cases hnil
      simp only [evalPrim] at hp
      cases hp
      rw [PValOK_Eval_eq ha hA, PValOK_Eval_eq hb hB]

/-- A partial environment entry read back as a result keeps its denotation --
which is what lets the `var` rule return a spine instead of flattening it. -/
theorem PResOK_toPRes {Pr ρr} : ∀ {pv : PVal} {v : Val},
    PValOK ρr pv v → PResOK Pr ρr pv.toPRes v
  | .stat _,   _, h => h
  | .dyn _,    _, h => Eval.var h
  | .cons a b, _, h => by
      obtain ⟨x, y, hv, ha, hb⟩ := h
      subst hv
      exact ⟨x, y, rfl, PResOK_toPRes ha, PResOK_toPRes hb⟩

/-- The semantic relation for a prepared argument, parallel to `PResOK`'s
package case: the bindings run, and the value denotes under the environment they
extend. -/
def PreparedOK (P : Program) (ρ : Env) (p : Prepared) (v : Val) : Prop :=
  ∃ ρ', EvalLets P ρ p.binds ρ' ∧ PValOK ρ' p.value v

theorem PreparedOK_toPRes {P ρ p v} (h : PreparedOK P ρ p v) : PResOK P ρ p.toPRes v := by
  obtain ⟨ρ', hl, hv⟩ := h
  exact ⟨ρ', hl, PResOK_toPRes hv⟩

/-! #### Preparation, semantically

The transport lemmas: running bindings extends the residual environment, and a
value that denoted something before still denotes it after, once its references
are shifted by the number of bindings run. -/

theorem PVal.shift_add : ∀ (a b : Nat) (v : PVal),
    PVal.shift a (PVal.shift b v) = PVal.shift (b + a) v
  | _, _, .stat _   => rfl
  | a, b, .dyn i    => by simp [PVal.shift]; omega
  | a, b, .cons x y => by simp [PVal.shift, PVal.shift_add a b x, PVal.shift_add a b y]

theorem EvalLets_nil {P : Program} {ρ ρ' : Env} (h : EvalLets P ρ [] ρ') : ρ' = ρ := by
  cases h; rfl

theorem EvalLets_append {P : Program} : ∀ {ρ ρ' ρ'' : Env} {a b : List Term},
    EvalLets P ρ a ρ' → EvalLets P ρ' b ρ'' → EvalLets P ρ (a ++ b) ρ''
  | _, _, _, [],     _, ha, hb => by cases ha; exact hb
  | _, _, _, _ :: _, _, ha, hb => by
      cases ha with
      | cons he ht => exact .cons he (EvalLets_append ht hb)

theorem PValOK_EvalLets {P : Program} :
    ∀ {ρ ρ' : Env} {bs : List Term} {pv : PVal} {w : Val},
      EvalLets P ρ bs ρ' → PValOK ρ pv w → PValOK ρ' (PVal.shift bs.length pv) w
  | _, _, [],     _, _, h, hv => by cases h; simpa using hv
  | _, _, _ :: _, _, _, h, hv => by
      cases h with
      | cons he ht =>
          have h2 := PValOK_EvalLets ht (PValOK_shift1 _ hv)
          simpa [PVal.shift_add, Nat.add_comm] using h2

theorem PEnv.shiftBy_add : ∀ (a b : Nat) (env : PEnv),
    PEnv.shiftBy a (PEnv.shiftBy b env) = PEnv.shiftBy (b + a) env
  | _, _, []        => rfl
  | a, b, v :: rest => by
      simp only [PEnv.shiftBy, PVal.shift_add, PEnv.shiftBy_add a b rest]

/-- Running a package's bindings transports a WHOLE compatible environment.

This is the transport lemma one-pass argument transfer needs and `mixUArgs`
never did.  `mixUArgs` entered exactly one binder per dynamic argument, so
`Compat_shift1` covered it; preparation emits zero, one or several bindings per
argument, so the later arguments are mixed under `env.shiftBy k` for a `k` only
`prepare` knows.  The source side does not move -- bindings are residual-only --
so the statement is `Compat` at the same `Δ` and the same `ρs`. -/
theorem Compat_after_EvalLets {Pr : Program} :
    ∀ {ρ ρ' : Env} {bs : List Term} {Δ : Div} {env : PEnv} {ρs : Env},
      EvalLets Pr ρ bs ρ' → Compat ρ Δ env ρs →
      Compat ρ' Δ (env.shiftBy bs.length) ρs
  | _, _, [],     _, _, _, hl, hc => by cases hl; simpa using hc
  | _, _, _ :: _, _, _, _, hl, hc => by
      cases hl with
      | cons he ht =>
          have h2 := Compat_after_EvalLets ht (Compat_shift1 _ hc)
          simpa [PEnv.shiftBy_add, Nat.add_comm] using h2

/-- Preparation preserves meaning: the bindings it emits evaluate, and the
partial value it keeps denotes what the original result denoted. -/
theorem prepare_ok {P : Program} : ∀ {ρ : Env} {r : PRes} {v : Val},
    PResOK P ρ r v → PreparedOK P ρ (prepare r) v := by
  intro ρ r
  induction r generalizing ρ with
  | stat w => intro v h; exact ⟨ρ, .nil, h⟩
  | code t =>
      intro v h
      cases t with
      | var i =>
          refine ⟨ρ, .nil, ?_⟩
          cases h with | var hk => exact hk
      | lit _ | letIn _ _ | ite _ _ _ | prim _ _ | ctorT _ _ | caseT _ _ | call _ _ =>
          exact ⟨v :: ρ, .cons h .nil, rfl⟩
  | cons a b iha ihb =>
      intro v h
      obtain ⟨x, y, hv, ha, hb⟩ := h
      subst hv
      have pa := iha ha
      have pb := ihb hb
      cases hpa : prepare a with
      | mk abs av =>
        cases hpb : prepare b with
        | mk bbs bv =>
          rw [hpa] at pa
          rw [hpb] at pb
          cases abs with
          | nil =>
              simp only [prepare, hpa, hpb]
              obtain ⟨ρa, hla, hva⟩ := pa
              obtain ⟨ρb, hlb, hvb⟩ := pb
              cases EvalLets_nil hla
              exact ⟨ρb, hlb, _, _, rfl, PValOK_EvalLets hlb hva, hvb⟩
          | cons _ _ =>
              cases bbs with
              | nil =>
                  simp only [prepare, hpa, hpb]
                  obtain ⟨ρa, hla, hva⟩ := pa
                  obtain ⟨ρb, hlb, hvb⟩ := pb
                  cases EvalLets_nil hlb
                  exact ⟨ρa, hla, _, _, rfl, hva, PValOK_EvalLets hla hvb⟩
              | cons _ _ =>
                  simp only [prepare, hpa, hpb]
                  exact ⟨_, .cons (PResOK_toCode ⟨_, _, rfl, ha, hb⟩) .nil, rfl⟩
  | lets bs r ih =>
      intro v h
      obtain ⟨ρ1, hl1, hr⟩ := h
      obtain ⟨ρ2, hl2, hv2⟩ := ih hr
      exact ⟨ρ2, EvalLets_append hl1 hl2, hv2⟩

/-! #### Preparation, backwards

The soundness direction reads the residual and must recover the source.  It
cannot go through `PRes.toCode`, because after the switch the residual never
RUNS `toCode`.  A spine `prepare` left unbound costs the residual nothing, while
`toCode` rebuilds the whole `consP` chain and charges one unit of fuel per
level, so `evalFuel mr` of `toCode` can time out at a fuel the residual finishes
in.  (Measured: a three-leaf static spine prepares to ZERO bindings, and its
`toCode` first returns a value at fuel 3.)  Since the fuel bound `mr` comes from
the residual and `specSound_all` only ever supplies `SOK` at fuels BELOW it,
that gap cannot be closed by raising the fuel.

So the soundness invariant is stated over what the residual actually runs: the
prepared form.  `prepare_peel` below is the bridge, and it goes in the easy
direction -- `toCode` does strictly MORE work than the package, so a `toCode`
run always contains a package run. -/

/-- A package's bindings, run under a fuel bound.  The bound is uniform rather
than decreasing: peeling a `wrapLets` produces a decreasing sequence and
`evalFuel_mono` lifts each step back to the bound, which is what keeps the
relation composable under `++`. -/
inductive EvalLetsAt (mr : Nat) (P : Program) : Env → List Term → Env → Prop where
  | nil  : EvalLetsAt mr P ρ [] ρ
  | cons : evalFuel mr P ρ e = .value d →
           EvalLetsAt mr P (d :: ρ) es ρ' → EvalLetsAt mr P ρ (e :: es) ρ'

theorem EvalLetsAt_mono {P : Program} : ∀ {m m' : Nat} {ρ ρ' : Env} {bs : List Term},
    m ≤ m' → EvalLetsAt m P ρ bs ρ' → EvalLetsAt m' P ρ bs ρ'
  | _, _, _, _, [],     _,   h => by cases h; exact .nil
  | _, _, _, _, _ :: _, hle, h => by
      cases h with
      | cons he ht =>
          exact .cons (evalFuel_mono _ _ _ _ _ _ hle he) (EvalLetsAt_mono hle ht)

/-- Dropping the bound recovers the fuel-free relation the completeness side
uses, so the two halves share `PValOK_EvalLets` rather than each proving it. -/
theorem EvalLetsAt_toEvalLets {P : Program} : ∀ {m : Nat} {ρ ρ' : Env} {bs : List Term},
    EvalLetsAt m P ρ bs ρ' → EvalLets P ρ bs ρ'
  | _, _, _, [],     h => by cases h; exact .nil
  | _, _, _, _ :: _, h => by
      cases h with
      | cons he ht => exact .cons (evalFuel_sound _ _ _ _ _ he) (EvalLetsAt_toEvalLets ht)

theorem EvalLetsAt_append {P : Program} {m : Nat} :
    ∀ {ρ ρ' ρ'' : Env} {a b : List Term},
      EvalLetsAt m P ρ a ρ' → EvalLetsAt m P ρ' b ρ'' → EvalLetsAt m P ρ (a ++ b) ρ''
  | _, _, _, [],     _, ha, hb => by cases ha; exact hb
  | _, _, _, _ :: _, _, ha, hb => by
      cases ha with
      | cons he ht => exact .cons he (EvalLetsAt_append ht hb)

/-- Peeling a `wrapLets` under a fuel bound.  The body is reached at SOME fuel
below the bound; the bindings are reported at the bound itself. -/
theorem wrapLets_peel {P : Program} :
    ∀ (bs : List Term) (mr : Nat) (ρ : Env) (body : Term) (v : Val),
      evalFuel mr P ρ (wrapLets bs body) = .value v →
      ∃ (ρ' : Env) (m' : Nat),
        m' ≤ mr ∧ EvalLetsAt mr P ρ bs ρ' ∧ evalFuel m' P ρ' body = .value v
  | [],      mr, ρ, body, v, hev => ⟨ρ, mr, Nat.le_refl _, .nil, by simpa [wrapLets] using hev⟩
  | e :: es, mr, ρ, body, v, hev => by
      cases mr with
      | zero => simp [evalFuel] at hev
      | succ mq =>
        simp only [wrapLets, evalFuel] at hev
        cases hd : evalFuel mq P ρ e with
        | outOfFuel   => rw [hd] at hev; simp at hev
        | typeError _ => rw [hd] at hev; simp at hev
        | value d =>
          rw [hd] at hev
          obtain ⟨ρ', m', hle, hl, hb⟩ := wrapLets_peel es mq (d :: ρ) body v hev
          exact ⟨ρ', m', by omega,
                 .cons (evalFuel_mono _ _ _ _ _ _ (Nat.le_succ mq) hd)
                       (EvalLetsAt_mono (Nat.le_succ mq) hl), hb⟩

/-- The converse companion to `prepare_ok`: a `toCode` run always contains a
run of the package.  This is what lets the soundness invariant be stated over
the prepared form while `specSound_all` still hands it a `toCode` run. -/
theorem prepare_peel {P : Program} : ∀ (r : PRes) (mr : Nat) (ρ : Env) (v : Val),
    evalFuel mr P ρ r.toCode = .value v →
    ∃ ρp, EvalLetsAt mr P ρ (prepare r).binds ρp ∧ PValOK ρp (prepare r).value v := by
  intro r
  induction r with
  | stat w =>
      intro mr ρ v hev
      cases mr with
      | zero => simp [evalFuel] at hev
      | succ mq =>
          simp only [PRes.toCode, evalFuel] at hev
          cases hev
          exact ⟨ρ, .nil, rfl⟩
  | code c =>
      intro mr ρ v hev
      cases c with
      | var i =>
          refine ⟨ρ, .nil, ?_⟩
          cases mr with
          | zero => simp [evalFuel] at hev
          | succ mq =>
              simp only [PRes.toCode, evalFuel] at hev
              cases hk : ρ[i]? with
              | none   => rw [hk] at hev; simp at hev
              | some u => rw [hk] at hev; cases hev; exact hk
      | lit _ | letIn _ _ | ite _ _ _ | prim _ _ | ctorT _ _ | caseT _ _ | call _ _ =>
          exact ⟨v :: ρ, .cons hev .nil, rfl⟩
  | cons a b iha ihb =>
      intro mr ρ v hev
      cases mr with
      | zero => simp [evalFuel] at hev
      | succ mq =>
        simp only [PRes.toCode, evalFuel] at hev
        cases hx : evalFuel mq P ρ (PRes.toCode a) with
        | outOfFuel   => simp [evalFuelList, hx] at hev
        | typeError _ => simp [evalFuelList, hx] at hev
        | value x =>
          cases hy : evalFuel mq P ρ (PRes.toCode b) with
          | outOfFuel   => simp [evalFuelList, hx, hy] at hev
          | typeError _ => simp [evalFuelList, hx, hy] at hev
          | value y =>
            simp only [evalFuelList, hx, hy, evalPrim] at hev
            cases hev
            obtain ⟨ρa, hla, hva⟩ := iha mq ρ x hx
            obtain ⟨ρb, hlb, hvb⟩ := ihb mq ρ y hy
            have hlea : mq ≤ mq + 1 := Nat.le_succ mq
            cases hpa : prepare a with
            | mk abs av =>
              cases hpb : prepare b with
              | mk bbs bv =>
                rw [hpa] at hla hva
                rw [hpb] at hlb hvb
                cases abs with
                | nil =>
                    cases hla
                    simp only [prepare, hpa, hpb]
                    refine ⟨ρb, EvalLetsAt_mono hlea hlb, x, y, rfl, ?_, hvb⟩
                    exact PValOK_EvalLets (EvalLetsAt_toEvalLets hlb) hva
                | cons ah at' =>
                  cases bbs with
                  | nil =>
                      cases hlb
                      simp only [prepare, hpa, hpb]
                      refine ⟨ρa, EvalLetsAt_mono hlea hla, x, y, rfl, hva, ?_⟩
                      exact PValOK_EvalLets (EvalLetsAt_toEvalLets hla) hvb
                  | cons bh bt =>
                      simp only [prepare, hpa, hpb]
                      refine ⟨Val.cons x y :: ρ, .cons ?_ .nil, rfl⟩
                      simp only [PRes.toCode, evalFuel, evalFuelList, hx, hy, evalPrim]
  | lets bs r' ih =>
      intro mr ρ v hev
      simp only [PRes.toCode] at hev
      obtain ⟨ρ1, m', hle, hl1, hb⟩ := wrapLets_peel bs mr ρ (PRes.toCode r') v hev
      obtain ⟨ρp, hl2, hv2⟩ := ih m' ρ1 v hb
      exact ⟨ρp, EvalLetsAt_append hl1 (EvalLetsAt_mono hle hl2), hv2⟩

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

/-- The semantic half of one-pass argument transfer.

`mixPArgs` returns the flattened binding list and the partial environment
TOGETHER, so this says exactly one thing: running the bindings reaches SOME
residual environment, and under that environment the partial environment is
compatible with the source argument values.

The transfer this replaced had to expose both `ws.length = dynCount ps` and the
shape `ρr' = ws.reverse ++ ρr`.  Those were the two facts a caller then had to
keep in step with a second function's independent reconstruction of the same
layout by hand -- the `wrapLets` index-arithmetic class of bug.  Here the
residual environment is existential and the agreement is structural, so there is
nothing for a caller to re-derive.  Counting is deliberately private: it lives
inside the proof, never in the statement. -/
theorem mixPArgs_ok {A Pr reqs n m} (h : TOK A Pr reqs n m) :
    ∀ (ps : Div) (ts : List ATerm) (Δ : Div) (env : PEnv) (bs : List Term)
      (env' : PEnv) (rq : List SpecRequest) (ρr ρs : Env) (vs : List Val),
      Compat ρr Δ env ρs →
      mixPArgs n A (indexOfReq reqs) Δ env ps ts = .ok (bs, env', rq) →
      evalFuelList m (eraseProgram A) ρs (eraseList ts) = .inl vs →
      ∃ ρr', EvalLets Pr ρr bs ρr' ∧ Compat ρr' ps env' vs := by
  intro ps
  induction ps with
  | nil =>
      intro ts Δ env bs env' rq ρr ρs vs _ hmix hsrc
      cases ts with
      | nil =>
          simp only [mixPArgs] at hmix
          cases hmix
          simp only [eraseList, evalFuelList] at hsrc
          cases hsrc
          exact ⟨ρr, .nil, .nil⟩
      | cons _ _ => simp [mixPArgs] at hmix
  | cons b ps' ih =>
      intro ts Δ env bs env' rq ρr ρs vs hc hmix hsrc
      cases ts with
      | nil => cases b <;> simp [mixPArgs] at hmix
      | cons t ts' =>
        simp only [eraseList, evalFuelList] at hsrc
        split at hsrc <;> try contradiction
        rename_i v₀ hv₀
        split at hsrc <;> try contradiction
        rename_i vs' hvs'
        cases hsrc
        cases b with
        | stat =>
            -- a static argument emits no binding, so the residual environment
            -- the tail is mixed under is the one we already have
            simp only [mixPArgs] at hmix
            cases hres : mixTerm n A (indexOfReq reqs) Δ env t with
            | error z => simp [hres] at hmix
            | ok pr =>
              obtain ⟨r, rq₁⟩ := pr
              have hr := h Δ env t r rq₁ ρr ρs v₀ hc hres hv₀
              simp only [hres] at hmix
              cases r with
              | code _ => simp at hmix
              | cons _ _ => simp at hmix
              | lets _ _ => simp at hmix
              | stat w =>
                cases hrec : mixPArgs n A (indexOfReq reqs) Δ env ps' ts' with
                | error z => simp [hrec] at hmix
                | ok q =>
                  obtain ⟨bs', env'', rq₂⟩ := q
                  -- appeal to the tail BEFORE `cases hmix`: `bs'` is a bare
                  -- variable in that equation, so `cases` eliminates it
                  obtain ⟨ρf, hlets, hcp⟩ :=
                    ih ts' Δ env bs' env'' rq₂ ρr ρs vs' hc hrec hvs'
                  simp only [hrec] at hmix
                  cases hmix
                  have hw : w = v₀ := hr.statEq w rfl
                  subst hw
                  exact ⟨ρf, hlets, hcp.stat⟩
        | dyn =>
            -- a dynamic argument is PREPARED: its code leaves become bindings
            -- and its partial structure survives as an environment entry.  The
            -- tail is mixed under however many bindings that actually was --
            -- `Compat_after_EvalLets` is what carries the invariant across them
            simp only [mixPArgs] at hmix
            cases hres : mixTerm n A (indexOfReq reqs) Δ env t with
            | error z => simp [hres] at hmix
            | ok pr =>
              obtain ⟨r, rq₁⟩ := pr
              have hr := h Δ env t r rq₁ ρr ρs v₀ hc hres hv₀
              simp only [hres] at hmix
              obtain ⟨ρp, hpLets, hpVal⟩ := prepare_ok hr
              cases hrec : mixPArgs n A (indexOfReq reqs) Δ
                  (env.shiftBy (prepare r).binds.length) ps' ts' with
              | error z => simp [hrec] at hmix
              | ok q =>
                obtain ⟨bs', env'', rq₂⟩ := q
                simp only [hrec] at hmix
                cases hmix
                obtain ⟨ρf, htLets, htCompat⟩ :=
                  ih ts' Δ (env.shiftBy (prepare r).binds.length) bs' env'' rq₂
                     ρp ρs vs' (Compat_after_EvalLets hpLets hc) hrec hvs'
                exact ⟨ρf, EvalLets_append hpLets htLets,
                       .dyn (PValOK_EvalLets htLets hpVal) htCompat⟩

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
            · -- a PRESERVED SPINE: read it back structurally rather than
              -- flattening it, which is the whole point of keeping it
              rename_i a b hdiv henv
              cases hmix
              obtain ⟨u', hs, hp⟩ := Compat_pval_lookup hc i (.cons a b) henv hdiv
              rw [hu] at hs
              cases Option.some.inj hs
              exact PResOK_toPRes hp

        | lift e =>
            simp only [mixTerm] at hmix
            split at hmix <;> try contradiction
            rename_i w rq' he
            cases hmix
            simp only [erase] at hsrc
            have := ihm Δ env e (.stat w) _ ρr ρs v hc he hsrc
            exact PResOK_code (by rw [this.statEq w rfl]; exact .lit)

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
              have hw : w = v₁ := hre.statEq w rfl
              subst hw
              exact ihm' _ _ body r _ ρr (w :: ρs) v (hc.stat) hbody hsrc
            · -- dynamic binding: one residual binder, so everything shifts
              rename_i _
              split at hmix <;> try contradiction
              rename_i rb rq₂ _hne hbody
              cases hmix
              have hcb : Compat (v₁ :: ρr) (.dyn :: Δ) (.dyn 0 :: env.shiftBy 1) (v₁ :: ρs) :=
                .dyn (pv := .dyn 0) (by simp [PValOK]) (Compat_shift1 v₁ hc)
              have hb := ihm' _ _ body rb rq₂ (v₁ :: ρr) (v₁ :: ρs) v hcb hbody hsrc
              exact ⟨v₁ :: ρr, .cons (PResOK_toCode hre) .nil, hb⟩

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
            · split at hmix
              · rename_i r' hstruct
                cases hmix
                exact primStruct_ok hstruct hall hp
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
                exact absurd (hrc.statEq _ rfl) (by simp)
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
                exact absurd (hrc.statEq _ rfl) (by simp)
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
              have hct : Val.ctor tag' vs' = Val.ctor tag vs := hs.statEq _ rfl
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
              rename_i bs env' rq₂ hua
              split at hmix <;> try contradiction
              rename_i rb rq₃ _hne hbody
              cases hmix
              obtain ⟨ρr', hlets, hcp⟩ :=
                mixPArgs_ok ihm' fd.params ts Δ env bs env' rq₂ ρr ρs vs hc hua hvs
              have hb := ihm' _ _ fd.body rb rq₃ ρr' vs v hcp hbody
                           (by simpa [eraseFunDef] using hsrc)
              exact ⟨ρr', hlets, hb⟩

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

The closure is function-major, so the entry request is NOT at index 0 -- but
`mixDriver` looks its index up, and `indexOfReq_spec` turns that lookup into
exactly the fact these two theorems need.  Nothing else in this file depends on
the order requests come out in. -/

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
  split at h <;> try contradiction
  rename_i e hidx
  cases h
  -- the entry request is wherever `mixDriver` said it was
  obtain ⟨fd', afd', hfd', hafd', _, hbody⟩ :=
    specOK_all A ⟨funs, e⟩ reqs stepFuel (by simpa [generate] using hgenf) m e _
      (indexOfReq_spec hidx)
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

/-- `mix` produced `r` for source term `t`: whatever `r` yields, the source
yields the same.

Stated over the PREPARED form, not over `PRes.toCode`.  `toCode` is not what the
residual runs: a spine `prepare` left unbound costs the residual nothing, while
`toCode` rebuilds the whole `consP` chain and charges a unit of fuel per level,
so a `toCode`-gated clause can demand more fuel than `mr` -- and `mr` cannot be
raised, because `specSound_all` inducts downward and supplies `SOK` only BELOW
it.  The four `toCode` clauses survive as derived accessors, so every consumer
reads the same as before; what changed is that producers now discharge the
obligation the residual actually incurs. -/
def PResSound (A : AProgram) (Pr : Program) (mr : Nat) (ρr ρs : Env)
    (r : PRes) (t : ATerm) : Prop :=
  ∀ (ρp : Env) (d : Val),
    EvalLetsAt mr Pr ρr (prepare r).binds ρp →
    PValOK ρp (prepare r).value d →
    Eval (eraseProgram A) ρs (erase t) d

theorem EvalLetsAt_split {P : Program} {m : Nat} :
    ∀ (a : List Term) {b : List Term} {ρ ρ'' : Env},
      EvalLetsAt m P ρ (a ++ b) ρ'' →
      ∃ ρ', EvalLetsAt m P ρ a ρ' ∧ EvalLetsAt m P ρ' b ρ''
  | [],      _, _, _, h => ⟨_, .nil, by simpa using h⟩
  | _ :: es, _, _, _, h => by
      cases h with
      | cons he ht =>
          obtain ⟨ρ', h1, h2⟩ := EvalLetsAt_split es ht
          exact ⟨ρ', .cons he h1, h2⟩

/-- Bindings only ever PREPEND, so anything the caller could already see stays
where it was, `bs.length` deeper. -/
theorem EvalLetsAt_getElem {P : Program} {m : Nat} :
    ∀ {bs : List Term} {ρ ρ' : Env} (i : Nat),
      EvalLetsAt m P ρ bs ρ' → ρ'[bs.length + i]? = ρ[i]?
  | [],      _, _, _, h => by cases h; simp
  | _ :: es, ρ, _, i, h => by
      cases h with
      | cons _ ht =>
          have := EvalLetsAt_getElem (bs := es) (i + 1) ht
          simpa [Nat.add_comm, Nat.add_left_comm, Nat.add_assoc] using this

theorem EvalLetsAt_length {P : Program} {m : Nat} : ∀ {bs : List Term} {ρ ρ' : Env},
    EvalLetsAt m P ρ bs ρ' → ρ'.length = ρ.length + bs.length
  | [],      _, _, h => by cases h; simp
  | _ :: es, _, _, h => by
      cases h with
      | cons _ ht =>
          have h2 := EvalLetsAt_length (bs := es) ht
          simp only [List.length_cons] at h2 ⊢
          omega

theorem PValOK_functional {ρr : Env} : ∀ {pv : PVal} {u v : Val},
    PValOK ρr pv u → PValOK ρr pv v → u = v
  | .stat _,   _, _, hu, hv => hu.symm.trans hv
  | .dyn k,    u, v, hu, hv => by
      -- `PValOK` at a reference IS the lookup, but only definitionally
      have hu' : ρr[k]? = some u := hu
      have hv' : ρr[k]? = some v := hv
      rw [hu'] at hv'; exact Option.some.inj hv'
  | .cons _ _, _, _, hu, hv => by
      obtain ⟨_, _, hu1, ha,  hb⟩  := hu
      obtain ⟨_, _, hv1, ha', hb'⟩ := hv
      subst hu1; subst hv1
      rw [PValOK_functional ha ha', PValOK_functional hb hb']

/-! #### Reconstructing a prepared run

The soundness direction of the structural rules runs BACKWARDS through a peel:
it holds a run of the RETAINED component and has to produce a run of the whole
operand, because it is the operand that `PResSound` speaks about.

What makes that free is the discard guard itself.  A discarded component is
`total`, and a total result prepares to NO bindings, so putting the operand's
package back adds nothing to run: the reconstructed run ends in the very same
residual environment and at the very same fuel.  Its VALUE still has to come
from somewhere, and that is scopedness.  Computation-freeness and
in-scope-ness, both, exactly as the guard says -- and this is the first place
the two are used for different jobs in the same step. -/

theorem prepare_total_binds : ∀ {r : PRes}, r.total = true → (prepare r).binds = []
  | .stat _,            _ => rfl
  | .code (.var _),     _ => rfl
  | .code (.lit _),     h => by simp [PRes.total] at h
  | .code (.letIn _ _), h => by simp [PRes.total] at h
  | .code (.ite _ _ _), h => by simp [PRes.total] at h
  | .code (.prim _ _),  h => by simp [PRes.total] at h
  | .code (.ctorT _ _), h => by simp [PRes.total] at h
  | .code (.caseT _ _), h => by simp [PRes.total] at h
  | .code (.call _ _),  h => by simp [PRes.total] at h
  | .lets _ _,          h => by simp [PRes.total] at h
  | .cons a b,          h => by
      simp only [PRes.total, Bool.and_eq_true] at h
      have ha := prepare_total_binds h.1
      have hb := prepare_total_binds h.2
      cases hpa : prepare a with
      | mk abs av =>
        cases hpb : prepare b with
        | mk bbs bv =>
          rw [hpa] at ha
          rw [hpb] at hb
          simp only at ha hb
          subst ha
          subst hb
          simp [prepare, hpa, hpb]

theorem PValOK_shift1_inv {ρ : Env} {u : Val} : ∀ {pv : PVal} {w : Val},
    PValOK (u :: ρ) (PVal.shift 1 pv) w → PValOK ρ pv w
  | .stat _,   _, h => h
  | .dyn k,    _, h => by
      have h' : (u :: ρ)[k + 1]? = some _ := h
      show ρ[k]? = some _
      simpa using h'
  | .cons _ _, _, h => by
      obtain ⟨x, y, he, ha, hb⟩ := h
      exact ⟨x, y, he, PValOK_shift1_inv ha, PValOK_shift1_inv hb⟩

/-- The converse of `PValOK_EvalLets`: a shifted value denoted the same thing
before the bindings ran.  Used to pull a component's value back OUT from under
the other component's bindings. -/
theorem PValOK_EvalLets_inv {P : Program} :
    ∀ {ρ ρ' : Env} {bs : List Term} {pv : PVal} {w : Val},
      EvalLets P ρ bs ρ' → PValOK ρ' (PVal.shift bs.length pv) w → PValOK ρ pv w
  | _, _,  [],      _,  _, h, hv => by cases h; simpa using hv
  | _, ρ', e :: es, pv, w, h, hv => by
      cases h with
      | cons he ht =>
          have hstep : PValOK ρ' (PVal.shift es.length (PVal.shift 1 pv)) w := by
            rw [PVal.shift_add]
            simpa [Nat.add_comm] using hv
          exact PValOK_shift1_inv (PValOK_EvalLets_inv ht hstep)

/-- A prepared cons run splits into its components' runs.  Needed for `consP`,
where BOTH components are retained. -/
theorem prepare_cons_split {Pr : Program} {m : Nat} :
    ∀ {a b : PRes} {ρ ρp : Env} {d : Val},
      EvalLetsAt m Pr ρ (prepare (.cons a b)).binds ρp →
      PValOK ρp (prepare (.cons a b)).value d →
      ∃ x y ρa ρb,
        d = .cons x y ∧
        EvalLetsAt m Pr ρ (prepare a).binds ρa ∧ PValOK ρa (prepare a).value x ∧
        EvalLetsAt m Pr ρ (prepare b).binds ρb ∧ PValOK ρb (prepare b).value y := by
  intro a b ρ ρp d hl hv
  cases hpa : prepare a with
  | mk abs av =>
    cases hpb : prepare b with
    | mk bbs bv =>
      cases abs with
      | nil =>
          simp only [prepare, hpa, hpb] at hl hv
          obtain ⟨x, y, he, hx, hy⟩ := hv
          exact ⟨x, y, ρ, ρp, he, .nil,
                 PValOK_EvalLets_inv (EvalLetsAt_toEvalLets hl) hx, hl, hy⟩
      | cons ah at' =>
        cases bbs with
        | nil =>
            simp only [prepare, hpa, hpb] at hl hv
            obtain ⟨x, y, he, hx, hy⟩ := hv
            exact ⟨x, y, ρp, ρ, he, hl, hx, .nil,
                   PValOK_EvalLets_inv (EvalLetsAt_toEvalLets hl) hy⟩
        | cons bh bt =>
            -- the fallback: the whole cons was bound, so its `toCode` DID run
            -- and `prepare_peel` reads each component's package out of that run
            simp only [prepare, hpa, hpb] at hl hv
            cases hl with
            | cons he ht =>
              cases ht
              simp only [PValOK] at hv
              cases hv
              cases m with
              | zero => simp [PRes.toCode, evalFuel] at he
              | succ mq =>
                simp only [PRes.toCode, evalFuel] at he
                cases hx : evalFuel mq Pr ρ (PRes.toCode a) with
                | outOfFuel   => simp [evalFuelList, hx] at he
                | typeError _ => simp [evalFuelList, hx] at he
                | value x =>
                  cases hy : evalFuel mq Pr ρ (PRes.toCode b) with
                  | outOfFuel   => simp [evalFuelList, hx, hy] at he
                  | typeError _ => simp [evalFuelList, hx, hy] at he
                  | value y =>
                    simp only [evalFuelList, hx, hy, evalPrim] at he
                    cases he
                    obtain ⟨ρa, hla, hva⟩ := prepare_peel a mq ρ x hx
                    obtain ⟨ρb, hlb, hvb⟩ := prepare_peel b mq ρ y hy
                    rw [hpa] at hla hva
                    rw [hpb] at hlb hvb
                    exact ⟨x, y, ρa, ρb, rfl,
                           EvalLetsAt_mono (Nat.le_succ mq) hla, hva,
                           EvalLetsAt_mono (Nat.le_succ mq) hlb, hvb⟩

/-- Putting a cons back together when the component NOT given is total, and so
contributes no bindings: the run is unchanged, and only the discarded value has
to be supplied.  Both orientations, because `hd` discards the tail and `tl` the
head. -/
theorem prepare_cons_join_right {Pr : Program} {m : Nat} {a b : PRes} {ρ ρp : Env}
    {x y : Val}
    (hbt : b.total = true)
    (hla : EvalLetsAt m Pr ρ (prepare a).binds ρp)
    (hxa : PValOK ρp (prepare a).value x)
    (hyb : PValOK ρ (prepare b).value y) :
    EvalLetsAt m Pr ρ (prepare (.cons a b)).binds ρp ∧
      PValOK ρp (prepare (.cons a b)).value (.cons x y) := by
  have hb00 : (prepare b).binds = [] := prepare_total_binds hbt
  cases hpa : prepare a with
  | mk abs av =>
    cases hpb : prepare b with
    | mk bbs bv =>
      rw [hpa] at hla hxa
      rw [hpb] at hyb hb00
      have hb0 : bbs = [] := hb00
      subst hb0
      cases abs with
      | nil =>
          cases hla
          simp only [prepare, hpa, hpb]
          exact ⟨.nil, x, y, rfl, by simpa using hxa, hyb⟩
      | cons ah at' =>
          simp only [prepare, hpa, hpb]
          exact ⟨hla, x, y, rfl, hxa,
                 PValOK_EvalLets (EvalLetsAt_toEvalLets hla) hyb⟩

theorem prepare_cons_join_left {Pr : Program} {m : Nat} {a b : PRes} {ρ ρp : Env}
    {x y : Val}
    (hat : a.total = true)
    (hlb : EvalLetsAt m Pr ρ (prepare b).binds ρp)
    (hyb : PValOK ρp (prepare b).value y)
    (hxa : PValOK ρ (prepare a).value x) :
    EvalLetsAt m Pr ρ (prepare (.cons a b)).binds ρp ∧
      PValOK ρp (prepare (.cons a b)).value (.cons x y) := by
  have ha00 : (prepare a).binds = [] := prepare_total_binds hat
  cases hpa : prepare a with
  | mk abs av =>
    cases hpb : prepare b with
    | mk bbs bv =>
      rw [hpa] at hxa ha00
      rw [hpb] at hlb hyb
      have ha0 : abs = [] := ha00
      subst ha0
      simp only [prepare, hpa, hpb]
      exact ⟨hlb, x, y, rfl,
             PValOK_EvalLets (EvalLetsAt_toEvalLets hlb) hxa, hyb⟩

/-! #### Peels, reflected

Each peel, read backwards: from a run of the RETAINED result, a run of the whole
operand, in the SAME residual environment and at the same fuel.  That identity
of environments is the whole content -- it is what says the structural rule
costs the residual nothing. -/

theorem peelHd_run_inv {Pr : Program} {m : Nat} :
    ∀ {r r' : PRes} {ρ ρp : Env} {d : Val},
      peelHd r = some r' →
      PRes.Scoped ρ.length r →
      EvalLetsAt m Pr ρ (prepare r').binds ρp →
      PValOK ρp (prepare r').value d →
      ∃ y, EvalLetsAt m Pr ρ (prepare r).binds ρp ∧
           PValOK ρp (prepare r).value (.cons d y) := by
  intro r
  induction r with
  | stat w =>
      intro r' ρ ρp d hp _ _ _
      simp [peelHd] at hp
  | code c => intro r' ρ ρp d hp _ _ _; cases c <;> simp [peelHd] at hp
  | cons a b _ _ =>
      intro r' ρ ρp d hp hsc hl hv
      simp only [peelHd] at hp
      split at hp
      next hbt =>
        cases hp
        obtain ⟨_, hbsc⟩ := hsc
        obtain ⟨_, hbv⟩ := prepare_scoped hbsc
        have hb0 : (prepare b).binds = [] := prepare_total_binds hbt
        rw [hb0] at hbv
        obtain ⟨y, hy⟩ := PValOK_of_Scoped (ρ := ρ) (by simpa using hbv)
        obtain ⟨hjl, hjv⟩ := prepare_cons_join_right hbt hl hv hy
        exact ⟨y, hjl, hjv⟩
      next => simp at hp
  | lets bs rr ih =>
      intro r' ρ ρp d hp hsc hl hv
      simp only [peelHd, Option.map_eq_some_iff] at hp
      obtain ⟨a', hp', hr'⟩ := hp
      subst hr'
      obtain ⟨_, hrsc⟩ := hsc
      simp only [prepare] at hl hv
      obtain ⟨ρ1, hl1, hl2⟩ := EvalLetsAt_split bs hl
      have hlen : ρ1.length = ρ.length + bs.length := EvalLetsAt_length hl1
      obtain ⟨y, hj1, hj2⟩ := ih hp' (by rw [hlen]; exact hrsc) hl2 hv
      exact ⟨y, by simp only [prepare]; exact EvalLetsAt_append hl1 hj1,
             by simp only [prepare]; exact hj2⟩

theorem peelTl_run_inv {Pr : Program} {m : Nat} :
    ∀ {r r' : PRes} {ρ ρp : Env} {d : Val},
      peelTl r = some r' →
      PRes.Scoped ρ.length r →
      EvalLetsAt m Pr ρ (prepare r').binds ρp →
      PValOK ρp (prepare r').value d →
      ∃ x, EvalLetsAt m Pr ρ (prepare r).binds ρp ∧
           PValOK ρp (prepare r).value (.cons x d) := by
  intro r
  induction r with
  | stat w =>
      intro r' ρ ρp d hp _ _ _
      simp [peelTl] at hp
  | code c => intro r' ρ ρp d hp _ _ _; cases c <;> simp [peelTl] at hp
  | cons a b _ _ =>
      intro r' ρ ρp d hp hsc hl hv
      simp only [peelTl] at hp
      split at hp
      next hat =>
        cases hp
        obtain ⟨hasc, _⟩ := hsc
        obtain ⟨_, hav⟩ := prepare_scoped hasc
        have ha0 : (prepare a).binds = [] := prepare_total_binds hat
        rw [ha0] at hav
        obtain ⟨x, hx⟩ := PValOK_of_Scoped (ρ := ρ) (by simpa using hav)
        obtain ⟨hjl, hjv⟩ := prepare_cons_join_left hat hl hv hx
        exact ⟨x, hjl, hjv⟩
      next => simp at hp
  | lets bs rr ih =>
      intro r' ρ ρp d hp hsc hl hv
      simp only [peelTl, Option.map_eq_some_iff] at hp
      obtain ⟨a', hp', hr'⟩ := hp
      subst hr'
      obtain ⟨_, hrsc⟩ := hsc
      simp only [prepare] at hl hv
      obtain ⟨ρ1, hl1, hl2⟩ := EvalLetsAt_split bs hl
      have hlen : ρ1.length = ρ.length + bs.length := EvalLetsAt_length hl1
      obtain ⟨x, hj1, hj2⟩ := ih hp' (by rw [hlen]; exact hrsc) hl2 hv
      exact ⟨x, by simp only [prepare]; exact EvalLetsAt_append hl1 hj1,
             by simp only [prepare]; exact hj2⟩

theorem peelIsNil_run_inv {Pr : Program} {m : Nat} :
    ∀ {r r' : PRes} {ρ ρp : Env} {d : Val},
      peelIsNil r = some r' →
      PRes.Scoped ρ.length r →
      EvalLetsAt m Pr ρ (prepare r').binds ρp →
      PValOK ρp (prepare r').value d →
      ∃ u, EvalLetsAt m Pr ρ (prepare r).binds ρp ∧
           PValOK ρp (prepare r).value u ∧ evalPrim .isNil [u] = .ok d := by
  intro r
  induction r with
  | stat w =>
      intro r' ρ ρp d hp _ _ _
      simp [peelIsNil] at hp
  | code c => intro r' ρ ρp d hp _ _ _; cases c <;> simp [peelIsNil] at hp
  | cons a b _ _ =>
      intro r' ρ ρp d hp hsc hl hv
      simp only [peelIsNil] at hp
      split at hp
      next hab =>
        cases hp
        cases hl
        have hd : Val.bool false = d := hv
        subst hd
        simp only [Bool.and_eq_true] at hab
        obtain ⟨hat, hbt⟩ := hab
        obtain ⟨hasc, hbsc⟩ := hsc
        obtain ⟨_, hav⟩ := prepare_scoped hasc
        obtain ⟨_, hbv⟩ := prepare_scoped hbsc
        have ha0 : (prepare a).binds = [] := prepare_total_binds hat
        have hb0 : (prepare b).binds = [] := prepare_total_binds hbt
        rw [ha0] at hav
        rw [hb0] at hbv
        obtain ⟨x, hx⟩ := PValOK_of_Scoped (ρ := ρ) (by simpa using hav)
        obtain ⟨y, hy⟩ := PValOK_of_Scoped (ρ := ρ) (by simpa using hbv)
        have hla : EvalLetsAt m Pr ρ (prepare a).binds ρ := by rw [ha0]; exact .nil
        obtain ⟨hjl, hjv⟩ := prepare_cons_join_right hbt hla hx hy
        exact ⟨.cons x y, hjl, hjv, rfl⟩
      next => simp at hp
  | lets bs rr ih =>
      intro r' ρ ρp d hp hsc hl hv
      simp only [peelIsNil, Option.map_eq_some_iff] at hp
      obtain ⟨a', hp', hr'⟩ := hp
      subst hr'
      obtain ⟨_, hrsc⟩ := hsc
      simp only [prepare] at hl hv
      obtain ⟨ρ1, hl1, hl2⟩ := EvalLetsAt_split bs hl
      have hlen : ρ1.length = ρ.length + bs.length := EvalLetsAt_length hl1
      obtain ⟨u, hj1, hj2, hj3⟩ := ih hp' (by rw [hlen]; exact hrsc) hl2 hv
      exact ⟨u, by simp only [prepare]; exact EvalLetsAt_append hl1 hj1,
             by simp only [prepare]; exact hj2, hj3⟩

/-! Accessors named to match `PResOK`'s, so `h.statEq` / `h.codeEval` read the
same in both directions and dot notation picks the right one by type.  They are
theorems now rather than projections; `prepare_peel` is what recovers them. -/

theorem PResSound.statEq {A Pr mr ρr ρs r t} (h : PResSound A Pr mr ρr ρs r t) :
    ∀ w, r = .stat w → Eval (eraseProgram A) ρs (erase t) w := by
  intro w hw; subst hw; exact h ρr w .nil rfl

theorem PResSound.codeEval {A Pr mr ρr ρs r t} (h : PResSound A Pr mr ρr ρs r t) :
    ∀ c v, r = .code c → evalFuel mr Pr ρr c = .value v →
      Eval (eraseProgram A) ρs (erase t) v := by
  intro c v hcd hev; subst hcd
  obtain ⟨ρp, hl, hval⟩ := prepare_peel (.code c) mr ρr v hev
  exact h ρp v hl hval

theorem PResSound.consEval {A Pr mr ρr ρs r t} (h : PResSound A Pr mr ρr ρs r t) :
    ∀ a b v, r = .cons a b → evalFuel mr Pr ρr (PRes.cons a b).toCode = .value v →
      Eval (eraseProgram A) ρs (erase t) v := by
  intro a b v hcd hev; subst hcd
  obtain ⟨ρp, hl, hval⟩ := prepare_peel _ mr ρr v hev
  exact h ρp v hl hval

theorem PResSound.letsEval {A Pr mr ρr ρs r t} (h : PResSound A Pr mr ρr ρs r t) :
    ∀ bs r' v, r = .lets bs r' → evalFuel mr Pr ρr (PRes.lets bs r').toCode = .value v →
      Eval (eraseProgram A) ρs (erase t) v := by
  intro bs r' v hcd hev; subst hcd
  obtain ⟨ρp, hl, hval⟩ := prepare_peel _ mr ρr v hev
  exact h ρp v hl hval

theorem PResSound_toCode {A Pr mr ρr ρs r t w}
    (h : PResSound A Pr mr ρr ρs r t)
    (hev : evalFuel mr Pr ρr r.toCode = .value w) :
    Eval (eraseProgram A) ρs (erase t) w := by
  obtain ⟨ρp, hl, hval⟩ := prepare_peel r mr ρr w hev
  exact h ρp w hl hval

/-! Introduction rules, one per result shape `mix` can return.  Each reduces the
prepared obligation back to the ordinary one for that shape, so the cases of
`mixTerm_sound` read as they did. -/

theorem PResSound_of_stat {A Pr mr ρr ρs t} {w : Val}
    (h : ∀ u, w = u → Eval (eraseProgram A) ρs (erase t) u) :
    PResSound A Pr mr ρr ρs (.stat w) t := fun _ d _ hval => h d hval

/-- A result already in partial-value form carries no computation, so there is
no binding to run and the obligation is exactly `PValOK`.  This is the rule the
`var` cases use, including the one that reads a preserved spine back. -/
theorem PResSound_of_toPRes {A Pr mr ρr ρs t} {pv : PVal}
    (h : ∀ u, PValOK ρr pv u → Eval (eraseProgram A) ρs (erase t) u) :
    PResSound A Pr mr ρr ρs pv.toPRes t := by
  intro ρp d hl hval
  rw [prepare_toPRes] at hl hval
  cases hl
  exact h d hval

/-- Arbitrary residual code.  The `var` exclusion is real: `prepare` binds code
exactly when it is not already a reference, and a reference carries no binding
whose evaluation could supply the value, so that shape goes through
`PResSound_of_toPRes` instead. -/
theorem PResSound_of_code {A Pr mr ρr ρs t} {c : Term} (hnv : ∀ i, c ≠ .var i)
    (h : ∀ v, evalFuel mr Pr ρr c = .value v → Eval (eraseProgram A) ρs (erase t) v) :
    PResSound A Pr mr ρr ρs (.code c) t := by
  intro ρp d hl hval
  have hpc : prepare (.code c) = ⟨[c], .dyn 0⟩ := by
    cases c with
    | var i => exact absurd rfl (hnv i)
    | lit _ | letIn _ _ | ite _ _ _ | prim _ _ | ctorT _ _ | caseT _ _ | call _ _ => rfl
  rw [hpc] at hl hval
  cases hl with
  | cons he ht =>
      cases ht
      simp only [PValOK] at hval
      cases hval
      exact h _ he

/-- A package: its own bindings run first, then the inner result's. -/
theorem PResSound_of_lets {A Pr mr ρr ρs t} {bs : List Term} {rb : PRes}
    (h : ∀ ρ1 ρp d, EvalLetsAt mr Pr ρr bs ρ1 →
           EvalLetsAt mr Pr ρ1 (prepare rb).binds ρp →
           PValOK ρp (prepare rb).value d →
           Eval (eraseProgram A) ρs (erase t) d) :
    PResSound A Pr mr ρr ρs (.lets bs rb) t := by
  intro ρp d hl hval
  simp only [prepare] at hl hval
  obtain ⟨ρ1, h1, h2⟩ := EvalLetsAt_split bs hl
  exact h ρ1 ρp d h1 h2 hval

/-- The one-binding package a dynamic `letIn` returns. -/
theorem PResSound_of_let1 {A Pr mr ρr ρs t} {e : Term} {rb : PRes}
    (h : ∀ d ρp u, evalFuel mr Pr ρr e = .value d →
           EvalLetsAt mr Pr (d :: ρr) (prepare rb).binds ρp →
           PValOK ρp (prepare rb).value u →
           Eval (eraseProgram A) ρs (erase t) u) :
    PResSound A Pr mr ρr ρs (.lets [e] rb) t := by
  refine PResSound_of_lets (fun _ ρp d hl1 hl2 hval => ?_)
  cases hl1 with
  | cons he ht => cases ht; exact h _ ρp d he hl2 hval

/-- Pointwise `PResSound` over an operand list -- what a structural primitive
answer has to consult, since it does not evaluate `rs.map PRes.toCode`. -/
abbrev PResSoundAll (A : AProgram) (Pr : Program) (mr : Nat) (ρr ρs : Env) :
    List PRes → List ATerm → Prop :=
  Forall₂ (PResSound A Pr mr ρr ρs)

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
      | cons _ _ => simp [allStatic] at hst
      | lets _ _ => simp [allStatic] at hst
      | stat u =>
          simp only [allStatic] at hst
          split at hst <;> try contradiction
          rename_i us hus
          cases hst
          exact .cons ((h Δ env t (.stat u) rq₁ ρr ρs hc ht).statEq u rfl)
                      (ih rs' rq₂ ρr ρs us hc hts hus)

/-- Pointwise soundness for an operand list: each operand result is sound for
its own source term.  This is the primitive fact; everything else about operand
lists is derived from it rather than re-inducted. -/
theorem mixTerms_sound_all {A Pr reqs n mr} (h : SOK A Pr reqs n mr) :
    ∀ (Δ : Div) (env : PEnv) (ts : List ATerm) (rs : List PRes) (rq : List SpecRequest)
      (ρr ρs : Env),
      Compat ρr Δ env ρs →
      mixTerms n A (indexOfReq reqs) Δ env ts = .ok (rs, rq) →
      PResSoundAll A Pr mr ρr ρs rs ts := by
  intro Δ env ts
  induction ts with
  | nil =>
      intro rs rq ρr ρs _ hmix
      simp only [mixTerms] at hmix
      cases hmix
      exact .nil
  | cons t ts ih =>
      intro rs rq ρr ρs hc hmix
      simp only [mixTerms] at hmix
      split at hmix <;> try contradiction
      rename_i r rq₁ rs' rq₂ ht hts
      cases hmix
      exact .cons (h Δ env t r rq₁ ρr ρs hc ht) (ih rs' rq₂ ρr ρs hc hts)

/-- Reading a pointwise-sound operand list through `toCode`, which is what the
residualizing FALLBACK does run. -/
theorem PResSoundAll_evalFuelList {A Pr mr ρr ρs} :
    ∀ {rs : List PRes} {ts : List ATerm} {ds : List Val},
      PResSoundAll A Pr mr ρr ρs rs ts →
      evalFuelList mr Pr ρr (rs.map PRes.toCode) = .inl ds →
      EvalList (eraseProgram A) ρs (eraseList ts) ds := by
  intro rs
  induction rs with
  | nil =>
      intro ts ds hall hev
      cases hall
      simp only [List.map_nil, evalFuelList] at hev
      cases hev
      simp only [eraseList]
      exact .nil
  | cons r rs' ih =>
      intro ts ds hall hev
      cases hall with
      | cons hr hrest =>
        simp only [List.map_cons, evalFuelList] at hev
        split at hev <;> try contradiction
        rename_i d hd
        split at hev <;> try contradiction
        rename_i ds' hds
        cases hev
        simp only [eraseList]
        exact .cons (PResSound_toCode hr hd) (ih hrest hds)

theorem mixTerms_sound_code {A Pr reqs n mr} (h : SOK A Pr reqs n mr) :
    ∀ (Δ : Div) (env : PEnv) (ts : List ATerm) (rs : List PRes) (rq : List SpecRequest)
      (ρr ρs : Env) (ds : List Val),
      Compat ρr Δ env ρs →
      mixTerms n A (indexOfReq reqs) Δ env ts = .ok (rs, rq) →
      evalFuelList mr Pr ρr (rs.map PRes.toCode) = .inl ds →
      EvalList (eraseProgram A) ρs (eraseList ts) ds :=
  fun Δ env ts rs rq ρr ρs ds hc hmix hev =>
    PResSoundAll_evalFuelList (mixTerms_sound_all h Δ env ts rs rq ρr ρs hc hmix) hev

/-! ## Structural answers, backwards

The mirror of `primStruct_ok`.  Here the guard earns its keep: a discarded
component has to be shown not to have mattered, and that is two separate facts.
It is `total`, so it contributed no bindings and the operand's run is the
retained one unchanged (`peel*_run_inv`); and it is in SCOPE, so it denotes a
value at all, which is what lets the operand's source term be given a value to
agree with. -/

theorem peelHd_sound {A Pr mr ρr ρs} {r₀ r : PRes} {t : ATerm} {ρp : Env} {d : Val}
    (hp : peelHd r₀ = some r)
    (hs : PResSound A Pr mr ρr ρs r₀ t)
    (hsc : PRes.Scoped ρr.length r₀)
    (hl : EvalLetsAt mr Pr ρr (prepare r).binds ρp)
    (hv : PValOK ρp (prepare r).value d) :
    Eval (eraseProgram A) ρs (.prim .hd [erase t]) d := by
  obtain ⟨y, hjl, hjv⟩ := peelHd_run_inv hp hsc hl hv
  exact .prim (.cons (hs ρp (.cons d y) hjl hjv) .nil) rfl

theorem peelTl_sound {A Pr mr ρr ρs} {r₀ r : PRes} {t : ATerm} {ρp : Env} {d : Val}
    (hp : peelTl r₀ = some r)
    (hs : PResSound A Pr mr ρr ρs r₀ t)
    (hsc : PRes.Scoped ρr.length r₀)
    (hl : EvalLetsAt mr Pr ρr (prepare r).binds ρp)
    (hv : PValOK ρp (prepare r).value d) :
    Eval (eraseProgram A) ρs (.prim .tl [erase t]) d := by
  obtain ⟨x, hjl, hjv⟩ := peelTl_run_inv hp hsc hl hv
  exact .prim (.cons (hs ρp (.cons x d) hjl hjv) .nil) rfl

theorem peelIsNil_sound {A Pr mr ρr ρs} {r₀ r : PRes} {t : ATerm} {ρp : Env} {d : Val}
    (hp : peelIsNil r₀ = some r)
    (hs : PResSound A Pr mr ρr ρs r₀ t)
    (hsc : PRes.Scoped ρr.length r₀)
    (hl : EvalLetsAt mr Pr ρr (prepare r).binds ρp)
    (hv : PValOK ρp (prepare r).value d) :
    Eval (eraseProgram A) ρs (.prim .isNil [erase t]) d := by
  obtain ⟨u, hjl, hjv, hpr⟩ := peelIsNil_run_inv hp hsc hl hv
  exact .prim (.cons (hs ρp u hjl hjv) .nil) hpr

theorem primStruct_sound {A Pr mr ρr ρs} {p : Prim} {rs : List PRes} {ts : List ATerm}
    {r : PRes} {ρp : Env} {d : Val}
    (hps : primStruct p rs = some r)
    (hall : PResSoundAll A Pr mr ρr ρs rs ts)
    (hsc : PRes.ScopedList ρr.length rs)
    (hl : EvalLetsAt mr Pr ρr (prepare r).binds ρp)
    (hv : PValOK ρp (prepare r).value d) :
    Eval (eraseProgram A) ρs (.prim p (eraseList ts)) d := by
  cases p with
  | addI | subI | mulI | divI | modI | ltI | leI | eqI | andB | orB | notB
  | eqV | mkCtorP | ctorTagP | ctorFieldsP | bvMk | bvWidth | bvUint | bvBit
  | bvAnd | bvOr | bvXor | bvNot | bvResize
  | bvSra | bvGetMask | bvSint | bvShl => simp [primStruct] at hps
  | consP =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons ra rest =>
        cases rest with
        | nil => simp [primStruct] at hps
        | cons rb rest2 =>
          cases rest2 with
          | cons _ _ => simp [primStruct] at hps
          | nil =>
            cases hall with
            | cons hA ht =>
              cases ht with
              | cons hB hn =>
                cases hn
                simp only [eraseList]
                cases ra with
                | stat a =>
                  cases rb with
                  | stat b =>
                      -- both static: the answer is a VALUE and each operand
                      -- denotes its own half with no bindings at all
                      simp only [primStruct] at hps
                      cases hps
                      have hd : Val.cons a b = d := hv
                      subst hd
                      exact .prim (.cons (hA ρr a .nil rfl)
                                    (.cons (hB ρr b .nil rfl) .nil)) rfl
                  | code _ | cons _ _ | lets _ _ =>
                      simp only [primStruct] at hps
                      cases hps
                      obtain ⟨x, y, ρa, ρb, he, hla, hxa, hlb, hyb⟩ :=
                        prepare_cons_split hl hv
                      subst he
                      exact .prim (.cons (hA ρa x hla hxa)
                                    (.cons (hB ρb y hlb hyb) .nil)) rfl
                | code _ | cons _ _ | lets _ _ =>
                    simp only [primStruct] at hps
                    cases hps
                    obtain ⟨x, y, ρa, ρb, he, hla, hxa, hlb, hyb⟩ :=
                      prepare_cons_split hl hv
                    subst he
                    exact .prim (.cons (hA ρa x hla hxa)
                                  (.cons (hB ρb y hlb hyb) .nil)) rfl
  | hd =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons r₀ rest =>
        cases rest with
        | cons _ _ => simp [primStruct] at hps
        | nil =>
          cases hall with
          | cons hA ht =>
            cases ht
            simp only [primStruct] at hps
            simp only [eraseList]
            exact peelHd_sound hps hA hsc.1 hl hv
  | tl =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons r₀ rest =>
        cases rest with
        | cons _ _ => simp [primStruct] at hps
        | nil =>
          cases hall with
          | cons hA ht =>
            cases ht
            simp only [primStruct] at hps
            simp only [eraseList]
            exact peelTl_sound hps hA hsc.1 hl hv
  | isNil =>
      cases rs with
      | nil => simp [primStruct] at hps
      | cons r₀ rest =>
        cases rest with
        | cons _ _ => simp [primStruct] at hps
        | nil =>
          cases hall with
          | cons hA ht =>
            cases ht
            simp only [primStruct] at hps
            simp only [eraseList]
            exact peelIsNil_sound hps hA hsc.1 hl hv

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
            | cons _ _ => simp [splitArgs] at hsp
            | lets _ _ => simp [splitArgs] at hsp
            | stat w =>
                simp only [splitArgs] at hsp
                split at hsp <;> try contradiction
                rename_i svs' dts' hrec
                cases hsp
                obtain ⟨vs, hsrc, hel, hlen⟩ :=
                  ih rs' rq₂ ρr ρs bs svs' _ ds hc hts hrec hev
                exact ⟨w :: vs, by simp [srcArgs, hsrc],
                       .cons (hr.statEq w rfl) hel, by simp [hlen]⟩
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

/-! ## One-pass argument transfer, backwards

The soundness counterpart of `mixPArgs_ok`: the residual environment is whatever
running the bindings reached, and neither the binding COUNT nor its structure
appears.

One thing is genuinely new here.  The transfer this replaced bound EVERY dynamic
argument, so the residual evaluation handed the proof each argument's value.  `mixPArgs`
binds only what carries computation, so for a prepared spine there is no
evaluation to read a value off -- it has to be CONSTRUCTED, and that is exactly
what scopedness buys (`PValOK_of_Scoped`).  This is the second half of the
discard guard: `PRes.total` says nothing is lost by not running the leaf,
scopedness says the leaf resolves.  Neither implies the other, which is why
`mixPArgs_scoped` had to come first. -/

theorem mixPArgs_sound {A Pr reqs n mr} (hsok : SOK A Pr reqs n mr) :
    ∀ (ps : Div) (ts : List ATerm) (Δ : Div) (env : PEnv) (bs : List Term)
      (env' : PEnv) (rq : List SpecRequest) (ρr ρs ρ1 : Env),
      Compat ρr Δ env ρs →
      mixPArgs n A (indexOfReq reqs) Δ env ps ts = .ok (bs, env', rq) →
      EvalLetsAt mr Pr ρr bs ρ1 →
      ∃ vs : List Val,
        Compat ρ1 ps env' vs ∧
        EvalList (eraseProgram A) ρs (eraseList ts) vs ∧
        vs.length = ps.length := by
  intro ps
  induction ps with
  | nil =>
      intro ts Δ env bs env' rq ρr ρs ρ1 _ hmix hl
      cases ts with
      | nil =>
          simp only [mixPArgs] at hmix
          cases hmix
          cases hl
          exact ⟨[], .nil, .nil, rfl⟩
      | cons _ _ => simp [mixPArgs] at hmix
  | cons b ps' ih =>
      intro ts Δ env bs env' rq ρr ρs ρ1 hc hmix hl
      cases ts with
      | nil => cases b <;> simp [mixPArgs] at hmix
      | cons t ts' =>
        cases b with
        | stat =>
            simp only [mixPArgs] at hmix
            cases hres : mixTerm n A (indexOfReq reqs) Δ env t with
            | error z => simp [hres] at hmix
            | ok pr =>
              obtain ⟨r, rq₁⟩ := pr
              have hr := hsok Δ env t r rq₁ ρr ρs hc hres
              simp only [hres] at hmix
              cases r with
              | code _ => simp at hmix
              | cons _ _ => simp at hmix
              | lets _ _ => simp at hmix
              | stat w =>
                cases hrec : mixPArgs n A (indexOfReq reqs) Δ env ps' ts' with
                | error z => simp [hrec] at hmix
                | ok q =>
                  obtain ⟨bs', env'', rq₂⟩ := q
                  obtain ⟨vs, hcp, hel, hvl⟩ :=
                    ih ts' Δ env bs' env'' rq₂ ρr ρs ρ1 hc hrec
                      (by simp only [hrec] at hmix; cases hmix; exact hl)
                  simp only [hrec] at hmix
                  cases hmix
                  exact ⟨w :: vs, hcp.stat, .cons (hr.statEq w rfl) hel, by simp [hvl]⟩
        | dyn =>
            simp only [mixPArgs] at hmix
            cases hres : mixTerm n A (indexOfReq reqs) Δ env t with
            | error z => simp [hres] at hmix
            | ok pr =>
              obtain ⟨r, rq₁⟩ := pr
              have hr := hsok Δ env t r rq₁ ρr ρs hc hres
              simp only [hres] at hmix
              cases hrec : mixPArgs n A (indexOfReq reqs) Δ
                  (env.shiftBy (prepare r).binds.length) ps' ts' with
              | error z => simp [hrec] at hmix
              | ok q =>
                obtain ⟨bs', env'', rq₂⟩ := q
                simp only [hrec] at hmix
                cases hmix
                obtain ⟨ρp, hl1, hl2⟩ := EvalLetsAt_split (prepare r).binds hl
                have hrsc : PRes.Scoped ρr.length r :=
                  mixTerm_scoped n A (indexOfReq reqs) Δ env t r rq₁ ρr.length
                    (Compat_Scoped hc) hres
                obtain ⟨_, hvsc⟩ := prepare_scoped hrsc
                have hplen : ρp.length = ρr.length + (prepare r).binds.length :=
                  EvalLetsAt_length hl1
                obtain ⟨d, hd⟩ := PValOK_of_Scoped (ρ := ρp) (by rw [hplen]; exact hvsc)
                obtain ⟨vs, hcp, hel, hvl⟩ :=
                  ih ts' Δ (env.shiftBy (prepare r).binds.length) bs' env'' rq₂
                     ρp ρs ρ1 (Compat_after_EvalLets (EvalLetsAt_toEvalLets hl1) hc) hrec hl2
                refine ⟨d :: vs, ?_, .cons (hr ρp d hl1 hd) hel, by simp [hvl]⟩
                exact .dyn (PValOK_EvalLets (EvalLetsAt_toEvalLets hl2) hd) hcp

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
          exact PResSound_of_stat (fun _ hw => by cases hw; exact .lit)

      | var i =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          · rename_i w _ henv
            cases hmix
            exact PResSound_of_stat
              (fun _ hw => by cases hw; exact .var (Compat_stat_lookup hc i w henv))
          · rename_i k _ henv
            cases hmix
            -- a bare reference: no binding is emitted, so the obligation is
            -- `PValOK` and `Compat` answers it outright
            refine PResSound_of_toPRes (pv := .dyn k) (fun u hval => ?_)
            obtain ⟨u', hs, hr⟩ := Compat_dyn_lookup hc i k henv
            cases PValOK_functional hval hr
            exact .var hs
          · -- a preserved spine, read back structurally
            rename_i a b hdiv henv
            cases hmix
            refine PResSound_of_toPRes (pv := .cons a b) (fun u hval => ?_)
            obtain ⟨u', hs, hp⟩ := Compat_pval_lookup hc i (.cons a b) henv hdiv
            cases PValOK_functional hval hp
            exact .var hs

      | lift e =>
          simp only [mixTerm] at hmix
          split at hmix <;> try contradiction
          rename_i w rq' he
          cases hmix
          refine PResSound_of_code (by intro i; simp) (fun v hev => ?_)
          cases mr with
          | zero => simp [evalFuel] at hev
          | succ mq =>
              simp only [evalFuel] at hev
              cases hev
              exact (ihm Δ env e (.stat w) _ ρr ρs hc he).statEq w rfl

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
            -- a static binding emits no residual binder, so whatever shape the
            -- BODY came back as, the source `letIn` agrees with it the same way
            -- the result IS the body's, whatever shape that was, so the
            -- obligation transfers verbatim
            exact fun ρp d hl hval => .letIn (hre.statEq w rfl) (hb ρp d hl hval)
          · -- dynamic binding
            rename_i _
            split at hmix <;> try contradiction
            rename_i rb rq₂ _hne hbody
            cases hmix
            -- the result is now a PACKAGE, so it is the `lets` component that
            -- carries the content and the `code` one that is vacuous
            refine PResSound_of_let1 (fun d ρp u hd hl2 hval => ?_)
            have hcb : Compat (d :: ρr) (.dyn :: Δ) (.dyn 0 :: env.shiftBy 1) (d :: ρs) :=
              .dyn (pv := .dyn 0) (by simp [PValOK]) (Compat_shift1 d hc)
            have hb := ihm _ _ body rb rq₂ (d :: ρr) (d :: ρs) hcb hbody
            exact .letIn (PResSound_toCode hre hd) (hb ρp u hl2 hval)

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
            exact PResSound_of_stat (fun _ hw => by
              cases hw
              exact .prim (mixTerms_sound_stat ihm Δ env ts rs _ ρr ρs ws hc hts hws) hp)
          · split at hmix
            · -- a guarded structural answer: the operands are consulted
              -- POINTWISE, since the residual does not evaluate their `toCode`
              rename_i r' hstruct
              cases hmix
              intro ρp d hl hval
              have hall := mixTerms_sound_all ihm Δ env ts rs _ ρr ρs hc hts
              have hsc : PRes.ScopedList ρr.length rs :=
                mixTerms_scoped (mixTerm_scoped n) A (indexOfReq reqs) Δ env ts rs _
                  ρr.length (Compat_Scoped hc) hts
              simp only [erase]
              exact primStruct_sound hstruct hall hsc hl hval
            · cases hmix
              refine PResSound_of_code (by intro i; simp) (fun v hev => ?_)
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
            exact PResSound_of_stat (fun _ hw => by
              cases hw
              exact .ctorT (mixTerms_sound_stat ihm Δ env ts rs _ ρr ρs ws hc hts hws))
          · cases hmix
            refine PResSound_of_code (by intro i; simp) (fun v hev => ?_)
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
            exact fun ρp d hl hval => .iteT (hrc.statEq _ rfl) (hb ρp d hl hval)
          · rename_i _
            split at hmix <;> try contradiction
            rename_i re' rq₂ he
            cases hmix
            have hb := ihm Δ env e r _ ρr ρs hc he
            exact fun ρp d hl hval => .iteF (hrc.statEq _ rfl) (hb ρp d hl hval)
          · rename_i _
            split at hmix <;> try contradiction
            rename_i ra rq₂ re' rq₃ ha he
            cases hmix
            refine PResSound_of_code (by intro i; simp) (fun v hev => ?_)
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
            exact fun ρp d hl hval =>
              .caseT (hs.statEq _ rfl) hfa (by simpa [Alt.arity] using har)
                (by simpa [Alt.body] using hb ρp d hl hval)
          · rename_i _
            split at hmix <;> try contradiction
            rename_i alts' rq₂ halts
            cases hmix
            refine PResSound_of_code (by intro i; simp) (fun v hev => ?_)
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
            exact fun ρp d hl hval => .call hel (eraseProgram_fn hfn)
              (by simpa [eraseFunDef] using hwl)
              (by simpa [eraseFunDef] using hb ρp d hl hval)
          · split at hmix <;> try contradiction
            rename_i svs dts hsplit
            split at hmix <;> try contradiction
            rename_i k hidx
            cases hmix
            refine PResSound_of_code (by intro i; simp) (fun v hev => ?_)
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
            exact fun ρp d hl hval => .call hel (eraseProgram_fn hfn)
              (by simpa [eraseFunDef] using hwl)
              (by simpa [eraseFunDef] using hb ρp d hl hval)
          · split at hmix <;> try contradiction
            rename_i bs env' rq₂ hua
            split at hmix <;> try contradiction
            rename_i rb rq₃ _hne hbody
            cases hmix
            -- an unfolded call also returns a PACKAGE now
            refine PResSound_of_lets (fun ρ1 ρp d hl1 hl2 hval => ?_)
            obtain ⟨vs, hcp, hel, hvl⟩ :=
              mixPArgs_sound ihm fd.params ts Δ env bs env' rq₂ ρr ρs ρ1 hc hua hl1
            have hb := ihm _ _ fd.body rb rq₃ ρ1 vs hcp hbody
            exact .call hel (eraseProgram_fn hfn) (by simpa [eraseFunDef] using hvl.symm)
              (by simpa [eraseFunDef] using hb ρp d hl2 hval)

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
  split at h <;> try contradiction
  rename_i e hidx
  cases h
  obtain ⟨fd', afd', hfd', hafd', hbody⟩ :=
    specSound_all A ⟨funs, e⟩ reqs stepFuel (by simpa [generate] using hgenf) mr e _
      (indexOfReq_spec hidx)
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

/-- **`mix_sound`**, in the form the plan states it: an iff between two
denotations, with no fuel anywhere.

`evalFuel_complete` is what bridges the gap -- each half of the proof is indexed
by a different fuel, and this turns "terminates at some fuel" back into `Eval`
on both sides.

`srcArgs` is the combination: it interleaves the static arguments `mix` was
given back with the dynamic ones the residual is called with, in the order the
callee's division prescribes. -/
theorem mixDriver_iff {stepFuel wlFuel : Nat} {A : AProgram} {statics : List Val}
    {Pr : Program} (h : mixDriver stepFuel wlFuel A statics = .ok Pr)
    (ds ρs : List Val) (v : Val) (fd : FunDef) (afd : AFunDef)
    (hpf : Pr.fn Pr.entry = some fd) (haf : A.fn A.entry = some afd)
    (hsa : srcArgs afd.params statics ds = some ρs) :
    Eval Pr ds fd.body v ↔ Eval (eraseProgram A) ρs (erase afd.body) v := by
  constructor
  · intro hr
    obtain ⟨mr, hmr⟩ := evalFuel_complete hr
    exact mixDriver_complete h mr ds ρs v fd afd hpf haf hsa hmr
  · intro hsrc
    obtain ⟨m, hm⟩ := evalFuel_complete hsrc
    exact mixDriver_sound h m ds ρs v fd afd hpf haf hsa hm

end Projection
