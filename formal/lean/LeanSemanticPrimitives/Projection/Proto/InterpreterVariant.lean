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
  * that rhs contains no `ite`, `switch` or `letN`             (so the single use is
    evaluated unconditionally, in the same order, and failure or divergence of
    `e` is preserved -- the object language's `call` is strict in its arguments)

SCOPE: no binder is introduced between the two bindings, so the rewrite cannot
capture; it moves `e`'s evaluation from "just before `e2`" to "during `e2`,
before anything else in `e2` runs", with nothing in between either way.  That
is an argument, not a proof.  Nothing here is proved, nothing here is in the
core build or the axiom audit, and `IHwAdequate_proved` is stated about `hwS`,
not about this.
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

structure InlineReport where
  found      : Bool := false
  selfRef    : Nat  := 0
  useInNext  : Nat  := 0
  useLater   : Nat  := 0
  nextStrict : Bool := false
  applied    : Bool := false
  deriving Repr, Inhabited

def InlineReport.ok (r : InlineReport) : Bool :=
  r.found && r.selfRef == 0 && r.useInNext == 1 && r.useLater == 0
    && r.nextStrict && r.applied

/-- The rewrite AT THIS NODE, with its side conditions reported either way. -/
def inlineNextLet (x : String) : SExp → Option (SExp × InlineReport)
  | t@(.letN y e (.letN z e2 body)) =>
      if y == x && z != x then
        let r : InlineReport :=
          { found := true, selfRef := countRef x e, useInNext := countRef x e2
          , useLater := countRef x body, nextStrict := noBranch e2 }
        if r.selfRef == 0 && r.useInNext == 1 && r.useLater == 0 && r.nextStrict
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

def hwResolvedVar : Except String (Program × List Bool) := resolveProgram hwSVar
def hwPVar   : Program    := match hwResolvedVar with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def hwInlVar : List Bool  := match hwResolvedVar with | .ok (_, i) => i | .error _ => []
def hwAVar   : Except BTAError AProgram :=
  bta hwPVar hwInlVar [.stat, .dyn, .dyn, .dyn] 200
def hwAPVar  : AProgram := match hwAVar with | .ok a => a | .error _ => ⟨[], 0⟩

end ProtoVar
end Projection
