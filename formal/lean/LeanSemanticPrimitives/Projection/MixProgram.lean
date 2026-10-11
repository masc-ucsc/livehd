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
def tagPCons : Nat := 62   -- PEnv entry: a PRESERVED cons spine
def tagRStat : Nat := 70   -- PRes: a value
def tagRCode : Nat := 71   -- PRes: residual code
def tagRCons : Nat := 72   -- PRes: a partial cons
def tagRLets : Nat := 73   -- PRes: a package, bindings plus a result
def tagRVal  : Nat := 74   -- PRes: a CARRIED partial value (`PRes.val`)
def tagReq   : Nat := 80   -- SpecRequest

/-! ## The `nthD` lookup summary's template -- shared with the host

`isNthD` (PartialEvaluator.lean) is a pattern match on exactly these terms;
`isNthDL` below compares the ENCODED callee with their `encATerm` encoding.
`Proto/LookupMirrorCheck.lean` checks that the two recognizers agree on every
function of three annotated programs. -/
def nthCondT : ATerm := .prim .stat .eqI [.var 1, .lit (.int 0)]
def nthBaseT : ATerm := .prim .dyn .hd [.var 0]
def nthArgsT : List ATerm := [.prim .dyn .tl [.var 0], .prim .stat .subI [.var 1, .lit (.int 1)]]

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

/-- Triples, for `mixPArgsL`, which returns bindings, the callee environment
and requests. -/
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
private def pCons (a b : SExp) := SExp.mk tagPCons [a, b]
private def rStat (v : SExp) := SExp.mk tagRStat [v]
private def rCode (t : SExp) := SExp.mk tagRCode [t]
private def rCons (a b : SExp) := SExp.mk tagRCons [a, b]
private def rLets (bs r : SExp) := SExp.mk tagRLets [bs, r]
private def rVal (v : SExp) := SExp.mk tagRVal [v]

private def true_  : SExp := .lit (.bool true)
private def false_ : SExp := .lit (.bool false)
private def and_ (a b : SExp) := P2 .andB a b
private def ctorTag_ (e : SExp) := P1 .ctorTagP e
private def ctorFields_ (e : SExp) := P1 .ctorFieldsP e

