/-
# DIAGNOSTIC interpreter variant -- NOT the verified path.

Section 7 of `certio/PHASE6_PERF.md` establishes, by measurement, that walking
the NODE portion of `I_hw`'s slot environment is free while walking the SOURCE
portion costs one let-bound `tl` per level.  It records as a HYPOTHESIS, not a
finding, that the difference is `env0`'s own `letN` in `main`'s binding block
(`HardwareInterpreter.lean:115`): a dynamic `letIn` reifies its body
(`PartialEvaluator.lean:326`), whereas the node spine is the RESULT of the
unfolded `evalNodes` call and survives as a `PVal.cons`.

This module runs the A/B that settles it.  `hwS` is UNTOUCHED and remains the
reference.  The variant applies ONE checked, local, source-to-source rewrite:

    letN "env0" e (letN "env" e2 body)   ==>   letN "env" e2[env0 := e] body

which is single-use inlining of the immediately following binding.  It is
applied only when all four side conditions hold, and the conditions are
reported either way so a failure is visible rather than silent:

  * `env0` does not occur in its own right-hand side          (no self-reference)
  * `env0` occurs EXACTLY ONCE in the next binding's rhs       (work done once)
  * `env0` does not occur in the rest of the block             (nothing else sees it)
  * that rhs contains no `ite`, `switch` or `letN`             (the single use is
    evaluated unconditionally, and there is no binder on the path, so `substRef`
    -- which is NOT capture-avoiding in general -- cannot capture)
  * every subterm of that rhs evaluated STRICTLY BEFORE the occurrence is
    `ref` or `lit`                                             (`beforeHoleTotal`)

## Why the fourth condition is not optional

An earlier version of this file claimed the first three conditions already gave
"the same order, and failure or divergence of `e` preserved, because `call` is
strict in its arguments".  **That is false**, and the counterexample is small:

    letN "z" E (letN "w" (call f [A, z]) body)        -- E runs FIRST
    ==>  letN "w" (call f [A, E]) body                 -- A runs first, THEN E

Arguments are evaluated left to right, so moving `E` into argument position 1
puts `A` ahead of it.  With `E` a type error and `A` a loop, the original
program fails with `typeError` and the rewritten one diverges (`outOfFuel`
under `evalFuel`).  Strictness says every argument is eventually evaluated; it
says nothing about which one fails first.

`beforeHoleTotal` restores the property by requiring everything evaluated
before the hole to be incapable of failing or diverging -- syntactically, a
variable reference or a literal.  In `main` the occurrence sits at
`call "evalNodes" [ref "nodes", ref "env0", ref "nsrc"]`, whose only preceding
argument is `ref "nodes"`, so the real rewrite satisfies it.
`proto_probe --inline-negative` exhibits the counterexample, checks that this
module REFUSES it, and checks that applying it anyway changes `typeError` into
`outOfFuel`.

## The intended theorem, stated precisely

NOT "the two programs are observationally equivalent".  What the side
conditions support is agreement on SUCCESSFUL evaluation:

    beforeHoleTotal and the three occurrence conditions  -->
      forall rho v,  SEval P rho (letN x e e2') v  <->  SEval P rho (e2'[x := e]) v

i.e. an `SEval` iff, where `SEval` relates only terminating successful
evaluations.  It is NOT an equality of `evalFuel` results: fuel accounting
differs (one fewer `letIn` to descend), and without `beforeHoleTotal` the
FAILURE MODE differs as above.  Any promotion must prove that statement, not a
stronger one that is false.

SCOPE: nothing here is proved, nothing here is in the core build or the axiom
audit, and `IHwAdequate_proved` is stated about `hwS`, not about this.  `hwS`
remains the reference until a proved source-to-source equivalence connects
the two.
-/
import LeanSemanticPrimitives.Projection.HardwareInterpreter

namespace Projection
namespace ProtoVar

open Projection.Surface

/-! ## A checked single-use inliner over the NAMED surface syntax

Names, not de Bruijn indices, so there is no index arithmetic to get wrong --
which is the class of bug this project has already hit once (`wrapLets`). -/

