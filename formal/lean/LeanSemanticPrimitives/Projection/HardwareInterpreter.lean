/-
  `I_hw` -- one hardware cycle, written in the object language.

  Milestone 2 items 1, 3 and 4 of `SIMULATOR_PLAN.md`.  This is the program the
  first projection specializes: `mix(I_hw, encDesign D)` is the simulator for
  `D`, and whether that residual is worth anything depends entirely on how this
  file is written.

  THE SLOT ENVIRONMENT IS THE WHOLE DESIGN PROBLEM.  `interpretDesign` carries
  `rho : Nat -> CertVal`, a function; `L` is first order, so the environment has
  to be data.  It is a cons chain, NEWEST FIRST, and slot `s` is read at depth
  `n - 1 - s` where `n` is the number of slots bound so far.  Both `n` and `s`
  are static -- they come from the certificate -- so `nthD` walks a static
  number of steps down a dynamic list, which is the one shape `ucall` exists to
  unroll.  Building the chain newest-first is what avoids `append`: every slot
  is one `cons` onto the front, never a traversal.

  WHAT THAT COSTS, stated plainly because the next milestone has to deal with
  it: a dep at depth k residualizes to k `tl`s and one `hd`, so a design with N
  slots produces O(N^2) residual plumbing, and the residual still conses its
  environment at run time.  The dispatch is gone -- no tag test, no walk over
  `D.nodes`, which is what the first projection is for and what Gate 0 measures
  -- but this is not yet the straight-line `let` chain the legacy fast model
  emits.  The fix is a specializer-side simplification (`hd (consP a b) => a`,
  `tl (consP a b) => b`), which collapses the whole chain to a single variable
  reference when the elements are already `let`-bound.  That touches
  `PartialEvaluator.lean` and its proof, so it is deliberately NOT bundled here:
  this file first has to be correct, and measured, before it is made fast.

  EVERY HELPER IS `inline`.  The recursions are all driven by a static list from
  the certificate, so they unfold completely and the residual is one function
  rather than a family of tiny ones.  `main` is the exception: it is the entry,
  and it is what gets specialized.

  UNSUPPORTED CONSTRUCTS RETURN A DEFINED, OBVIOUSLY WRONG VALUE rather than
  failing, matching the shared semantics' own convention (`CertVal.asBV` of a
  memory is `mk_bv 0 0`).  Failing would be better here, but `caseT` with no
  matching alternative residualizes to junk rather than to an error -- so an
  explicit wrong answer is the honest choice, and `SupportedByProjection` is
  what actually excludes these designs from the theorem.
-/

import LeanSemanticPrimitives.Projection.OperatorBridge
import LeanSemanticPrimitives.Projection.PartialEvaluator
import LeanSemanticPrimitives.Projection.BTA
import LeanSemanticPrimitives.Projection.Surface

namespace Projection
namespace Hw
open Surface

/-! ## The program -/

@[inline] private def R (x : String) : SExp := .ref x
@[inline] private def P (p : Prim) (es : List SExp) : SExp := .prim p es
@[inline] private def C (f : String) (es : List SExp) : SExp := .call f es

/-- `Op_And`'s code in `DesignEncoding.opCode`.  Named rather than spelled `7`
inline so the two cannot drift apart silently. -/
private def andCode : Int := Int.ofNat (opCode .Op_And)

