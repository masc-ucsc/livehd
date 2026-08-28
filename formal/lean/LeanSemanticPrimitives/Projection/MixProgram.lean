/-
  `mix`, written in `L`.

  `PartialEvaluator.lean` gives the specializer as a Lean function.  That one
  cannot be specialized: the second projection needs `mix` to be an object
  PROGRAM, because `mix` must take `mix` as its static input, and a Lean `def`
  is not data.  This file is that program.

  It is a transcription, not a second design.  Every function here mirrors one
  in `PartialEvaluator.lean`, and `MixProgram_agrees` (Demo) checks that the two
  produce the same residual program on the same input.  Where they differ, the
  difference is recorded below.

  DIFFERENCE 1 -- NO ERROR PLUMBING.  The Lean specializer returns
  `Except MixError`; this one does not.  Threading an `Except` through a
  first-order language by hand means every one of ~30 functions gains a check at
  every call site, roughly tripling the program -- and under `bta_sound` every
  one of those branches is unreachable, because `mix` is only ever applied to a
  well-annotated program.  Instead, an impossible case falls through to a
  `caseT` with no matching alternative, which `evalFuel` reports as a
  `typeError`.  The failure is still loud; it just is not plumbed.

  DIFFERENCE 2 -- NO FUEL PARAMETER.  The Lean version takes a step budget
  because Lean requires termination.  Here `evalFuel` supplies it from outside,
  which is also why the correctness statement can be phrased with `Eval` and
  never mention fuel.

  NOTHING IS MARKED `inline`, AND THAT IS THE LOAD-BEARING CHOICE.  It is
  tempting to inline `mixTerm`, since it recurses on a static term and would
  then produce beautifully flat code.  It is wrong: at the outer level -- `mix`
  specializing `mix` -- the annotated program is static but the static VALUES
  are not, so a `ucall` in the program being specialized turns into an unfold
  whose guard `mix` cannot evaluate, and unfolding a recursive callee never
  terminates.  Residualizing instead makes `mixTerm` memoized on its static
  arguments `(A, Δ, t)`, of which there are finitely many, so the loop is closed
  by the memo table.  The result is one residual function per interpreter
  subterm -- which is exactly what a compiler is.
-/

import LeanSemanticPrimitives.Projection.Surface
import LeanSemanticPrimitives.Projection.Encoding

namespace Projection
namespace MixProg

open Surface

/-! ## Tags for the specializer's own data -/

def tagPStat : Nat := 60   -- PEnv entry: a known value
def tagPDyn  : Nat := 61   -- PEnv entry: a residual de Bruijn index
def tagRStat : Nat := 70   -- PRes: a value
def tagRCode : Nat := 71   -- PRes: residual code
def tagReq   : Nat := 80   -- SpecRequest

/-! ## Building surface terms -/

private abbrev R := SExp.ref
private def K (n : Int) : SExp := .lit (.int n)
private def P1 (p : Prim) (a : SExp) : SExp := .prim p [a]
private def P2 (p : Prim) (a b : SExp) : SExp := .prim p [a, b]
private def C (f : String) (as : List SExp) : SExp := .call f as

private def hd_ (e : SExp) := P1 .hd e
private def tl_ (e : SExp) := P1 .tl e
private def cons_ (a b : SExp) := P2 .consP a b
private def nil_ : SExp := .lit .nil
private def isNil_ (e : SExp) := P1 .isNil e
private def eq_ (a b : SExp) := P2 .eqI a b
private def add_ (a b : SExp) := P2 .addI a b
private def sub_ (a b : SExp) := P2 .subI a b

/-- Pairs are cons cells; `mix` returns `(result, requests)` everywhere. -/
private def pair_ (a b : SExp) := cons_ a b
private def fst_ (e : SExp) := hd_ e
private def snd_ (e : SExp) := tl_ e

/-! ### Encoded `Term` constructors, as the residual program's syntax -/

