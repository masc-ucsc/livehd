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

def direct : EvalResult := evalFuel 200 interpP [] (.call 0 [.lit sample, .lit sampleEnv])

-- `(5 + 3) * 7 = 56`
#guard direct == .value (.int 56)

/-! ## The first projection: specialize the interpreter to `sample` -/

def residual : Except MixError Program := mixDriver 200 50 interpA [sample]

#guard residual.toOption.isSome

def residualP : Program := match residual with | .ok p => p | .error _ => ⟨[], 0⟩

/-- The residual program, run on the dynamic input alone.  Note the argument
list: the static parameter is GONE -- the specialized entry takes only `env`. -/
def viaMix : EvalResult := evalFuel 200 residualP [] (.call 0 [.lit sampleEnv])

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
#guard evalFuel 200 residual2P [] (.call 0 [.lit sampleEnv]) == .value (.int 56)
#guard ((residual2P.funs.map (fun fd => countCaseT fd.body)).foldl (· + ·) 0) == 0


end Demo
end Projection