/-- `Option` in `L`: `nil` is `none`, a one-element list is `some`.  Used by the
peels, which may decline to answer. -/
private def none_ : SExp := nil_
private def some_ (e : SExp) := cons_ e nil_
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
  , (27, .prim .ctorFieldsP [pA])
  , (28, .prim .bvSra [pA, pB, pC]), (29, .prim .bvGetMask [pA, pB, pC])
  , (30, .prim .bvSint [pA]), (31, .prim .bvShl [pA, pB, pC]) ]

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
              -- a dyn entry reads back as `var k`, a preserved spine is CARRIED
              -- as `.val` rather than copied; `ofPVal` is the one rule for both,
              -- mirroring the host's `var` rule and `PRes.ofPVal`
              (pair_ (C "ofPVal" [R "e"]) nil_))

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
              -- a guarded structural answer if the spine supports one,
              -- otherwise the opaque residual node
              (.letN "st" (C "primStructL" [R "p", R "rs"]) <|
               .ite (isNil_ (R "st"))
                 (pair_ (rCode (ePrim (R "p") (C "mapToCode" [R "rs"]))) (snd_ (R "o")))
                 (pair_ (hd_ (R "st")) (snd_ (R "o")))))

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
              -- inline: transfer the arguments in one pass, then specialize the
              -- body in the environment that pass built.  The binding travels
              -- WITH the body rather than around its reified code, so a partial
              -- body keeps its spine on the way out.
              (.letN "u" (C "mixPArgsL"
                  [R "A", R "reqs", R "D", R "env", R "ps", R "ts"]) <|
               -- the lookup summary, on the environment just built; `none` unfolds
               .letN "sm" (.ite (C "isNthDL" [R "fd", R "f"]) (C "nthEnvL" [t2_ (R "u")]) none_) <|
               .ite (isNil_ (R "sm"))
                 (.letN "o2" (C "mixTerm"
                     [R "A", R "reqs", R "ps", t2_ (R "u"), C "funBody" [R "fd"]]) <|
                  pair_ (rLets (t1_ (R "u")) (fst_ (R "o2")))
                        (C "appendL" [t3_ (R "u"), snd_ (R "o2")]))
                 (pair_ (rLets (t1_ (R "u")) (hd_ (R "sm"))) (t3_ (R "u"))))) ]

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

  -- A partial value may now be a SPINE, and every residual index in its leaves
  -- moves when a binder is entered -- so shifting is structural, not a single
  -- arithmetic step on one index.
  , { name := "shiftPV", params := ["k", "v"]
    , body := .switch (R "v")
        [ (tagPStat, ["x"],    pStat (R "x"))
        , (tagPDyn,  ["j"],    pDyn (add_ (R "j") (R "k")))
        , (tagPCons, ["a","b"], pCons (C "shiftPV" [R "k", R "a"])
                                      (C "shiftPV" [R "k", R "b"])) ] }

  , { name := "shiftEnv", params := ["k", "env"]
    , body := .ite (isNil_ (R "env")) nil_
        (cons_ (C "shiftPV" [R "k", hd_ (R "env")])
               (C "shiftEnv" [R "k", tl_ (R "env")])) }

  -- `PRes.ofPVal`: reading a preserved spine back out as a partial RESULT.
  -- A `dyn` entry is a `var`, and a spine is CARRIED in O(1) as `.val` rather
  -- than copied.  This replaced `pvToPRes`, which copied the spine into a
  -- chain of `rCons` -- the host's `var` rule no longer calls `PVal.toPRes`, so
  -- nothing here calls its mirror and it was removed rather than left dead.
  , { name := "ofPVal", params := ["v"]
    , body := .switch (R "v")
        [ (tagPStat, ["x"],     rStat (R "x"))
        , (tagPDyn,  ["k"],     rCode (eVar (R "k")))
        , (tagPCons, ["a","b"], rVal (R "v")) ] }

  -- `PVal.toCode`: reify a partial value directly
  , { name := "pvToCode", params := ["v"]
    , body := .switch (R "v")
        [ (tagPStat, ["x"],     eLit (R "x"))
        , (tagPDyn,  ["k"],     eVar (R "k"))
        , (tagPCons, ["a","b"],
            ePrim (K 23) (cons_ (C "pvToCode" [R "a"]) (cons_ (C "pvToCode" [R "b"]) nil_))) ] }

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

  -- ## the `nthD` lookup summary (mirrors `isNthD`, `walkHead`, `nthSummaryEnv`)

  -- the recognizer: params [dyn, stat], body an `ite` whose condition, base and
  -- recursive call are the template -- the `ite`'s own binding time is NOT
  -- compared, exactly as the host ignores it -- and the recursion is to `f`
  , { name := "isNthDL", params := ["fd", "f"]
    , body :=
        .letN "bd" (C "funBody" [R "fd"]) <|
        .ite (P2 .eqV (C "funParams" [R "fd"]) (.lit (encDiv [.dyn, .stat])))
          (.ite (eq_ (ctorTag_ (R "bd")) (K (Int.ofNat tagAIte)))
             (.letN "fs" (ctorFields_ (R "bd")) <|
              and_ (P2 .eqV (hd_ (tl_ (R "fs"))) (.lit (encATerm nthCondT)))
                (and_ (P2 .eqV (hd_ (tl_ (tl_ (R "fs")))) (.lit (encATerm nthBaseT)))
                      (P2 .eqV (hd_ (tl_ (tl_ (tl_ (R "fs")))))
                               (SExp.mk tagAUcall [.lit (encBT .dyn), R "f", .lit (encATerms nthArgsT)]))))
             false_)
          false_ }

  -- walk a KNOWN encoded spine `k` cells; `some` its head there, else `none`
  , { name := "walkHeadL", params := ["v", "k"]
    , body :=
        .ite (eq_ (ctorTag_ (R "v")) (K (Int.ofNat tagPCons)))
          (.letN "fs" (ctorFields_ (R "v")) <|
           .ite (eq_ (R "k") (K 0)) (some_ (hd_ (R "fs")))
                (C "walkHeadL" [hd_ (tl_ (R "fs")), sub_ (R "k") (K 1)]))
          none_ }

  -- the summary, on the environment `mixPArgsL` built for a recognised callee:
  -- [list entry, static index].  Only a `dyn` leaf is answered.  A non-integer
  -- index is a `typeError` here where the host falls back -- and the host's
  -- unfold then fails on `eqI` too, so failure maps to failure.
  , { name := "nthEnvL", params := ["env"]
    , body :=
        .letN "k" (C "pvVal" [hd_ (tl_ (R "env"))]) <|
        .ite (P2 .ltI (R "k") (K 0)) none_
          (.letN "h" (C "walkHeadL" [hd_ (R "env"), R "k"]) <|
           .ite (isNil_ (R "h")) none_
             (.letN "lf" (hd_ (R "h")) <|
              .ite (eq_ (ctorTag_ (R "lf")) (K (Int.ofNat tagPDyn)))
                (some_ (rCode (eVar (C "pvIdx" [R "lf"]))))
                none_)) }

  , { name := "toCode", params := ["r"]
    , body := .switch (R "r")
        [ (tagRStat, ["v"], eLit (R "v"))     -- this is `lift`
        , (tagRCode, ["t"], R "t")
        -- a spine forced into a dynamic context re-emits the `consP` chain
        , (tagRCons, ["a","b"],
            ePrim (K 23) (cons_ (C "toCode" [R "a"]) (cons_ (C "toCode" [R "b"]) nil_)))
        , (tagRLets, ["bs","r2"], C "wrapLetsL" [R "bs", C "toCode" [R "r2"]])
        , (tagRVal,  ["v"],       C "pvToCode" [R "v"]) ] }

  -- ## the discard guard, and the structural answers it licenses

  -- Computation-free: a result that holds no work, so nothing is lost by not
  -- running it.  A residual VARIABLE qualifies; arbitrary residual code and a
  -- package do not.
  , { name := "totalL", params := ["r"]
    , body := .switch (R "r")
        [ (tagRStat, ["v"],    true_)
        , (tagRCode, ["t"],    eq_ (ctorTag_ (R "t")) (K (Int.ofNat tagVar)))
        , (tagRCons, ["a","b"], and_ (C "totalL" [R "a"]) (C "totalL" [R "b"]))
        , (tagRLets, ["bs","r2"], false_)
        -- a partial value's leaves are `stat` or `dyn`: total
        , (tagRVal,  ["v"],       true_) ] }

  -- Each peel sees THROUGH a package and puts it back; the guard applies to the
  -- component being DISCARDED, which is why `hd` tests the tail, `tl` the head,
  -- and `isNil` both.
  , { name := "peelHdL", params := ["r"]
    , body := .switch (R "r")
        [ (tagRStat, ["v"],    none_)
        , (tagRCode, ["t"],    none_)
        , (tagRCons, ["a","b"], .ite (C "totalL" [R "b"]) (some_ (R "a")) none_)
        , (tagRLets, ["bs","r2"],
            .letN "o" (C "peelHdL" [R "r2"]) <|
            .ite (isNil_ (R "o")) none_ (some_ (rLets (R "bs") (hd_ (R "o")))))
        -- a carried spine answers without a guard walk: its parts are total
        , (tagRVal, ["v"],
            .switch (R "v")
              [ (tagPStat, ["x"],     none_)
              , (tagPDyn,  ["k"],     none_)
              , (tagPCons, ["a","b"], some_ (C "ofPVal" [R "a"])) ]) ] }

  , { name := "peelTlL", params := ["r"]
    , body := .switch (R "r")
        [ (tagRStat, ["v"],    none_)
        , (tagRCode, ["t"],    none_)
        , (tagRCons, ["a","b"], .ite (C "totalL" [R "a"]) (some_ (R "b")) none_)
        , (tagRLets, ["bs","r2"],
            .letN "o" (C "peelTlL" [R "r2"]) <|
            .ite (isNil_ (R "o")) none_ (some_ (rLets (R "bs") (hd_ (R "o")))))
        -- a carried spine answers without a guard walk: its parts are total
        , (tagRVal, ["v"],
            .switch (R "v")
              [ (tagPStat, ["x"],     none_)
              , (tagPDyn,  ["k"],     none_)
              , (tagPCons, ["a","b"], some_ (C "ofPVal" [R "b"])) ]) ] }

  , { name := "peelIsNilL", params := ["r"]
    , body := .switch (R "r")
        [ (tagRStat, ["v"],    none_)
        , (tagRCode, ["t"],    none_)
        , (tagRCons, ["a","b"],
            .ite (and_ (C "totalL" [R "a"]) (C "totalL" [R "b"]))
                 (some_ (rStat false_)) none_)
        , (tagRLets, ["bs","r2"],
            .letN "o" (C "peelIsNilL" [R "r2"]) <|
            .ite (isNil_ (R "o")) none_ (some_ (rLets (R "bs") (hd_ (R "o")))))
        -- a carried spine answers without a guard walk: its parts are total
        , (tagRVal, ["v"],
            .switch (R "v")
              [ (tagPStat, ["x"],     none_)
              , (tagPDyn,  ["k"],     none_)
              , (tagPCons, ["a","b"], some_ (rStat false_)) ]) ] }

  -- `consP` discards nothing, so it may always build a spine.
  , { name := "primStructL", params := ["p", "rs"]
    , body :=
        .ite (eq_ (R "p") (K 23))
          (.ite (eq_ (C "lenL" [R "rs"]) (K 2))
            (.letN "a" (hd_ (R "rs")) <|
             .letN "b" (hd_ (tl_ (R "rs"))) <|
             .ite (and_ (eq_ (ctorTag_ (R "a")) (K (Int.ofNat tagRStat)))
                        (eq_ (ctorTag_ (R "b")) (K (Int.ofNat tagRStat))))
               (some_ (rStat (cons_ (C "presVal" [R "a"]) (C "presVal" [R "b"]))))
               (some_ (rCons (R "a") (R "b"))))
            none_)
        (.ite (eq_ (R "p") (K 12))
          (.ite (eq_ (C "lenL" [R "rs"]) (K 1)) (C "peelHdL" [hd_ (R "rs")]) none_)
        (.ite (eq_ (R "p") (K 13))
          (.ite (eq_ (C "lenL" [R "rs"]) (K 1)) (C "peelTlL" [hd_ (R "rs")]) none_)
        (.ite (eq_ (R "p") (K 11))
          (.ite (eq_ (C "lenL" [R "rs"]) (K 1)) (C "peelIsNilL" [hd_ (R "rs")]) none_)
          none_))) }

  -- ## preparation

  -- Turn a result into BINDINGS plus a partial value whose `dyn` leaves index
  -- them.  Arbitrary code becomes one binding; a reference becomes none; a
  -- spine survives, and only the side that emitted bindings shifts.
  , { name := "prepareL", params := ["r"]
    , body := .switch (R "r")
        [ (tagRStat, ["v"], pair_ nil_ (pStat (R "v")))
        , (tagRCode, ["t"],
            .ite (eq_ (ctorTag_ (R "t")) (K (Int.ofNat tagVar)))
              (pair_ nil_ (pDyn (hd_ (ctorFields_ (R "t")))))
              (pair_ (cons_ (R "t") nil_) (pDyn (K 0))))
        , (tagRCons, ["a","b"],
            .letN "pa" (C "prepareL" [R "a"]) <|
            .letN "pb" (C "prepareL" [R "b"]) <|
            .ite (isNil_ (fst_ (R "pa")))
              (pair_ (fst_ (R "pb"))
                     (pCons (C "shiftPV" [C "lenL" [fst_ (R "pb")], snd_ (R "pa")])
                            (snd_ (R "pb"))))
              (.ite (isNil_ (fst_ (R "pb")))
                (pair_ (fst_ (R "pa"))
                       (pCons (snd_ (R "pa"))
                              (C "shiftPV" [C "lenL" [fst_ (R "pa")], snd_ (R "pb")])))
                (pair_ (cons_ (C "toCode" [rCons (R "a") (R "b")]) nil_) (pDyn (K 0)))))
        , (tagRLets, ["bs","r2"],
            .letN "p" (C "prepareL" [R "r2"]) <|
            pair_ (C "appendL" [R "bs", fst_ (R "p")]) (snd_ (R "p")))
        -- already a partial value: nothing to bind, nothing to copy
        , (tagRVal, ["v"], pair_ nil_ (R "v")) ] }

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
  -- Argument transfer for an unfolded call, in ONE pass.  Returns the emitted
  -- bindings, the callee environment, and the requests -- so the binder depth
  -- is `length bindings` by construction instead of an arithmetic count that
  -- has to be kept in step with a second function.
  --
  -- A static parameter emits no binding, so nothing after it shifts.  A dynamic
  -- one is PREPARED: its code leaves become bindings and its partial structure
  -- survives, so the tail is mixed under however many bindings that actually
  -- was -- zero, one, or several.
  , { name := "mixPArgsL", params := ["A", "reqs", "D", "env", "params", "ts"]
    , body := .ite (isNil_ (R "params")) (triple_ nil_ nil_ nil_)
        (.letN "o1" (C "mixTerm" [R "A", R "reqs", R "D", R "env", hd_ (R "ts")]) <|
         .ite (eq_ (hd_ (R "params")) (K 0))
           (.letN "o2" (C "mixPArgsL"
               [R "A", R "reqs", R "D", R "env", tl_ (R "params"), tl_ (R "ts")]) <|
            triple_ (t1_ (R "o2"))
                    (cons_ (pStat (C "presVal" [fst_ (R "o1")])) (t2_ (R "o2")))
                    (C "appendL" [snd_ (R "o1"), t3_ (R "o2")]))
           (.letN "pp" (C "prepareL" [fst_ (R "o1")]) <|
            .letN "bs0" (fst_ (R "pp")) <|
            .letN "o2" (C "mixPArgsL"
               [R "A", R "reqs", R "D", C "shiftEnv" [C "lenL" [R "bs0"], R "env"],
                tl_ (R "params"), tl_ (R "ts")]) <|
            triple_ (C "appendL" [R "bs0", t1_ (R "o2")])
                    (cons_ (C "shiftPV" [C "lenL" [t1_ (R "o2")], snd_ (R "pp")])
                           (t2_ (R "o2")))
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