private def eLit   (v : SExp)          := SExp.mk tagLit   [v]
private def eVar   (k : SExp)          := SExp.mk tagVar   [k]
private def eLetIn (a b : SExp)        := SExp.mk tagLetIn [a, b]
private def eIte   (c a b : SExp)      := SExp.mk tagIte   [c, a, b]
private def ePrim  (p ts : SExp)       := SExp.mk tagPrim  [p, ts]
private def eCtorT (k ts : SExp)       := SExp.mk tagCtorT [k, ts]
private def eCaseT (s as : SExp)       := SExp.mk tagCaseT [s, as]
private def eCall  (f ts : SExp)       := SExp.mk tagCall  [f, ts]
private def eAlt   (t a b : SExp)      := SExp.mk tagAlt   [t, a, b]
private def eFunDef (a b : SExp)       := SExp.mk tagFunDef [a, b]
private def eProgram (fs e : SExp)     := SExp.mk tagProgram [fs, e]

private def pStat (v : SExp) := SExp.mk tagPStat [v]
private def pDyn  (k : SExp) := SExp.mk tagPDyn  [k]
private def rStat (v : SExp) := SExp.mk tagRStat [v]
private def rCode (t : SExp) := SExp.mk tagRCode [t]
private def mkReq (f vs : SExp) := SExp.mk tagReq [f, vs]

/-! ## `evalPrim`, in `L`

The dispatch is on the primitive's CODE, which is part of the program being
specialized and therefore static -- so this whole chain unrolls away, leaving
one primitive application.  That is the single clearest place to see why
binding-time analysis has to be offline. -/

private def pA : SExp := hd_ (R "vs")
private def pB : SExp := hd_ (tl_ (R "vs"))
private def pC : SExp := hd_ (tl_ (tl_ (R "vs")))

private def primTable : List (Int × SExp) :=
  [ (0,  .prim .addI [pA, pB]), (1,  .prim .subI [pA, pB])
  , (2,  .prim .mulI [pA, pB]), (3,  .prim .divI [pA, pB])
  , (4,  .prim .modI [pA, pB])
  , (5,  .prim .ltI  [pA, pB]), (6,  .prim .leI  [pA, pB])
  , (7,  .prim .eqI  [pA, pB])
  , (8,  .prim .andB [pA, pB]), (9,  .prim .orB  [pA, pB])
  , (10, .prim .notB [pA])
  , (11, .prim .isNil [pA]),    (12, .prim .hd [pA]), (13, .prim .tl [pA])
  , (14, .prim .bvMk [pA, pB]), (15, .prim .bvWidth [pA])
  , (16, .prim .bvUint [pA]),   (17, .prim .bvBit [pA, pB])
  , (18, .prim .bvAnd [pA, pB, pC]), (19, .prim .bvOr [pA, pB, pC])
  , (20, .prim .bvXor [pA, pB, pC]), (21, .prim .bvNot [pA, pB])
  , (22, .prim .bvResize [pA, pB])
  , (23, .prim .consP [pA, pB]), (24, .prim .eqV [pA, pB])
  , (25, .prim .mkCtorP [pA, pB]), (26, .prim .ctorTagP [pA])
  , (27, .prim .ctorFieldsP [pA]) ]

/-- An unrecognised code falls through to `hd nil`, a `typeError`. -/
private def evalPrimBody : SExp :=
  primTable.foldr (fun pe acc => .ite (eq_ (R "p") (K pe.1)) pe.2 acc) (hd_ nil_)

