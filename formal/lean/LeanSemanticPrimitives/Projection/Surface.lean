/-
  A named surface syntax for `L`, and its resolution to de Bruijn form.

  WHY THIS EXISTS.  The next two artifacts -- `mix` written in `L`, and the
  LGraph interpreter written in `L` -- are each several hundred constructs long.
  Written directly as `Term` literals they would be de Bruijn index arithmetic
  by hand, where a single off-by-one produces a program that still typechecks,
  still runs, and silently computes the wrong thing.  Name resolution is a
  fifty-line function that turns that entire class of bug into a
  `unbound identifier` error.

  This is NOT part of the trusted path.  Everything downstream is stated about
  the resolved `Term`/`Program`, so a bug here can only produce a program that
  fails its own tests -- it cannot make a false theorem true.  That is why
  resolution is an ordinary `Except`-returning function with no correctness
  proof: there is nothing to prove it against, since the surface syntax has no
  semantics of its own.

  BINDER ORDER IS THE ONE THING TO GET RIGHT, and it is fixed by the semantics
  rather than chosen here:

    `letIn e b`     evaluates `b` in `v :: ρ`, so the bound name is index 0;
    a `caseT` alt   evaluates its body in `vs ++ ρ`, so field `j` is index `j`;
    a function body runs in `vs` alone, so parameter `j` is index `j`.

  The `inline` flag on a function is NOT a binding-time property -- it is the
  unfold-versus-residualize choice, which is a termination and code-size
  judgement.  Binding-time analysis has no business making it, so it is carried
  from the surface instead of inferred.
-/

import LeanSemanticPrimitives.Projection.BindingTime

namespace Projection
namespace Surface

inductive SExp where
  | lit    : Val → SExp
  | ref    : String → SExp
  | letN   : String → SExp → SExp → SExp
  | ite    : SExp → SExp → SExp → SExp
  | prim   : Prim → List SExp → SExp
  | mk     : Nat → List SExp → SExp
  | switch : SExp → List (Nat × List String × SExp) → SExp
  | call   : String → List SExp → SExp
  deriving Inhabited

/-- One `switch` branch: a tag, the names it binds to the constructor's fields
in order, and a body. -/
abbrev SAlt := Nat × List String × SExp

structure SFun where
  name   : String
  params : List String
  /-- Unfold calls to this function rather than generating a specialized copy. -/
  inline : Bool := false
  body   : SExp
  deriving Inhabited

structure SProgram where
  funs  : List SFun
  entry : String
  deriving Inhabited

/-! ## Resolution -/

def idxOf : List String → String → Option Nat
  | [],      _ => none
  | x :: xs, y => if x == y then some 0 else (idxOf xs y).map (· + 1)

def funNames (P : SProgram) : List String := P.funs.map SFun.name

mutual

/-- `scope` is innermost-first, matching `Env`. -/
def resolve (fns : List String) (scope : List String) : SExp → Except String Term
  | .lit v  => .ok (.lit v)
  | .ref x  =>
    match idxOf scope x with
    | some i => .ok (.var i)
    | none   => .error s!"unbound identifier '{x}'"
  | .letN x e b => do
      let e' ← resolve fns scope e
      let b' ← resolve fns (x :: scope) b
      .ok (.letIn e' b')
  | .ite c a b => do
      let c' ← resolve fns scope c
      let a' ← resolve fns scope a
      let b' ← resolve fns scope b
      .ok (.ite c' a' b')
  | .prim p es => do
      let es' ← resolveList fns scope es
      if es'.length = p.arity then .ok (.prim p es')
      else .error s!"primitive expects {p.arity} operands, got {es'.length}"
  | .mk k es => do
      let es' ← resolveList fns scope es
      .ok (.ctorT k es')
  | .switch s alts => do
      let s' ← resolve fns scope s
      let alts' ← resolveAlts fns scope alts
      .ok (.caseT s' alts')
  | .call f es => do
      let es' ← resolveList fns scope es
      match idxOf fns f with
      | some i => .ok (.call i es')
      | none   => .error s!"unknown function '{f}'"

def resolveList (fns : List String) (scope : List String) : List SExp → Except String (List Term)
  | []      => .ok []
  | e :: es => do
      let e'  ← resolve fns scope e
      let es' ← resolveList fns scope es
      .ok (e' :: es')

def resolveAlts (fns : List String) (scope : List String) :
    List SAlt → Except String (List Alt)
  | []                    => .ok []
  | (tag, bs, body) :: as => do
      -- field `j` is index `j`, and the enclosing scope moves out by `bs.length`
      let body' ← resolve fns (bs ++ scope) body
      let as'   ← resolveAlts fns scope as
      .ok ((tag, bs.length, body') :: as')

end

/-- The resolved program plus, in the same order, which functions are to be
unfolded.  Two returns rather than a record because the flag is consumed by
binding-time analysis and never appears in the semantics. -/
def resolveProgram (P : SProgram) : Except String (Program × List Bool) := do
  let fns := funNames P
  let funs ← resolveFuns fns P.funs
  match idxOf fns P.entry with
  | none   => .error s!"unknown entry '{P.entry}'"
  | some e => .ok (⟨funs, e⟩, P.funs.map SFun.inline)
where
  resolveFuns (fns : List String) : List SFun → Except String (List FunDef)
    | []      => .ok []
    | f :: fs => do
        -- a function body runs in its arguments alone, parameter `j` at index `j`
        let b  ← resolve fns f.params f.body
        let fs' ← resolveFuns fns fs
        .ok (⟨f.params.length, b⟩ :: fs')

/-! ## Sugar

Only what the two large programs actually need.  Sequential `let`s are the
common shape and nesting them by hand is exactly the noise this file exists to
remove. -/

/-- `lets [(x, e), …] body` -- each binding is in scope for the ones after it. -/
def lets : List (String × SExp) → SExp → SExp
  | [],            body => body
  | (x, e) :: bs,  body => .letN x e (lets bs body)

@[inline] def int (n : Int) : SExp := .lit (.int n)
@[inline] def bool (b : Bool) : SExp := .lit (.bool b)
@[inline] def nil : SExp := .lit .nil
@[inline] def cons (a b : SExp) : SExp := .prim .consP [a, b]

/-- A literal list, as a cons chain. -/
def list : List SExp → SExp
  | []      => nil
  | e :: es => cons e (list es)

end Surface
end Projection
