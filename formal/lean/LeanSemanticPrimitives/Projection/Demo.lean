/-
  The first Futamura projection, executably, on a toy interpreter.

  This is the design's smoke test, run before `mix_sound` is attempted: if `mix`
  cannot erase an interpreter's dispatch on a two-constructor expression
  language, no amount of proof about it is worth writing.

  The object-object language: arithmetic expressions over a dynamic environment,
  represented as ordinary `Val` data.

      ctor 0 [int n]    constant n
      ctor 1 [int i]    variable i, looked up in the environment
      ctor 2 [e₁, e₂]   e₁ + e₂
      ctor 3 [e₁, e₂]   e₁ * e₂

  `interp (e, env)` has `e` static and `env` dynamic.  Specializing it to a
  fixed `e` should leave a residual program with no `caseT` in it at all: the
  interpreter's dispatch happened at specialization time.
-/

import LeanSemanticPrimitives.Projection.PartialEvaluator
import LeanSemanticPrimitives.Projection.BTA
import LeanSemanticPrimitives.Projection.Surface

namespace Projection
namespace Demo

/-! ## The interpreter, as an annotated program

Two functions, both `(static, dynamic) → dynamic`:

* `nth i env` walks `i` steps down a dynamic list -- a STATIC loop over DYNAMIC
  data, the shape that only unrolls because `ucall` exists;
* `interp e env` dispatches on the static expression.
-/

/-- `nth i env = if i = 0 then hd env else nth (i-1) (tl env)` -/
def nthFun : AFunDef where
  params := [.stat, .dyn]
  ret    := .dyn
  body   :=
    .ite .dyn
      (.prim .stat .eqI [.var 0, .lit (.int 0)])          -- STATIC condition
      (.prim .dyn .hd [.var 1])
      (.ucall .dyn 1
        [.prim .stat .subI [.var 0, .lit (.int 1)],
         .prim .dyn .tl [.var 1]])

/-- Inside an alternative of arity `k`, field `j` is at index `j` and everything
that was in scope moves out by `k` -- so `env`, at index 1 outside, is at index
`1 + k` inside. -/
def interpFun : AFunDef where
  params := [.stat, .dyn]
  ret    := .dyn
  body :=
    .caseT .dyn (.var 0)
      [ (0, 1, .lift (.var 0))                                   -- const n
      , (1, 1, .ucall .dyn 1 [.var 0, .var 2])                   -- nth i env
      , (2, 2, .prim .dyn .addI [ .ucall .dyn 0 [.var 0, .var 3]
                                , .ucall .dyn 0 [.var 1, .var 3] ])
      , (3, 2, .prim .dyn .mulI [ .ucall .dyn 0 [.var 0, .var 3]
                                , .ucall .dyn 0 [.var 1, .var 3] ]) ]

def interpA : AProgram := ⟨[interpFun, nthFun], 0⟩

-- The annotation must typecheck before anything else means anything.
#guard wfAProgram interpA

/-! ## A source program and the environment it runs in -/

/-- `(x₀ + 3) * x₁` -/
def sample : Val :=
  .ctor 3 [ .ctor 2 [.ctor 1 [.int 0], .ctor 0 [.int 3]]
          , .ctor 1 [.int 1] ]

/-- `[5, 7]` as a cons list. -/
def sampleEnv : Val := .cons (.int 5) (.cons (.int 7) .nil)

/-! ## Running the interpreter directly -/

def interpP : Program := eraseProgram interpA

def direct : EvalResult := evalFuel 200 interpP [] (.call interpP.entry [.lit sample, .lit sampleEnv])

-- `(5 + 3) * 7 = 56`
#guard direct == .value (.int 56)

/-! ## The first projection: specialize the interpreter to `sample` -/

def residual : Except MixError Program := mixDriver 200 50 interpA [sample]

#guard residual.toOption.isSome

def residualP : Program := match residual with | .ok p => p | .error _ => ⟨[], 0⟩

/-- The residual program, run on the dynamic input alone.  Note the argument
list: the static parameter is GONE -- the specialized entry takes only `env`. -/
def viaMix : EvalResult := evalFuel 200 residualP [] (.call residualP.entry [.lit sampleEnv])

#guard viaMix == .value (.int 56)

