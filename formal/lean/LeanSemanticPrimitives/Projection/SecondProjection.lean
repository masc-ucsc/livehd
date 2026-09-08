/-
  The second projection, as a theorem rather than a check.

  `Gate0.lean` demonstrates `mix(mix, interp)` on one interpreter and one source
  program.  This says it for ALL of them.

  THE KEY OBSERVATION is that the second projection's correctness does not need
  the object specializer to be a faithful transcription of the Lean one.  It
  needs only `mix_sound` and `bta_erases`:

    * `mixDriver_iff` says the residual computes what `eraseProgram A_M`
      computes on the combined inputs;
    * `bta_erases` says `eraseProgram A_M` IS `mixProgram`, because annotation
      does not change the program.

  So the derived compiler, run on a design, computes exactly what `mixProgram`
  computes on (interpreter, design) -- which is the second projection.

  WHAT THIS DOES NOT SAY, and what `mixProgram_implements_mixHost` would add, is
  that `mixProgram` computes what the LEAN specializer computes.  That is a
  separate and much larger obligation: it relates a 53-function object program
  to a Lean definition, function by function.  Until it is proved, `Gate0`'s
  `objP == Demo.residual2P` remains a check, and the theorem below is about
  `mixProgram` on its own terms.
-/

import LeanSemanticPrimitives.Projection.PartialEvaluatorCorrect
import LeanSemanticPrimitives.Projection.BTA

namespace Projection

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

/-! ## The second projection -/

/-- Running the DERIVED COMPILER on a design computes exactly what running the
specializer on (interpreter, design) computes.

`hera` is discharged by `bta_erases`: the annotated specializer erases to the
specializer.  Without it the theorem would relate the compiler to an annotated
program rather than to a program. -/
theorem secondProjection_correct
    {A_M : AProgram} {mixP : Program} (hera : eraseProgram A_M = mixP)
    {A_I : Val} {sf wf : Nat} {compilerP : Program}
    (hc : mixDriver sf wf A_M [A_I] = .ok compilerP)
    {fd : FunDef} {afd : AFunDef}
    (hpf : compilerP.fn compilerP.entry = some fd)
    (haf : A_M.fn A_M.entry = some afd)
    (hparams : afd.params = [.stat, .dyn])
    (D v : Val) :
    Eval compilerP [D] fd.body v ↔ Eval mixP [A_I, D] (erase afd.body) v := by
  have hsa : srcArgs afd.params [A_I] [D] = some [A_I, D] := by
    rw [hparams]; simp [srcArgs]
  have h := mixDriver_iff hc [D] [A_I, D] v fd afd hpf haf hsa
  rw [hera] at h
  exact h

/-- The same statement in call form, which is how `Gate0` runs it. -/
theorem secondProjection_correct_call
    {A_M : AProgram} {mixP : Program} (hera : eraseProgram A_M = mixP)
    {A_I : Val} {sf wf : Nat} {compilerP : Program}
    (hc : mixDriver sf wf A_M [A_I] = .ok compilerP)
    {fd : FunDef} {afd : AFunDef} {mfd : FunDef}
    (hpf : compilerP.fn compilerP.entry = some fd)
    (hfa : fd.arity = 1)
    (haf : A_M.fn A_M.entry = some afd)
    (hparams : afd.params = [.stat, .dyn])
    (hmf : mixP.fn mixP.entry = some mfd)
    (hmb : mfd.body = erase afd.body) (hma : mfd.arity = 2)
    (D v : Val) :
    Eval compilerP [] (.call compilerP.entry [.lit D]) v ↔
    Eval mixP [] (.call mixP.entry [.lit A_I, .lit D]) v := by
  have h := secondProjection_correct hera hc hpf haf hparams D v
  have e1 := Eval_entry (P := compilerP) (args := [D]) (v := v) hpf (by simp [hfa])
  have e2 := Eval_entry (P := mixP) (args := [A_I, D]) (v := v) hmf (by simp [hma])
  rw [hmb] at e2
  simpa using e1.symm.trans (h.trans e2)

end Projection