def hwS : SProgram where
  entry := "main"
  funs := [

  -- main(d, inp, st) : the whole cycle.
  { name := "main", params := ["d", "inp", "st"], inline := false
  , body :=
      .switch (R "d")
        [(tagDesign, ["srcs", "nodes", "outs", "flops", "mems"],
          .switch (R "st")
            [(tagState, ["fq"],
              lets
                [ ("nsrc", C "lenL" [R "srcs"])
                , ("nnod", C "lenL" [R "nodes"])
                , ("nall", P .addI [R "nsrc", R "nnod"])
                , ("env0", C "mkSources" [R "srcs", R "inp", R "fq", nil])
                , ("env",  C "evalNodes" [R "nodes", R "env0", R "nsrc"])
                , ("os",   C "mkOutputs" [R "outs", R "env", R "nall"])
                , ("nf",   C "flopNexts" [R "flops", R "env", R "nall", R "fq", int 0])
                ]
                (.mk tagResult [.mk tagState [R "nf"], R "os"]))])]
  },

  -- length of a static list
  { name := "lenL", params := ["l"], inline := true
  , body := .ite (P .isNil [R "l"]) (int 0)
                 (P .addI [int 1, C "lenL" [P .tl [R "l"]]]) },

  -- nthD(l, i): i static, l dynamic.  No bounds check -- see `RuntimeSized`.
  { name := "nthD", params := ["l", "i"], inline := true
  , body := .ite (P .eqI [R "i", int 0])
                 (P .hd [R "l"])
                 (C "nthD" [P .tl [R "l"], P .subI [R "i", int 1]]) },

  -- slot(env, n, s): read global slot `s` from the newest-first environment
  { name := "slot", params := ["env", "n", "s"], inline := true
  , body := C "nthD" [R "env", P .subI [P .subI [R "n", int 1], R "s"]] },

  -- bv_nonzero, spelled out: no primitive of its own
  { name := "nz", params := ["v"], inline := true
  , body := P .notB [P .eqI [P .bvUint [R "v"], int 0]] },

  -- the source slots, in order, consed newest-first
  { name := "mkSources", params := ["srcs", "inp", "fq", "acc"], inline := true
  , body := .ite (P .isNil [R "srcs"]) (R "acc")
              (C "mkSources"
                 [ P .tl [R "srcs"], R "inp", R "fq"
                 , cons (C "srcVal" [P .hd [R "srcs"], R "inp", R "fq"]) (R "acc") ]) },

  { name := "srcVal", params := ["sd", "inp", "fq"], inline := true
  , body :=
      .switch (R "sd")
        [ (tagSrcInput, ["idx", "w"],
            P .bvResize [R "w", C "nthD" [R "inp", R "idx"]])
        , (tagSrcConst, ["w", "v"],
            P .bvMk [R "w", R "v"])
        , (tagSrcFlopQ, ["idx", "w"],
            P .bvResize [R "w", C "nthD" [R "fq", R "idx"]])
        , (tagSrcFlopQA, ["idx", "w", "ri", "rv", "al"],
            .ite (.ite (R "al")
                       (P .notB [C "nz" [C "nthD" [R "inp", R "ri"]]])
                       (C "nz" [C "nthD" [R "inp", R "ri"]]))
                 (P .bvMk [R "w", R "rv"])
                 (P .bvResize [R "w", C "nthD" [R "fq", R "idx"]]))
          -- memory sources: unsupported, defined-and-wrong (see header)
        , (tagSrcMemImg,   ["i", "aw", "dw"],       P .bvMk [int 0, int 0])
        , (tagSrcMemConst, ["aw", "dw", "ct"],      P .bvMk [int 0, int 0]) ] },

  -- the dense nodes, in order, each consed onto the environment
  { name := "evalNodes", params := ["nodes", "env", "n"], inline := true
  , body := .ite (P .isNil [R "nodes"]) (R "env")
              (C "evalNodes"
                 [ P .tl [R "nodes"]
                 , cons (C "evalNode" [P .hd [R "nodes"], R "env", R "n"]) (R "env")
                 , P .addI [R "n", int 1] ]) },

  { name := "evalNode", params := ["nd", "env", "n"], inline := true
  , body := .switch (R "nd")
              [(tagNode, ["op", "w", "deps", "org"],
                C "applyOp" [R "op", R "w", R "deps", R "env", R "n"])] },

  -- the operator dispatch.  `code` is static, so this `ite` is resolved at
  -- specialization time and does not appear in the residual -- that is the
  -- Gate 0 criterion, applied to hardware.
  { name := "applyOp", params := ["op", "w", "deps", "env", "n"], inline := true
  , body := .switch (R "op")
              [(tagOp, ["code", "pay"],
                .ite (P .eqI [R "code", .lit (.int andCode)])
                     (C "opAnd" [R "w", R "deps", R "env", R "n"])
                     (P .bvMk [R "w", int 0]))] },

  -- Op_And: resize the FIRST operand to the node width, fold the rest in
  -- unchanged.  Mirrors `eval_op` exactly; see `OperatorBridge.evalOp_And_cons`.
  { name := "opAnd", params := ["w", "deps", "env", "n"], inline := true
  , body := .ite (P .isNil [R "deps"]) (P .bvMk [R "w", int 0])
              (C "foldAnd"
                 [ P .tl [R "deps"], R "env", R "n"
                 , P .bvResize [R "w", C "slot" [R "env", R "n", P .hd [R "deps"]]]
                 , R "w" ]) },

  { name := "foldAnd", params := ["deps", "env", "n", "acc", "w"], inline := true
  , body := .ite (P .isNil [R "deps"]) (R "acc")
              (C "foldAnd"
                 [ P .tl [R "deps"], R "env", R "n"
                 , P .bvAnd [R "w", R "acc", C "slot" [R "env", R "n", P .hd [R "deps"]]]
                 , R "w" ]) },

  { name := "mkOutputs", params := ["outs", "env", "n"], inline := true
  , body := .ite (P .isNil [R "outs"]) nil
              (.switch (P .hd [R "outs"])
                 [(tagOutput, ["s", "w"],
                   cons (P .bvResize [R "w", C "slot" [R "env", R "n", R "s"]])
                        (C "mkOutputs" [P .tl [R "outs"], R "env", R "n"]))]) },

  { name := "flopNexts", params := ["flops", "env", "n", "fq", "idx"], inline := true
  , body := .ite (P .isNil [R "flops"]) nil
              (cons (C "flopNext" [P .hd [R "flops"], R "env", R "n", R "fq", R "idx"])
                    (C "flopNexts"
                       [P .tl [R "flops"], R "env", R "n", R "fq",
                        P .addI [R "idx", int 1]])) },

  -- reset before enable; the reset VALUE, not a hardcoded zero; and when
  -- disabled the OLD STATE, read raw -- `srcFlopNext` does not resize that
  -- branch, so neither does this one.
  { name := "flopNext", params := ["f", "env", "n", "fq", "idx"], inline := true
  , body := .switch (R "f")
              [(tagFlop, ["w", "din", "en", "rp", "rv", "al"],
                .ite (C "rstActive" [R "rp", R "al", R "env", R "n"])
                     (P .bvMk [R "w", R "rv"])
                     (.ite (C "enabled" [R "en", R "env", R "n"])
                           (P .bvResize [R "w", C "slot" [R "env", R "n", R "din"]])
                           (C "nthD" [R "fq", R "idx"])))] },

  -- `xor resetActiveLow (bv_nonzero ...)`, with the polarity static
  { name := "rstActive", params := ["rp", "al", "env", "n"], inline := true
  , body := .ite (P .isNil [R "rp"]) (bool false)
              (.ite (R "al")
                    (P .notB [C "nz" [C "slot" [R "env", R "n", P .hd [R "rp"]]]])
                    (C "nz" [C "slot" [R "env", R "n", P .hd [R "rp"]]])) },

  { name := "enabled", params := ["en", "env", "n"], inline := true
  , body := .ite (P .isNil [R "en"]) (bool true)
              (C "nz" [C "slot" [R "env", R "n", P .hd [R "en"]]]) }
  ]