/-- The point of the exercise: the interpreter's dispatch is not in the output. -/
def countCaseT : Term → Nat
  | .lit _      => 0
  | .var _      => 0
  | .letIn a b  => countCaseT a + countCaseT b
  | .ite a b c  => countCaseT a + countCaseT b + countCaseT c
  | .prim _ ts  => countCaseTs ts
  | .ctorT _ ts => countCaseTs ts
  | .caseT s as => 1 + countCaseT s + countCaseTAlts as
  | .call _ ts  => countCaseTs ts
where
  countCaseTs : List Term → Nat
    | []      => 0
    | t :: ts => countCaseT t + countCaseTs ts
  countCaseTAlts : List Alt → Nat
    | []      => 0
    | a :: as => countCaseT a.body + countCaseTAlts as

def residualCaseTs : Nat := (residualP.funs.map (fun fd => countCaseT fd.body)).foldl (· + ·) 0
def sourceCaseTs   : Nat := (interpP.funs.map   (fun fd => countCaseT fd.body)).foldl (· + ·) 0

#guard sourceCaseTs == 1
#guard residualCaseTs == 0

-- And the recursive `nth` walk is unrolled: `nth` needed no residual function.
#guard residualP.funs.length == 1

/-! ## The same thing again, through the real pipeline

Above, the annotation was written by hand.  Here the interpreter is written in
the surface syntax with names, resolved to de Bruijn, and annotated by BTA --
which is how every program from here on will be produced.  If the two paths
disagree, one of them is wrong.
-/

open Surface in
/-- `interp` and `nth` in named form.  `nth` is marked `inline` so its static
walk down a dynamic list unrolls; `interp`'s recursion on subexpressions is
inlined too, since the expression tree is static and finite. -/
def interpS : SProgram where
  entry := "interp"
  funs :=
    [ { name := "interp", params := ["e", "env"], inline := true
      , body :=
          .switch (.ref "e")
            [ (0, ["n"],       .ref "n")
            , (1, ["i"],       .call "nth" [.ref "i", .ref "env"])
            , (2, ["l", "r"],  .prim .addI [ .call "interp" [.ref "l", .ref "env"]
                                           , .call "interp" [.ref "r", .ref "env"] ])
            , (3, ["l", "r"],  .prim .mulI [ .call "interp" [.ref "l", .ref "env"]
                                           , .call "interp" [.ref "r", .ref "env"] ]) ] }
    , { name := "nth", params := ["i", "env"], inline := true
      , body :=
          .ite (.prim .eqI [.ref "i", int 0])
            (.prim .hd [.ref "env"])
            (.call "nth" [.prim .subI [.ref "i", int 1], .prim .tl [.ref "env"]]) } ]

def interpResolved : Except String (Program × List Bool) := Surface.resolveProgram interpS

#guard interpResolved.toOption.isSome

def interpP2  : Program    := match interpResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def interpInl : List Bool  := match interpResolved with | .ok (_, i) => i | .error _ => []

-- name resolution must reproduce the hand-written de Bruijn program exactly
#guard interpP2.funs.length == 2

-- BTA: the entry takes a static expression and a dynamic environment
def interpA2 : Except BTAError AProgram := bta interpP2 interpInl [.stat, .dyn] 50

#guard interpA2.toOption.isSome

def interpA2P : AProgram := match interpA2 with | .ok a => a | .error _ => ⟨[], 0⟩

-- bta_sound and bta_erases, as executable checks on this instance
#guard wfAProgram interpA2P
#guard eraseProgram interpA2P == interpP2

-- BTA must infer exactly the division that was written by hand
#guard (interpA2P.funs.map AFunDef.params) == [[.stat, .dyn], [.stat, .dyn]]
#guard (interpA2P.funs.map AFunDef.ret)    == [.dyn, .dyn]

def residual2 : Except MixError Program := mixDriver 200 50 interpA2P [sample]

#guard residual2.toOption.isSome

def residual2P : Program := match residual2 with | .ok p => p | .error _ => ⟨[], 0⟩

-- and the pipeline-produced residual computes the same thing, with the
-- dispatch equally gone
#guard evalFuel 200 residual2P [] (.call residual2P.entry [.lit sampleEnv]) == .value (.int 56)
#guard ((residual2P.funs.map (fun fd => countCaseT fd.body)).foldl (· + ·) 0) == 0

/-! ## Regression: an unfolded call with TWO dynamic arguments

