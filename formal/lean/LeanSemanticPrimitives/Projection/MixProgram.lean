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

/-- Triples, for `mixUArgsL`, which returns results, argument codes and requests. -/
private def triple_ (a b c : SExp) := cons_ a (cons_ b c)
private def t1_ (e : SExp) := hd_ e
private def t2_ (e : SExp) := hd_ (tl_ e)
private def t3_ (e : SExp) := tl_ (tl_ e)

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
            -- the DIVISION decides which kind of entry this is, and the
            -- division is static; switching on the entry itself would put a tag
            -- test in the generated compiler for every variable reference
            .letN "e" (C "nthE" [R "i", R "env"]) <|
            .ite (eq_ (C "nthS" [R "i", R "D"]) (K 0))
              (pair_ (rStat (C "pvVal" [R "e"])) nil_)
              (pair_ (rCode (eVar (C "pvIdx" [R "e"]))) nil_))

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
              (.letN "o2" (C "mixCaseSel"
                   [R "A", R "reqs", R "D", R "env", R "as", C "presVal" [R "r1"]])
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
               pair_ (rCode (eCall (C "indexOfReqL" [R "reqs", R "req"])
                                   (C "splitDyns" [R "ps", R "rs"])))
                     (C "appendL" [R "q", cons_ (R "req") nil_])))

        , (tagAUcall, ["b", "f", "ts"],
            .letN "fd" (C "fnOf" [R "A", R "f"]) <|
            .letN "ps" (C "funParams" [R "fd"]) <|
            .ite (eq_ (R "b") (K 0))
              (.letN "o" (C "mixTerms" [R "A", R "reqs", R "D", R "env", R "ts"]) <|
               .letN "o2" (C "mixTerm"
                  [R "A", R "reqs", R "ps",
                   C "mapPStat" [C "allStaticL" [fst_ (R "o")]], C "funBody" [R "fd"]]) <|
               pair_ (fst_ (R "o2")) (C "appendL" [snd_ (R "o"), snd_ (R "o2")]))
              -- inline: bind each dynamic argument once, then specialize the
              -- body.  The arguments go through mixUArgsL, which threads the
              -- residual scope -- argument j sits under the binders of the
              -- dynamic arguments before it.
              (.letN "u" (C "mixUArgsL"
                  [R "A", R "reqs", R "D", R "env", R "ps", R "ts"]) <|
               .letN "dts" (t2_ (R "u")) <|
               .letN "o2" (C "mixTerm"
                   [R "A", R "reqs", R "ps",
                    C "inlineEnvL" [R "ps", t1_ (R "u")],
                    C "funBody" [R "fd"]]) <|
               pair_ (rCode (C "wrapLetsL" [R "dts", C "toCode" [fst_ (R "o2")]]))
                     (C "appendL" [t3_ (R "u"), snd_ (R "o2")]))) ]

/-! ## The program -/