/-! ## Resolution and binding-time analysis -/

def hwResolved : Except String (Program × List Bool) := resolveProgram hwS

#guard hwResolved.toOption.isSome

def hwP : Program := match hwResolved with | .ok (p, _) => p | .error _ => ⟨[], 0⟩
def hwInl : List Bool := match hwResolved with | .ok (_, i) => i | .error _ => []

/-- The division: the certificate is static, the runtime input and state are
dynamic.  Everything else is inferred. -/
def hwA : Except BTAError AProgram := bta hwP hwInl [.stat, .dyn, .dyn] 200

#guard hwA.toOption.isSome

def hwAP : AProgram := match hwA with | .ok a => a | .error _ => ⟨[], 0⟩

#guard wfAProgram hwAP
#guard eraseProgram hwAP == hwP

/-! ## Milestone 2 acceptance: `I_hw` performs one hardware cycle

Run directly -- no specialization yet -- and compared against the SHARED
reference semantics on the same fixtures Milestone 0 and Milestone 1 accepted.
The comparison is against `encResult (interpretDesign ...)` rather than against
a hand-written expected value, so these check the interpreter, not my
arithmetic. -/

namespace Acceptance
open Compiler Projection.Acceptance

def runHw (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : EvalResult :=
  evalFuel 5000 hwP []
    (.call hwP.entry [.lit (encDesign D), .lit (encInput i), .lit (encState s)])

@[inline] def refOf (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : EvalResult :=
  .value (encResult (interpretDesign D i s))

-- combinational
#guard runHw tinyD tinyIn tinySt == refOf tinyD tinyIn tinySt
-- sequential: enabled, disabled (old state held), and reset (which beats enable)
#guard runHw seqD (seqIn 5 1 0) (seqSt 0) == refOf seqD (seqIn 5 1 0) (seqSt 0)
#guard runHw seqD (seqIn 3 0 0) (seqSt 4) == refOf seqD (seqIn 3 0 0) (seqSt 4)
#guard runHw seqD (seqIn 3 1 1) (seqSt 4) == refOf seqD (seqIn 3 1 1) (seqSt 4)

end Acceptance

/-! ## The first projection, on hardware

Milestone 3's definition, here because it is one line and because the checks
below are what justify the milestone's proof being worth writing.  The theorem
(`projectDesign_correct`) is not yet written; these are `#guard`s, and the
distinction is the one `SHARED_SEMANTICS.md` and Gate 0 already draw. -/

def projectDesign (D : Compiler.DesignCert) : Except MixError Program :=
  mixDriver 20000 200 hwAP [encDesign D]

namespace Acceptance
open Compiler Projection.Acceptance

def tinyR : Program := match projectDesign tinyD with | .ok p => p | .error _ => ⟨[], 0⟩
def seqR  : Program := match projectDesign seqD  with | .ok p => p | .error _ => ⟨[], 0⟩

#guard (projectDesign tinyD).toOption.isSome
#guard (projectDesign seqD).toOption.isSome

/-- The residual takes the DYNAMIC arguments only: the design is gone. -/
def runR (R : Program) (i : RuntimeInput) (s : RuntimeState) : EvalResult :=
  evalFuel 5000 R [] (.call R.entry [.lit (encInput i), .lit (encState s)])

#guard runR tinyR tinyIn tinySt == refOf tinyD tinyIn tinySt
#guard runR seqR (seqIn 5 1 0) (seqSt 0) == refOf seqD (seqIn 5 1 0) (seqSt 0)
#guard runR seqR (seqIn 3 0 0) (seqSt 4) == refOf seqD (seqIn 3 0 0) (seqSt 4)
#guard runR seqR (seqIn 3 1 1) (seqSt 4) == refOf seqD (seqIn 3 1 1) (seqSt 4)

-- the design is gone in the literal sense: the residual entry takes two
-- arguments (input, state), not three
#guard tinyR.funs.map FunDef.arity == [2]
#guard seqR.funs.map FunDef.arity  == [2]

/-! ### Gate 0, applied to hardware

The criterion is not "the residual is small" but "the residual does not dispatch
on the design".  Every `caseT` tag that survives is collected below; the only
one is `tagState`, which destructures the RUNTIME state record and has to be
there.  No source tag, no `tagNode`, no `tagOp`, no `tagFlop` -- and therefore
no walk over `D.nodes` and no test on an opcode. -/

private partial def tagsOf : Term → List Nat
  | .caseT s as => (as.map Alt.tag) ++ tagsOf s ++ (as.map (fun a => tagsOf (Alt.body a))).flatten
  | .lit _ | .var _ => []
  | .letIn a b => tagsOf a ++ tagsOf b
  | .ite a b c => tagsOf a ++ tagsOf b ++ tagsOf c
  | .prim _ ts | .ctorT _ ts | .call _ ts => (ts.map tagsOf).flatten

private def tagsIn (R : Program) : List Nat :=
  ((R.funs.map (fun fd => tagsOf fd.body)).flatten).eraseDups

#guard tagsIn tinyR == [tagState]
#guard tagsIn seqR  == [tagState]

-- …whereas the interpreter dispatches on every one of them
#guard [tagDesign, tagState, tagSrcInput, tagSrcConst, tagSrcFlopQ, tagSrcFlopQA,
        tagSrcMemImg, tagSrcMemConst, tagNode, tagOp, tagOutput, tagFlop].all
         (fun t => t ∈ tagsIn hwP)

-- and none of the DESIGN tags survives specialization
#guard [tagDesign, tagSrcInput, tagSrcConst, tagSrcFlopQ, tagSrcFlopQA,
        tagSrcMemImg, tagSrcMemConst, tagNode, tagOp, tagOutput, tagFlop].all
         (fun t => t ∉ tagsIn seqR)