Unfolding wraps one residual `let` per dynamic argument, so argument 1 is
evaluated underneath argument 0's binder.  Mixed in the caller's environment its
de Bruijn indices come out one short and it reads the wrong variable.

Nothing above catches this: `interp` and `nth` each take exactly one dynamic
argument, and with one argument there is one binder and no shift.  The check
below is the smallest program that does catch it -- `sub` is unfolded with two
dynamic arguments that must NOT be confused, and `subI` is not commutative, so
swapping them changes the answer. -/

open Surface in
def twoArgS : SProgram where
  entry := "main"
  funs :=
    [ { name := "main", params := ["s", "x", "y"]
      , body := .call "sub" [.prim .addI [.ref "x", .ref "s"], .ref "y"] }
    , { name := "sub", params := ["a", "b"], inline := true
      , body := .prim .subI [.ref "a", .ref "b"] } ]

def twoArgResolved : Except String (Program × List Bool) := Surface.resolveProgram twoArgS
#guard twoArgResolved.toOption.isSome

def twoArgP   : Program   := match twoArgResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def twoArgInl : List Bool := match twoArgResolved with | .ok (_, i) => i | .error _ => []

-- `s` static, `x` and `y` dynamic
def twoArgA : AProgram :=
  match bta twoArgP twoArgInl [.stat, .dyn, .dyn] 50 with
  | .ok a => a | .error _ => ⟨[], 0⟩

#guard wfAProgram twoArgA

def twoArgRes : Program :=
  match mixDriver 500 100 twoArgA [.int 10] with | .ok p => p | .error _ => ⟨[], 0⟩

-- source:  main 10 7 2  =  sub (7 + 10) 2  =  15
#guard evalFuel 500 twoArgP [] (.call twoArgP.entry [.lit (.int 10), .lit (.int 7), .lit (.int 2)])
         == .value (.int 15)

-- residual, on the dynamic arguments alone.  With the arguments mixed in the
-- caller's environment this yielded the wrong value, because argument 1 read
-- argument 0's binder.
#guard evalFuel 500 twoArgRes [] (.call twoArgRes.entry [.lit (.int 7), .lit (.int 2)])
         == .value (.int 15)

/-! ## Regression: the entry is not source function 0

Everything above agrees between the Lean and object specializers only because
this toy's entry happens to be source function 0 and its helpers are inlined
away, so the residual has ONE function and there is no order to disagree about.

This fixture removes both accidents: two RESIDUALIZED functions, with the entry
at source index 1 and its callee at index 0.  Before the Lean driver was made
function-major it put the entry at residual index 0 (discovery order) while the
object specializer put it at index 1 (function-major order) -- the two produced
different programs, checked and confirmed.  Both residuals computed the same
value, so the disagreement was purely a permutation of the function table, with
the call indices embedded in the residual code pointing into the wrong one. -/

open Surface in
def twoFunS : SProgram where
  entry := "main"
  funs :=
    [ { name := "helper", params := ["s", "x"]
      , body := .prim .addI [.ref "x", .ref "s"] }
    , { name := "main", params := ["s", "x"]
      , body := .prim .mulI [.call "helper" [.ref "s", .ref "x"], int 2] } ]

def twoFunResolved : Except String (Program × List Bool) := Surface.resolveProgram twoFunS
#guard twoFunResolved.toOption.isSome

def twoFunP   : Program   := match twoFunResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def twoFunInl : List Bool := match twoFunResolved with | .ok (_, i) => i | .error _ => []

-- the entry is NOT source function 0; that is the whole point of the fixture
#guard twoFunP.entry == 1

def twoFunA : AProgram :=
  match bta twoFunP twoFunInl [.stat, .dyn] 50 with | .ok a => a | .error _ => ⟨[], 0⟩
#guard wfAProgram twoFunA

def twoFunRes : Program :=
  match mixDriver 500 100 twoFunA [.int 10] with | .ok p => p | .error _ => ⟨[], 0⟩

-- two residual functions, and function-major order puts the entry second
#guard twoFunRes.funs.length == 2
#guard twoFunRes.entry == 1

-- main 10 7 = (7 + 10) * 2 = 34, source and residual alike
#guard evalFuel 500 twoFunP [] (.call twoFunP.entry [.lit (.int 10), .lit (.int 7)])
         == .value (.int 34)
#guard evalFuel 500 twoFunRes [] (.call twoFunRes.entry [.lit (.int 7)]) == .value (.int 34)

end Demo
end Projection