def mixS : SProgram where
  entry := "mixDriver"
  funs := [

  -- ## generic list helpers

  -- `nth`, TWICE.  Recursion is on `i`, which is static everywhere, so both
  -- copies unroll -- but the LIST differs: `nthS` indexes the function table and
  -- the division, which are static, while `nthE` indexes the partial
  -- environment, whose contents are not.  One shared copy is forced dynamic by
  -- the environment use, and then `fnOf` returns a dynamic function definition,
  -- `funBody` a dynamic term, and `mixTerm`'s term parameter goes dynamic --
  -- at which point the specializer specializes nothing.  Same reason as
  -- `appendD`, and the same general fix (polyvariant BTA) would remove it.
  { name := "nthS", params := ["i", "l"]
  , body := .ite (eq_ (R "i") (K 0)) (hd_ (R "l"))
                 (C "nthS" [sub_ (R "i") (K 1), tl_ (R "l")]) }

  , { name := "nthE", params := ["i", "l"]
    , body := .ite (eq_ (R "i") (K 0)) (hd_ (R "l"))
                   (C "nthE" [sub_ (R "i") (K 1), tl_ (R "l")]) }

  , { name := "appendL", params := ["a", "b"]
    , body := .ite (isNil_ (R "a")) (R "b")
                   (cons_ (hd_ (R "a")) (C "appendL" [tl_ (R "a"), R "b"])) }

  -- A SECOND COPY OF `append`, for divisions only.  Binding-time analysis here
  -- is monovariant -- one division per function -- and `appendL` is applied
  -- both to request lists (dynamic) and to divisions (static), so a single copy
  -- is forced dynamic and drags every division it builds down with it.
  -- Duplicating the helper is the standard remedy; making BTA polyvariant would
  -- fix this class of loss in general.
  , { name := "appendD", params := ["a", "b"]
    , body := .ite (isNil_ (R "a")) (R "b")
                   (cons_ (hd_ (R "a")) (C "appendD" [tl_ (R "a"), R "b"])) }

  -- `length`, twice, for the same reason: `numFuns` measures the static function
  -- table and is a loop bound that has to stay static, while `closeL` measures
  -- the request list.
  , { name := "lenS", params := ["l"]
    , body := .ite (isNil_ (R "l")) (K 0) (add_ (K 1) (C "lenS" [tl_ (R "l")])) }

  , { name := "lenL", params := ["l"]
    , body := .ite (isNil_ (R "l")) (K 0) (add_ (K 1) (C "lenL" [tl_ (R "l")])) }

  , { name := "numFuns", params := ["A"], body := C "lenS" [C "progFuns" [R "A"]] }

  -- ## annotated-program accessors

  , { name := "progFuns", params := ["A"]
    , body := .switch (R "A") [(tagAProgram, ["fs", "e"], R "fs")] }
  , { name := "progEntry", params := ["A"]
    , body := .switch (R "A") [(tagAProgram, ["fs", "e"], R "e")] }
  , { name := "fnOf", params := ["A", "f"]
    , body := C "nthS" [R "f", C "progFuns" [R "A"]] }
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
        , (tagAVar,   ["i"],            C "nthS" [R "i", R "D"])
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

  -- The PEnv entry's SHAPE is fixed by the division, so which of these applies
  -- is a static question even though the payload is not.  Reading the payload
  -- with a one-alternative `switch` keeps that question out of `mixTerm`.
  , { name := "pvVal", params := ["e"]
    , body := .switch (R "e") [(tagPStat, ["v"], R "v")] }
  , { name := "pvIdx", params := ["e"]
    , body := .switch (R "e") [(tagPDyn, ["k"], R "k")] }

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

  , { name := "altTag", params := ["a"]
    , body := .switch (R "a") [(tagAAlt, ["t", "n", "b"], R "t")] }
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

  -- inlining wraps one let per dynamic argument, so the j-th of them ends up at
  -- residual index k-1-j -- written as the number of dynamic parameters STILL
  -- TO COME, which is the same number and is locally computable
  , { name := "inlineEnvL", params := ["params", "rs"]
    , body := .ite (isNil_ (R "params")) nil_
        (.ite (eq_ (hd_ (R "params")) (K 0))
          (cons_ (pStat (C "presVal" [hd_ (R "rs")]))
                 (C "inlineEnvL" [tl_ (R "params"), tl_ (R "rs")]))
          (cons_ (pDyn (C "dynCountL" [tl_ (R "params")]))
                 (C "inlineEnvL" [tl_ (R "params"), tl_ (R "rs")]))) }

  , { name := "wrapLetsL", params := ["es", "body"]
    , body := .ite (isNil_ (R "es")) (R "body")
                   (eLetIn (hd_ (R "es")) (C "wrapLetsL" [tl_ (R "es"), R "body"])) }

  -- ## the memo table

  -- NO ACCUMULATOR.  The natural `indexOfReq reqs r i` carries a counter that
  -- increments in a loop whose termination test (`isNil reqs`) is dynamic.  The
  -- counter is static, so specializing it generates one residual function for
  -- i = 0, 1, 2, … without bound, and the second projection simply never
  -- finishes.  A static parameter that grows in a dynamically-terminated loop
  -- is the classic non-termination of offline partial evaluation; the usual fix
  -- is to generalize the parameter to dynamic, and the better fix here is to
  -- not have it, computing the index on the way back out instead.
  , { name := "indexOfReqL", params := ["reqs", "r"]
    , body := .ite (isNil_ (R "reqs")) (K (-1))
        (.ite (P2 .eqV (hd_ (R "reqs")) (R "r")) (K 0)
          (.letN "k" (C "indexOfReqL" [tl_ (R "reqs"), R "r"]) <|
           .ite (P2 .ltI (R "k") (K 0)) (K (-1)) (add_ (R "k") (K 1)))) }

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

  -- "The trick": select the alternative by walking the STATIC alternative list
  -- and testing tags dynamically, rather than looking the alternative up and
  -- then processing whatever came back.
  --
  -- The two are equivalent when `mix` runs.  They are not equivalent when `mix`
  -- is SPECIALIZED: the scrutinee's tag is a static value of the program being
  -- specialized, which is dynamic one level out, so a lookup returns a dynamic
  -- alternative and `mixTerm` inherits a dynamic term -- at which point nothing
  -- specializes at all.  Walking the list statically keeps `altBody a` static
  -- in each branch; the dynamic tag test is simply residualized, which is
  -- exactly the dispatch a real compiler performs on the source program.
  , { name := "mixCaseSel", params := ["A", "reqs", "D", "env", "as", "sv"]
    -- The exhausted case must EMIT a failing term, not evaluate one.  `mix`
    -- unrolls this walk over the static alternative list and therefore reaches
    -- the end of it at specialization time, even though at run time exactly one
    -- alternative matches -- so a static `hd nil` here fails while specializing
    -- a perfectly good program.  Emitting residual `hd nil` instead reproduces
    -- what the source does when no alternative matches (a `typeError`), in the
    -- one execution that actually gets there.
    , body := .ite (isNil_ (R "as"))
        (pair_ (rCode (ePrim (K 12) (cons_ (eLit nil_) nil_))) nil_)
        (.letN "a"  (hd_ (R "as")) <|
         .letN "ar" (C "altArity" [R "a"]) <|
         .ite (eq_ (C "altTag" [R "a"]) (P1 .ctorTagP (R "sv")))
           (C "mixTerm"
             [R "A", R "reqs",
              C "appendD" [C "replicateL" [R "ar", K 0], R "D"],
              C "appendL" [C "mapPStat" [P1 .ctorFieldsP (R "sv")], R "env"],
              C "altBody" [R "a"]])
           (C "mixCaseSel" [R "A", R "reqs", R "D", R "env", tl_ (R "as"), R "sv"])) }

  -- Arguments of an UNFOLDED call, with the residual scope threaded.
  --
  -- mixTerms mixes every argument in the same environment, which is right
  -- wherever the residual node introduces no binder.  Unfolding wraps one let
  -- per dynamic argument, so argument j sits under the binders of arguments
  -- 0..j-1: mixed in the caller's environment its de Bruijn indices come out
  -- short by the number of preceding dynamic arguments and it reads the wrong
  -- variable.  Static arguments do not shift, so mixing them deeper is
  -- harmless.
  , { name := "mixUArgsL", params := ["A", "reqs", "D", "env", "params", "ts"]
    , body := .ite (isNil_ (R "params")) (triple_ nil_ nil_ nil_)
        (.letN "o1" (C "mixTerm" [R "A", R "reqs", R "D", R "env", hd_ (R "ts")]) <|
         .ite (eq_ (hd_ (R "params")) (K 0))
           (.letN "o2" (C "mixUArgsL"
               [R "A", R "reqs", R "D", R "env", tl_ (R "params"), tl_ (R "ts")]) <|
            triple_ (cons_ (fst_ (R "o1")) (t1_ (R "o2")))
                    (t2_ (R "o2"))
                    (C "appendL" [snd_ (R "o1"), t3_ (R "o2")]))
           (.letN "o2" (C "mixUArgsL"
               [R "A", R "reqs", R "D", C "shiftEnv" [K 1, R "env"],
                tl_ (R "params"), tl_ (R "ts")]) <|
            triple_ (cons_ (fst_ (R "o1")) (t1_ (R "o2")))
                    (cons_ (C "toCode" [fst_ (R "o1")]) (t2_ (R "o2")))
                    (C "appendL" [snd_ (R "o1"), t3_ (R "o2")]))) }

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
              C "appendD" [C "replicateL" [R "ar", K 1], R "D"],
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

  -- `f` IS A SEPARATE, STATIC PARAMETER -- not read out of the request.
  --
  -- This is the change that decides whether the second projection works at all.
  -- Taking `f` from the request makes it dynamic one level out, so `fnOf A f`,
  -- `funParams`, and `funBody` all go dynamic, and `mixTerm` receives a dynamic
  -- term: its `switch` on the term is then residualized and NOTHING
  -- specializes.  Passing `f` alongside keeps the whole chain static, and
  -- `mixTerm` ends up memoized on `(A, Δ, t)` -- one residual function per
  -- interpreter subterm, which is a compiler.
  , { name := "mixFun", params := ["A", "reqs", "f", "args"]
    , body :=
        .letN "fd" (C "fnOf" [R "A", R "f"]) <|
        .letN "ps" (C "funParams" [R "fd"]) <|
        .letN "o"  (C "mixTerm"
            [R "A", R "reqs", R "ps",
             C "buildEnvL" [R "ps", R "args", K 0],
             C "funBody" [R "fd"]]) <|
        pair_ (eFunDef (C "dynCountL" [R "ps"]) (C "toCode" [fst_ (R "o")]))
              (snd_ (R "o")) }

  -- ## the driver
  --
  -- Requests are kept in FUNCTION-MAJOR order, and every pass over them is an
  -- outer loop on the function index (static, so it unrolls) around an inner
  -- loop on the requests (dynamic, so it survives).  That is what lets `f` be
  -- static at the point `mixFun` is called.
  --
  -- The cost is that discovery is a chaotic iteration rather than a worklist:
  -- each round re-specializes every request seen so far.  Rounds are bounded by
  -- the depth of the call graph, and the alternative -- popping from a dynamic
  -- worklist -- is exactly what makes `f` dynamic.

  , { name := "filterFun", params := ["reqs", "f"]
    , body := .ite (isNil_ (R "reqs")) nil_
        (.ite (eq_ (C "reqFun" [hd_ (R "reqs")]) (R "f"))
           (cons_ (hd_ (R "reqs")) (C "filterFun" [tl_ (R "reqs"), R "f"]))
           (C "filterFun" [tl_ (R "reqs"), R "f"])) }

  , { name := "groupByFun", params := ["A", "reqs", "f"]
    , body := .ite (eq_ (R "f") (C "numFuns" [R "A"])) nil_
        (C "appendL" [C "filterFun" [R "reqs", R "f"],
                      C "groupByFun" [R "A", R "reqs", add_ (R "f") (K 1)]]) }

  , { name := "collectFor", params := ["A", "reqs", "f", "todo"]
    , body := .ite (isNil_ (R "todo")) nil_
        (.ite (eq_ (C "reqFun" [hd_ (R "todo")]) (R "f"))
           (C "appendL"
             [snd_ (C "mixFun" [R "A", R "reqs", R "f", C "reqArgs" [hd_ (R "todo")]]),
              C "collectFor" [R "A", R "reqs", R "f", tl_ (R "todo")]])
           (C "collectFor" [R "A", R "reqs", R "f", tl_ (R "todo")])) }

  , { name := "collectAll", params := ["A", "reqs", "f"]
    , body := .ite (eq_ (R "f") (C "numFuns" [R "A"])) nil_
        (C "appendL" [C "collectFor" [R "A", R "reqs", R "f", R "reqs"],
                      C "collectAll" [R "A", R "reqs", add_ (R "f") (K 1)]]) }

  , { name := "closeL", params := ["A", "seen"]
    , body :=
        .letN "s2" (C "groupByFun"
            [R "A",
             C "appendL" [R "seen",
               C "addNewL" [R "seen", C "collectAll" [R "A", R "seen", K 0]]],
             K 0]) <|
        .ite (eq_ (C "lenL" [R "seen"]) (C "lenL" [R "s2"])) (R "seen")
             (C "closeL" [R "A", R "s2"]) }

  , { name := "genFor", params := ["A", "reqs", "f", "todo"]
    , body := .ite (isNil_ (R "todo")) nil_
        (.ite (eq_ (C "reqFun" [hd_ (R "todo")]) (R "f"))
           (cons_ (fst_ (C "mixFun" [R "A", R "reqs", R "f", C "reqArgs" [hd_ (R "todo")]]))
                  (C "genFor" [R "A", R "reqs", R "f", tl_ (R "todo")]))
           (C "genFor" [R "A", R "reqs", R "f", tl_ (R "todo")])) }

  , { name := "genAll", params := ["A", "reqs", "f"]
    , body := .ite (eq_ (R "f") (C "numFuns" [R "A"])) nil_
        (C "appendL" [C "genFor" [R "A", R "reqs", R "f", R "reqs"],
                      C "genAll" [R "A", R "reqs", add_ (R "f") (K 1)]]) }

  -- The entry is no longer residual function 0: function-major order puts
  -- requests for lower-numbered source functions first, so the entry's index
  -- has to be looked up.
  , { name := "mixDriver", params := ["A", "statics"]
    , body :=
        .letN "req0" (mkReq (C "progEntry" [R "A"]) (R "statics")) <|
        .letN "reqs" (C "closeL"
            [R "A", C "groupByFun" [R "A", cons_ (R "req0") nil_, K 0]]) <|
        eProgram (C "genAll" [R "A", R "reqs", K 0])
                 (C "indexOfReqL" [R "reqs", R "req0"]) }
  ]

/-! ## Resolution -/

def mixResolved : Except String (Program × List Bool) := resolveProgram mixS

def mixProgram : Program :=
  match mixResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩

def mixInline : List Bool :=
  match mixResolved with | .ok (_, i) => i | .error _ => []

end MixProg
end Projection