/-- The term walk's alternatives, hoisted so the list is not fighting the
structure-instance indentation rules. -/
private def mixTermAlts : List SAlt :=
  [ (tagALit, ["v"], pair_ (rStat (R "v")) nil_)

        , (tagAVar, ["i"],
            .switch (C "nthL" [R "i", R "env"])
              [ (tagPStat, ["v"], pair_ (rStat (R "v")) nil_)
              , (tagPDyn,  ["k"], pair_ (rCode (eVar (R "k"))) nil_) ])

        , (tagALift, ["e"],
            .letN "o" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "e"])
              (pair_ (rCode (eLit (C "presVal" [fst_ (R "o")]))) (snd_ (R "o"))))

        , (tagALetIn, ["b", "e", "bd"],
            .letN "be" (C "btOfD" [R "D", R "e"]) <|
            .letN "o1" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "e"]) <|
            .letN "r1" (fst_ (R "o1")) <|
            .letN "q1" (snd_ (R "o1")) <|
            .ite (eq_ (R "be") (K 0))
              -- static binding: no residual binder, so nothing in scope moves
              (.letN "o2" (C "mixTerm"
                  [R "A", R "reqs", cons_ (K 0) (R "D"),
                   cons_ (pStat (C "presVal" [R "r1"])) (R "env"), R "bd"])
                (pair_ (fst_ (R "o2")) (C "appendL" [R "q1", snd_ (R "o2")])))
              -- dynamic binding: one residual binder, so everything shifts by one
              (.letN "o2" (C "mixTerm"
                  [R "A", R "reqs", cons_ (K 1) (R "D"),
                   cons_ (pDyn (K 0)) (C "shiftEnv" [K 1, R "env"]), R "bd"])
                (pair_ (rCode (eLetIn (C "toCode" [R "r1"])
                                      (C "toCode" [fst_ (R "o2")])))
                       (C "appendL" [R "q1", snd_ (R "o2")]))))

        , (tagAIte, ["b", "c", "a", "e"],
            .letN "bc" (C "btOfD" [R "D", R "c"]) <|
            .letN "o1" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "c"]) <|
            .letN "r1" (fst_ (R "o1")) <|
            .letN "q1" (snd_ (R "o1")) <|
            .ite (eq_ (R "bc") (K 0))
              -- static condition: only the taken branch is walked, so the other
              -- branch's code and its specialization requests never appear
              (.ite (C "presVal" [R "r1"])
                (.letN "o2" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "a"])
                  (pair_ (fst_ (R "o2")) (C "appendL" [R "q1", snd_ (R "o2")])))
                (.letN "o2" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "e"])
                  (pair_ (fst_ (R "o2")) (C "appendL" [R "q1", snd_ (R "o2")]))))
              (.letN "o2" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "a"]) <|
               .letN "o3" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "e"]) <|
               pair_ (rCode (eIte (C "toCode" [R "r1"])
                                  (C "toCode" [fst_ (R "o2")])
                                  (C "toCode" [fst_ (R "o3")])))
                     (C "appendL" [R "q1",
                        C "appendL" [snd_ (R "o2"), snd_ (R "o3")]])))

        , (tagAPrim, ["b", "p", "ts"],
            .letN "o" (C "mixTerms" [R "A", R "reqs", R "D", R "env", R "ts"]) <|
            .letN "rs" (fst_ (R "o")) <|
            .ite (eq_ (R "b") (K 0))
              (pair_ (rStat (C "evalPrimL" [R "p", C "allStaticL" [R "rs"]])) (snd_ (R "o")))
              (pair_ (rCode (ePrim (R "p") (C "mapToCode" [R "rs"]))) (snd_ (R "o"))))

        , (tagACtorT, ["b", "k", "ts"],
            .letN "o" (C "mixTerms" [R "A", R "reqs", R "D", R "env", R "ts"]) <|
            .letN "rs" (fst_ (R "o")) <|
            .ite (eq_ (R "b") (K 0))
              (pair_ (rStat (P2 .mkCtorP (R "k") (C "allStaticL" [R "rs"]))) (snd_ (R "o")))
              (pair_ (rCode (eCtorT (R "k") (C "mapToCode" [R "rs"]))) (snd_ (R "o"))))

        , (tagACaseT, ["b", "s", "as"],
            .letN "bs" (C "btOfD" [R "D", R "s"]) <|
            .letN "o1" (C "mixTerm" [R "A", R "reqs", R "D", R "env", R "s"]) <|
            .letN "r1" (fst_ (R "o1")) <|
            .letN "q1" (snd_ (R "o1")) <|
            .ite (eq_ (R "bs") (K 0))
              -- static scrutinee: pick the alternative now and bind its fields as
              -- values.  No residual `caseT` survives.
              (.letN "sv" (C "presVal" [R "r1"]) <|
               .letN "tg" (P1 .ctorTagP (R "sv")) <|
               .letN "fs" (P1 .ctorFieldsP (R "sv")) <|
               .letN "al" (C "findAAltL" [R "as", R "tg"]) <|
               .letN "ar" (C "altArity" [R "al"]) <|
               .letN "o2" (C "mixTerm"
                   [R "A", R "reqs",
                    C "appendL" [C "replicateL" [R "ar", K 0], R "D"],
                    C "appendL" [C "mapPStat" [R "fs"], R "env"],
                    C "altBody" [R "al"]])
                 (pair_ (fst_ (R "o2")) (C "appendL" [R "q1", snd_ (R "o2")])))
              (.letN "o2" (C "mixAlts" [R "A", R "reqs", R "D", R "env", R "as"])
                 (pair_ (rCode (eCaseT (C "toCode" [R "r1"]) (fst_ (R "o2"))))
                        (C "appendL" [R "q1", snd_ (R "o2")]))))

        , (tagACall, ["b", "f", "ts"],
            .letN "o" (C "mixTerms" [R "A", R "reqs", R "D", R "env", R "ts"]) <|
            .letN "rs" (fst_ (R "o")) <|
            .letN "q" (snd_ (R "o")) <|
            .letN "fd" (C "fnOf" [R "A", R "f"]) <|
            .letN "ps" (C "funParams" [R "fd"]) <|
            .ite (eq_ (R "b") (K 0))
              -- a static result cannot come out of a residual call, so unfold
              (.letN "o2" (C "mixTerm"
                  [R "A", R "reqs", R "ps",
                   C "mapPStat" [C "allStaticL" [R "rs"]], C "funBody" [R "fd"]])
                (pair_ (fst_ (R "o2")) (C "appendL" [R "q", snd_ (R "o2")])))
              -- ask the driver for a specialized copy and call it
              (.letN "req" (mkReq (R "f") (C "splitStatics" [R "ps", R "rs"])) <|
               pair_ (rCode (eCall (C "indexOfReqL" [R "reqs", R "req", K 0])
                                   (C "splitDyns" [R "ps", R "rs"])))
                     (C "appendL" [R "q", cons_ (R "req") nil_])))

        , (tagAUcall, ["b", "f", "ts"],
            .letN "o" (C "mixTerms" [R "A", R "reqs", R "D", R "env", R "ts"]) <|
            .letN "rs" (fst_ (R "o")) <|
            .letN "q" (snd_ (R "o")) <|
            .letN "fd" (C "fnOf" [R "A", R "f"]) <|
            .letN "ps" (C "funParams" [R "fd"]) <|
            .ite (eq_ (R "b") (K 0))
              (.letN "o2" (C "mixTerm"
                  [R "A", R "reqs", R "ps",
                   C "mapPStat" [C "allStaticL" [R "rs"]], C "funBody" [R "fd"]])
                (pair_ (fst_ (R "o2")) (C "appendL" [R "q", snd_ (R "o2")])))
              -- inline: bind each dynamic argument once, then specialize the body
              (.letN "dts" (C "splitDyns" [R "ps", R "rs"]) <|
               .letN "kk"  (C "dynCountL" [R "ps"]) <|
               .letN "o2" (C "mixTerm"
                   [R "A", R "reqs", R "ps",
                    C "inlineEnvL" [R "ps", R "rs", R "kk", K 0],
                    C "funBody" [R "fd"]]) <|
               pair_ (rCode (C "wrapLetsL" [R "dts", C "toCode" [fst_ (R "o2")]]))
                     (C "appendL" [R "q", snd_ (R "o2")]))) ]