partial def countRef (x : String) : SExp → Nat
  | .lit _         => 0
  | .ref y         => if y == x then 1 else 0
  | .letN y e b    => countRef x e + (if y == x then 0 else countRef x b)
  | .ite c t f     => countRef x c + countRef x t + countRef x f
  | .prim _ es     => es.foldl (fun a e => a + countRef x e) 0
  | .mk _ es       => es.foldl (fun a e => a + countRef x e) 0
  | .call _ es     => es.foldl (fun a e => a + countRef x e) 0
  | .switch s alts =>
      countRef x s + alts.foldl
        (fun a (p : SAlt) => a + (if p.2.1.contains x then 0 else countRef x p.2.2)) 0

partial def substRef (x : String) (v : SExp) : SExp → SExp
  | .lit l         => .lit l
  | .ref y         => if y == x then v else .ref y
  | .letN y e b    => .letN y (substRef x v e) (if y == x then b else substRef x v b)
  | .ite c t f     => .ite (substRef x v c) (substRef x v t) (substRef x v f)
  | .prim p es     => .prim p (es.map (substRef x v))
  | .mk t es       => .mk t (es.map (substRef x v))
  | .call f es     => .call f (es.map (substRef x v))
  | .switch s alts => .switch (substRef x v s)
      (alts.map (fun (p : SAlt) =>
         (p.1, p.2.1, if p.2.1.contains x then p.2.2 else substRef x v p.2.2)))

/-- No `ite`, `switch` or `letN`: every leaf is reached unconditionally and
exactly once, and the object language evaluates `call`/`prim`/`mk` arguments
strictly, so substituting into such a term preserves evaluation order, work
count, and failure. -/
partial def noBranch : SExp → Bool
  | .lit _      => true
  | .ref _      => true
  | .letN _ _ _ => false
  | .ite _ _ _  => false
  | .switch _ _ => false
  | .prim _ es  => es.all noBranch
  | .mk _ es    => es.all noBranch
  | .call _ es  => es.all noBranch

/-- Evaluating this can neither fail nor diverge: it is a variable already in
scope, or a literal. -/
def obviouslyTotal : SExp → Bool
  | .ref _ => true
  | .lit _ => true
  | _      => false

mutual

/-- Everything evaluated STRICTLY BEFORE the single occurrence of `x` is
obviously total.  Arguments are evaluated left to right, so within each
argument list the ones preceding the hole must be total; the ones after it are
unconstrained, because they run after `e` in both programs.  Any binder or
branch on the path to the hole is rejected outright. -/
partial def beforeHoleTotal (x : String) : SExp → Bool
  | .ref _     => true                       -- the hole itself
  | .lit _     => true
  | .prim _ es => argsBeforeHole x es
  | .mk _ es   => argsBeforeHole x es
  | .call _ es => argsBeforeHole x es
  | _          => false                      -- letN / ite / switch on the path

partial def argsBeforeHole (x : String) : List SExp → Bool
  | []        => true
  | e :: rest =>
      if countRef x e > 0 then beforeHoleTotal x e      -- rest is AFTER the hole
      else obviouslyTotal e && argsBeforeHole x rest    -- this one runs BEFORE it

end

structure InlineReport where
  found       : Bool := false
  selfRef     : Nat  := 0
  useInNext   : Nat  := 0
  useLater    : Nat  := 0
  nextStrict  : Bool := false
  beforeTotal : Bool := false
  applied     : Bool := false
  deriving Repr, Inhabited

def InlineReport.ok (r : InlineReport) : Bool :=
  r.found && r.selfRef == 0 && r.useInNext == 1 && r.useLater == 0
    && r.nextStrict && r.beforeTotal && r.applied

/-- The rewrite AT THIS NODE, with its side conditions reported either way. -/
def inlineNextLet (x : String) : SExp → Option (SExp × InlineReport)
  | t@(.letN y e (.letN z e2 body)) =>
      if y == x && z != x then
        let r : InlineReport :=
          { found := true, selfRef := countRef x e, useInNext := countRef x e2
          , useLater := countRef x body, nextStrict := noBranch e2
          , beforeTotal := beforeHoleTotal x e2 }
        if r.selfRef == 0 && r.useInNext == 1 && r.useLater == 0 && r.nextStrict
             && r.beforeTotal
        then some (.letN z (substRef x e e2) body, { r with applied := true })
        else some (t, r)
      else none
  | _ => none

mutual