/-- What the residual is made of.  The hardware primitives are the point; the
`consP`/`hd`/`tl` are the environment plumbing the file header flags as the
O(N^2) cost to be removed by a specializer-side simplification.  There is no
`mkCtorP`/`ctorTagP`/`ctorFieldsP`: no reflection survives. -/
private partial def primsOf : Term → List Prim
  | .prim p ts => p :: (ts.map primsOf).flatten
  | .caseT s as => primsOf s ++ (as.map (fun a => primsOf (Alt.body a))).flatten
  | .lit _ | .var _ => []
  | .letIn a b => primsOf a ++ primsOf b
  | .ite a b c => primsOf a ++ primsOf b ++ primsOf c
  | .ctorT _ ts | .call _ ts => (ts.map primsOf).flatten

#guard ((seqR.funs.map (fun fd => primsOf fd.body)).flatten).eraseDups.all
         (fun p => p ∈ [Prim.consP, .bvResize, .hd, .tl, .bvAnd, .notB, .eqI, .bvUint])

-- the two `ite`s left in the sequential residual are the reset and enable
-- tests, which are genuinely runtime conditions; the combinational design has
-- none at all
private partial def countIte : Term → Nat
  | .ite a b c => 1 + countIte a + countIte b + countIte c
  | .caseT s as => countIte s + (as.map (fun a => countIte (Alt.body a))).foldl (·+·) 0
  | .lit _ | .var _ => 0
  | .letIn a b => countIte a + countIte b
  | .prim _ ts | .ctorT _ ts | .call _ ts => (ts.map countIte).foldl (·+·) 0

#guard ((tinyR.funs.map (fun fd => countIte fd.body)).foldl (·+·) 0) == 0
#guard ((seqR.funs.map (fun fd => countIte fd.body)).foldl (·+·) 0) == 2

end Acceptance
end Hw
end Projection