/-! ## The program -/

def mixS : SProgram where
  entry := "mixDriver"
  funs := [

  -- ## generic list helpers

  -- `nth i l`.  Recursion is on `i`, which is static wherever this is used, so
  -- specializing it unrolls to exactly `i` steps.
  { name := "nthL", params := ["i", "l"]
  , body := .ite (eq_ (R "i") (K 0)) (hd_ (R "l"))
                 (C "nthL" [sub_ (R "i") (K 1), tl_ (R "l")]) }

  , { name := "appendL", params := ["a", "b"]
    , body := .ite (isNil_ (R "a")) (R "b")
                   (cons_ (hd_ (R "a")) (C "appendL" [tl_ (R "a"), R "b"])) }

  -- ## annotated-program accessors

  , { name := "progFuns", params := ["A"]
    , body := .switch (R "A") [(tagAProgram, ["fs", "e"], R "fs")] }
  , { name := "progEntry", params := ["A"]
    , body := .switch (R "A") [(tagAProgram, ["fs", "e"], R "e")] }
  , { name := "fnOf", params := ["A", "f"]
    , body := C "nthL" [R "f", C "progFuns" [R "A"]] }
  , { name := "funParams", params := ["fd"]
    , body := .switch (R "fd") [(tagAFunDef, ["p", "r", "b"], R "p")] }
  , { name := "funRet", params := ["fd"]
    , body := .switch (R "fd") [(tagAFunDef, ["p", "r", "b"], R "r")] }
  , { name := "funBody", params := ["fd"]
    , body := .switch (R "fd") [(tagAFunDef, ["p", "r", "b"], R "b")] }

  -- ## binding time of an annotated term under a division
  --
  -- Non-recursive except for the variable lookup, exactly as in Lean: every
  -- other case reads a stored annotation.

  , { name := "btOfD", params := ["D", "t"]
    , body := .switch (R "t")
        [ (tagALit,   ["v"],            K 0)
        , (tagAVar,   ["i"],            C "nthL" [R "i", R "D"])
        , (tagALetIn, ["b", "e", "bd"], R "b")
        , (tagAIte,   ["b", "c", "a", "e"], R "b")
        , (tagAPrim,  ["b", "p", "ts"], R "b")
        , (tagACtorT, ["b", "k", "ts"], R "b")
        , (tagACaseT, ["b", "s", "as"], R "b")
        , (tagACall,  ["b", "f", "ts"], R "b")
        , (tagAUcall, ["b", "f", "ts"], R "b")
        , (tagALift,  ["e"],            K 1) ] }

  -- ## partial-environment helpers

  , { name := "shiftEnv", params := ["k", "env"]
    , body := .ite (isNil_ (R "env")) nil_
        (.switch (hd_ (R "env"))
          [ (tagPStat, ["v"], cons_ (pStat (R "v")) (C "shiftEnv" [R "k", tl_ (R "env")]))
          , (tagPDyn,  ["j"], cons_ (pDyn (add_ (R "j") (R "k")))
                                    (C "shiftEnv" [R "k", tl_ (R "env")])) ]) }

  -- `[dyn j, dyn (j+1), …, dyn (k-1)]`
  , { name := "freshFrom", params := ["j", "k"]
    , body := .ite (eq_ (R "j") (R "k")) nil_
                   (cons_ (pDyn (R "j")) (C "freshFrom" [add_ (R "j") (K 1), R "k"])) }

  , { name := "replicateL", params := ["k", "x"]
    , body := .ite (eq_ (R "k") (K 0)) nil_
                   (cons_ (R "x") (C "replicateL" [sub_ (R "k") (K 1), R "x"])) }

  -- ## partial results

  , { name := "presVal", params := ["r"]
    , body := .switch (R "r") [(tagRStat, ["v"], R "v")] }

  , { name := "toCode", params := ["r"]
    , body := .switch (R "r")
        [ (tagRStat, ["v"], eLit (R "v"))     -- this is `lift`
        , (tagRCode, ["t"], R "t") ] }

  , { name := "allStaticL", params := ["rs"]
    , body := .ite (isNil_ (R "rs")) nil_
                   (cons_ (C "presVal" [hd_ (R "rs")]) (C "allStaticL" [tl_ (R "rs")])) }

  , { name := "mapToCode", params := ["rs"]
    , body := .ite (isNil_ (R "rs")) nil_
                   (cons_ (C "toCode" [hd_ (R "rs")]) (C "mapToCode" [tl_ (R "rs")])) }

  , { name := "evalPrimL", params := ["p", "vs"], body := evalPrimBody }

  -- ## alternative selection

  , { name := "findAAltL", params := ["alts", "tag"]
    , body := .switch (hd_ (R "alts"))
        [ (tagAAlt, ["t", "a", "b"],
            .ite (eq_ (R "t") (R "tag")) (hd_ (R "alts"))
                 (C "findAAltL" [tl_ (R "alts"), R "tag"])) ] }

  , { name := "altArity", params := ["a"]
    , body := .switch (R "a") [(tagAAlt, ["t", "n", "b"], R "n")] }
  , { name := "altBody", params := ["a"]
    , body := .switch (R "a") [(tagAAlt, ["t", "n", "b"], R "b")] }

  -- ## call-argument plumbing
  --
  -- `params` is a division: 0 = static, 1 = dynamic.

  , { name := "splitStatics", params := ["params", "rs"]
    , body := .ite (isNil_ (R "params")) nil_
        (.ite (eq_ (hd_ (R "params")) (K 0))
          (cons_ (C "presVal" [hd_ (R "rs")])
                 (C "splitStatics" [tl_ (R "params"), tl_ (R "rs")]))
          (C "splitStatics" [tl_ (R "params"), tl_ (R "rs")])) }

  , { name := "splitDyns", params := ["params", "rs"]
    , body := .ite (isNil_ (R "params")) nil_
        (.ite (eq_ (hd_ (R "params")) (K 0))
          (C "splitDyns" [tl_ (R "params"), tl_ (R "rs")])
          (cons_ (C "toCode" [hd_ (R "rs")])
                 (C "splitDyns" [tl_ (R "params"), tl_ (R "rs")]))) }

  , { name := "dynCountL", params := ["params"]
    , body := .ite (isNil_ (R "params")) (K 0)
        (.ite (eq_ (hd_ (R "params")) (K 0))
          (C "dynCountL" [tl_ (R "params")])
          (add_ (K 1) (C "dynCountL" [tl_ (R "params")]))) }

  -- the `j`-th dynamic parameter of a specialized function is residual index `j`
  , { name := "buildEnvL", params := ["params", "statics", "j"]
    , body := .ite (isNil_ (R "params")) nil_
        (.ite (eq_ (hd_ (R "params")) (K 0))
          (cons_ (pStat (hd_ (R "statics")))
                 (C "buildEnvL" [tl_ (R "params"), tl_ (R "statics"), R "j"]))
          (cons_ (pDyn (R "j"))
                 (C "buildEnvL" [tl_ (R "params"), R "statics", add_ (R "j") (K 1)]))) }

  -- inlining wraps `k` lets around the body, so the `j`-th dynamic argument
  -- ends up at residual index `k-1-j`
  , { name := "inlineEnvL", params := ["params", "rs", "k", "j"]
    , body := .ite (isNil_ (R "params")) nil_
        (.ite (eq_ (hd_ (R "params")) (K 0))
          (cons_ (pStat (C "presVal" [hd_ (R "rs")]))
                 (C "inlineEnvL" [tl_ (R "params"), tl_ (R "rs"), R "k", R "j"]))
          (cons_ (pDyn (sub_ (sub_ (R "k") (K 1)) (R "j")))
                 (C "inlineEnvL" [tl_ (R "params"), tl_ (R "rs"), R "k",
                                  add_ (R "j") (K 1)]))) }

  , { name := "wrapLetsL", params := ["es", "body"]
    , body := .ite (isNil_ (R "es")) (R "body")
                   (eLetIn (hd_ (R "es")) (C "wrapLetsL" [tl_ (R "es"), R "body"])) }

  -- ## the memo table

  , { name := "indexOfReqL", params := ["reqs", "r", "i"]
    , body := .ite (isNil_ (R "reqs")) (K (-1))
        (.ite (P2 .eqV (hd_ (R "reqs")) (R "r")) (R "i")
              (C "indexOfReqL" [tl_ (R "reqs"), R "r", add_ (R "i") (K 1)])) }

  , { name := "memberReqL", params := ["reqs", "r"]
    , body := .ite (isNil_ (R "reqs")) (bool false)
        (.ite (P2 .eqV (hd_ (R "reqs")) (R "r")) (bool true)
              (C "memberReqL" [tl_ (R "reqs"), R "r"])) }

  , { name := "addNewL", params := ["seen", "rq"]
    , body := .ite (isNil_ (R "rq")) nil_
        (.ite (C "memberReqL" [R "seen", hd_ (R "rq")])
              (C "addNewL" [R "seen", tl_ (R "rq")])
              (cons_ (hd_ (R "rq"))
                     (C "addNewL" [C "appendL" [R "seen", cons_ (hd_ (R "rq")) nil_],
                                   tl_ (R "rq")]))) }

  -- ## the term walk
  --
  -- Returns `(PRes, requests)`.  Mirrors `mixTerm` case for case.

  , { name := "mixTerm", params := ["A", "reqs", "D", "env", "t"]
    , body := .switch (R "t") mixTermAlts }

  , { name := "mapPStat", params := ["vs"]
    , body := .ite (isNil_ (R "vs")) nil_
                   (cons_ (pStat (hd_ (R "vs"))) (C "mapPStat" [tl_ (R "vs")])) }

  , { name := "mixTerms", params := ["A", "reqs", "D", "env", "ts"]
    , body := .ite (isNil_ (R "ts")) (pair_ nil_ nil_)
        (.letN "o1" (C "mixTerm"  [R "A", R "reqs", R "D", R "env", hd_ (R "ts")]) <|
         .letN "o2" (C "mixTerms" [R "A", R "reqs", R "D", R "env", tl_ (R "ts")]) <|
         pair_ (cons_ (fst_ (R "o1")) (fst_ (R "o2")))
               (C "appendL" [snd_ (R "o1"), snd_ (R "o2")])) }

  , { name := "mixAlts", params := ["A", "reqs", "D", "env", "as"]
    , body := .ite (isNil_ (R "as")) (pair_ nil_ nil_)
        (.letN "a"  (hd_ (R "as")) <|
         .letN "ar" (C "altArity" [R "a"]) <|
         .letN "o1" (C "mixTerm"
             [R "A", R "reqs",
              C "appendL" [C "replicateL" [R "ar", K 1], R "D"],
              C "appendL" [C "freshFrom" [K 0, R "ar"], C "shiftEnv" [R "ar", R "env"]],
              C "altBody" [R "a"]]) <|
         .letN "o2" (C "mixAlts" [R "A", R "reqs", R "D", R "env", tl_ (R "as")]) <|
         pair_ (cons_ (eAlt (.switch (R "a") [(tagAAlt, ["t", "n", "b"], R "t")])
                            (R "ar") (C "toCode" [fst_ (R "o1")]))
                      (fst_ (R "o2")))
               (C "appendL" [snd_ (R "o1"), snd_ (R "o2")])) }

  -- ## specializing one function

  , { name := "reqFun", params := ["r"]
    , body := .switch (R "r") [(tagReq, ["f", "vs"], R "f")] }
  , { name := "reqArgs", params := ["r"]
    , body := .switch (R "r") [(tagReq, ["f", "vs"], R "vs")] }

  , { name := "mixFun", params := ["A", "reqs", "req"]
    , body :=
        .letN "fd" (C "fnOf" [R "A", C "reqFun" [R "req"]]) <|
        .letN "ps" (C "funParams" [R "fd"]) <|
        .letN "o"  (C "mixTerm"
            [R "A", R "reqs", R "ps",
             C "buildEnvL" [R "ps", C "reqArgs" [R "req"], K 0],
             C "funBody" [R "fd"]]) <|
        pair_ (eFunDef (C "dynCountL" [R "ps"]) (C "toCode" [fst_ (R "o")]))
              (snd_ (R "o")) }

  -- ## the driver
  --
  -- Discovery passes `seen` as the memo table, so a request it has not reached
  -- yet resolves to index -1.  That code is thrown away; only generation, which
  -- runs against the complete list, is kept.

  , { name := "discoverL", params := ["A", "work", "seen"]
    , body := .ite (isNil_ (R "work")) (R "seen")
        (.letN "o" (C "mixFun" [R "A", R "seen", hd_ (R "work")]) <|
         .letN "fresh" (C "addNewL" [R "seen", snd_ (R "o")]) <|
         C "discoverL" [R "A", C "appendL" [tl_ (R "work"), R "fresh"],
                        C "appendL" [R "seen", R "fresh"]]) }

  , { name := "generateL", params := ["A", "reqs", "todo"]
    , body := .ite (isNil_ (R "todo")) nil_
        (cons_ (fst_ (C "mixFun" [R "A", R "reqs", hd_ (R "todo")]))
               (C "generateL" [R "A", R "reqs", tl_ (R "todo")])) }

  , { name := "mixDriver", params := ["A", "statics"]
    , body :=
        .letN "req0" (mkReq (C "progEntry" [R "A"]) (R "statics")) <|
        .letN "reqs" (C "discoverL" [R "A", cons_ (R "req0") nil_,
                                     cons_ (R "req0") nil_]) <|
        eProgram (C "generateL" [R "A", R "reqs", R "reqs"]) (K 0) }
  ]

/-! ## Resolution -/

def mixResolved : Except String (Program × List Bool) := resolveProgram mixS

def mixProgram : Program :=
  match mixResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩

def mixInline : List Bool :=
  match mixResolved with | .ok (_, i) => i | .error _ => []

end MixProg
end Projection