/-- Descend `letN` bodies and `switch` arms -- enough to reach `main`'s binding
block, and nothing more.  Rewrites the FIRST match only. -/
partial def goInline (x : String) : SExp → Option (SExp × InlineReport)
  | t@(.letN y e b) =>
      match inlineNextLet x t with
      | some r => some r
      | none   => (goInline x b).map (fun p => (.letN y e p.1, p.2))
  | .switch s alts => (goAlts x alts).map (fun p => (.switch s p.1, p.2))
  | _ => none

partial def goAlts (x : String) : List SAlt → Option (List SAlt × InlineReport)
  | [] => none
  | a :: rest =>
      match goInline x a.2.2 with
      | some p => some ((a.1, a.2.1, p.1) :: rest, p.2)
      | none   => (goAlts x rest).map (fun p => (a :: p.1, p.2))

end

/-! ## The variant program -/

def rewritten : Option (SFun × InlineReport) :=
  match Hw.hwS.funs.find? (fun f => f.name == "main") with
  | none   => none
  | some f => (goInline "env0" f.body).map (fun p => ({ f with body := p.1 }, p.2))

def report : InlineReport := match rewritten with | some p => p.2 | none => {}

def hwSVar : SProgram :=
  match rewritten with
  | none        => Hw.hwS
  | some (f, r) =>
      if r.ok then
        { Hw.hwS with
          funs := Hw.hwS.funs.map (fun g => if g.name == "main" then f else g) }
      else Hw.hwS

/-- The variant really is a different program -- a guard against silently
measuring the reference twice, which would read as "the change does nothing". -/
def changed : Bool := report.ok

/-! ## The negative test for `beforeHoleTotal`

A three-function program exhibiting the counterexample in the header.  `main`
binds `z` to a TYPE ERROR and then uses it as the SECOND argument of a call
whose FIRST argument DIVERGES.

  before:  letN z (hd 0) (letN w (pick2 (loop 0) z) w)   -- z runs first: typeError
  after:   letN w (pick2 (loop 0) (hd 0)) w              -- loop runs first: outOfFuel

`inlineNextLet` must REFUSE this (`beforeTotal = false`), and forcing the
rewrite must change the observed failure.  Both are checked by
`proto_probe --inline-negative`. -/

private def Rf (x : String) : SExp := .ref x
private def Cf (f : String) (es : List SExp) : SExp := .call f es

def negS : SProgram where
  entry := "main"
  funs :=
    [ -- diverges: unconditional self-call
      { name := "loop", params := ["n"], body := Cf "loop" [Rf "n"] }
      -- type error: `hd` of an integer
    , { name := "bad", params := ["n"], body := .prim .hd [.lit (.int 0)] }
      -- strict in both, returns the second
    , { name := "pick2", params := ["a", "b"], body := Rf "b" }
    , { name := "main", params := [],
        body := .letN "z" (Cf "bad" [.lit (.int 0)])
                  (.letN "w" (Cf "pick2" [Cf "loop" [.lit (.int 0)], Rf "z"])
                     (Rf "w")) } ]

/-- What the checker says about `z` -- expected `beforeTotal false, applied false`. -/
def negReport : InlineReport :=
  match negS.funs.find? (fun f => f.name == "main") with
  | none   => {}
  | some f => match goInline "z" f.body with
              | none        => {}
              | some (_, r) => r

/-- Apply the substitution ANYWAY, bypassing the side conditions, to show what
the conditions are protecting against. -/
def forceInline (x : String) : SExp → SExp
  | .letN y e (.letN z e2 body) =>
      if y == x then .letN z (substRef x e e2) body
      else .letN y e (.letN z e2 body)
  | t => t

def negSForced : SProgram :=
  { negS with funs := negS.funs.map (fun f =>
      if f.name == "main" then { f with body := forceInline "z" f.body } else f) }

def negBefore : Program :=
  match resolveProgram negS with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def negAfter : Program :=
  match resolveProgram negSForced with | .ok (p, _) => p | .error _ => ⟨[], 0⟩

def hwResolvedVar : Except String (Program × List Bool) := resolveProgram hwSVar
def hwPVar   : Program    := match hwResolvedVar with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def hwInlVar : List Bool  := match hwResolvedVar with | .ok (_, i) => i | .error _ => []
def hwAVar   : Except BTAError AProgram :=
  bta hwPVar hwInlVar [.stat, .dyn, .dyn, .dyn] 200
def hwAPVar  : AProgram := match hwAVar with | .ok a => a | .error _ => ⟨[], 0⟩

end ProtoVar
end Projection
