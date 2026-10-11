/-
  `I_hw` -- one hardware cycle, written in the object language.

  Milestone 2 items 1, 3 and 4 of `SIMULATOR_PLAN.md`.  This is the program the
  first projection specializes: `mix(I_hw, encDesign D)` is the simulator for
  `D`, and whether that residual is worth anything depends entirely on how this
  file is written.

  THE SLOT ENVIRONMENT IS THE WHOLE DESIGN PROBLEM.  `interpretDesign` carries
  `rho : Nat -> CertVal`, a FUNCTION, and `L` is first order, so the environment
  has to be DATA.  That much is forced.  What is NOT forced is the cons chain:
  a first-order language can carry an array, an indexed vector, a tuple, or a
  dedicated slot-store primitive just as well.  The cons chain is the encoding
  this file happens to use, and it is the wrong one at scale.

  As written, the chain is NEWEST FIRST and slot `s` is read at depth
  `n - 1 - s` where `n` is the number of slots bound so far.  Both `n` and `s`
  are static -- they come from the certificate -- so `nthD` walks a static
  number of steps down a dynamic list, which is the one shape `ucall` exists to
  unroll.  Building the chain newest-first is what avoids `append`: every slot
  is one `cons` onto the front, never a traversal.

  WHAT THAT COSTS -- measured, in `Scaling.lean`, not estimated.  A dep at depth
  `k` residualizes to `k` `tl`s and one `hd`, so residual GENERATION is O(N^2)
  for dependency patterns like the ones real dataflow graphs have.  `Scaling.lean`
  pins it exactly: `fanD`, where every read sits at maximum depth by
  construction, produces exactly `N^2` `tl` applications.  The practical ceiling
  is around N = 1000-2000 slots, which already puts DINO's 4,772-node
  `SingleCycleCPU` out of reach.  The dispatch is gone -- no tag test, no walk
  over `D.nodes`, which is what the first projection is for and what Gate 0
  measures -- but this is not yet the straight-line `let` chain the legacy fast
  model emits, and it cannot reach a real design at all.

  AN EARLIER VERSION OF THIS COMMENT PROPOSED THE WRONG FIX, and the correction
  is worth keeping.  It said the cure was a specializer-side rewrite
  `hd (consP a b) => a` / `tl (consP a b) => b`.  That rewrite can never fire:
  the `.ucall .dyn` case wrapped the body in the argument bindings and mapped
  each dynamic parameter to a single `PVal.dyn i`, so the environment reached
  the body as a residual VARIABLE and the `consP` was never syntactically
  adjacent to the `hd`.  Post-processing could not rescue it either, because the
  O(N^2) term had to be built before anything could simplify it.

  The actual fix is a partial VALUE DOMAIN in the specializer -- one that
  preserves a known cons spine with dynamic leaves, so a slot read resolves to a
  single variable reference at specialization time.  That touches
  `PartialEvaluator.lean` and its correctness proof, so it is deliberately not
  bundled here: this file first has to be correct, and measured, before it is
  made fast.

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

/-- Operator codes from `DesignEncoding.opCode`, named rather than spelled
inline so the two cannot drift apart silently. -/
private def andCode     : Int := Int.ofNat (opCode .Op_And)
private def orCode      : Int := Int.ofNat (opCode .Op_Or)
private def sraCode     : Int := Int.ofNat (opCode .Op_SRA)
private def getMaskCode : Int := Int.ofNat (opCode .Op_GetMask)
private def xorCode     : Int := Int.ofNat (opCode .Op_Xor)
private def notCode     : Int := Int.ofNat (opCode .Op_Not)
-- `opCode` ignores the payload, so any witness names the same code
private def sumCode     : Int := Int.ofNat (opCode (.Op_Sum 0))
private def eqCode      : Int := Int.ofNat (opCode .Op_EQ)
private def rorCode     : Int := Int.ofNat (opCode .Op_Ror)
private def muxBoolCode : Int := Int.ofNat (opCode .Op_MuxBool)
private def muxNCode    : Int := Int.ofNat (opCode .Op_MuxN)
private def ultCode     : Int := Int.ofNat (opCode .Op_ULT)
private def ugtCode     : Int := Int.ofNat (opCode .Op_UGT)
private def sltCode     : Int := Int.ofNat (opCode .Op_SLT)
private def sgtCode     : Int := Int.ofNat (opCode .Op_SGT)
private def sextCode    : Int := Int.ofNat (opCode .Op_Sext)
private def shlCode     : Int := Int.ofNat (opCode .Op_SHL)

def hwS : SProgram where
  entry := "main"
  funs := [

  -- main(d, edges, inp, st) : the whole cycle.  `edges` is the third DYNAMIC
  -- input: which declared clock domains fire this step.
  { name := "main", params := ["d", "edges", "inp", "st"], inline := false
  , body :=
      .switch (R "d")
        [(tagDesign, ["srcs", "nodes", "outs", "flops", "mems", "clks"],
          .switch (R "st")
            [(tagState, ["fq"],
              lets
                [ ("nsrc", C "lenL" [R "srcs"])
                , ("nnod", C "lenL" [R "nodes"])
                , ("nall", P .addI [R "nsrc", R "nnod"])
                , ("env0", C "mkSources" [R "srcs", R "inp", R "fq", nil])
                , ("env",  C "evalNodes" [R "nodes", R "env0", R "nsrc"])
                , ("os",   C "mkOutputs" [R "outs", R "env", R "nall"])
                , ("nf",   C "flopNexts"
                             [R "flops", R "edges", R "env", R "nall", R "fq", int 0])
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
  -- The opcode is STATIC -- it comes out of the certificate -- so this chain is
  -- decided during specialization and no dispatch survives into the residual.
  -- That is the Gate 0 property, and it is why one `ite` per supported operator
  -- costs the residual nothing.
  --
  -- THE FALLBACK IS A WRONG ANSWER, NOT A REFUSAL.  An unsupported operator
  -- yields `mk_bv w 0` here, exactly as it does in `eval_op`'s own catch-all,
  -- so the two agree -- but neither refuses.  Excluding such designs is
  -- `SupportedByProjection`'s job in Milestone 2 and is not done yet.
  { name := "applyOp", params := ["op", "w", "deps", "env", "n"], inline := true
  , body := .switch (R "op")
              [(tagOp, ["code", "pay"],
                .ite (P .eqI [R "code", .lit (.int andCode)])
                  (C "opAnd" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int orCode)])
                  (C "opOr" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int sraCode)])
                  (C "opSra" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int getMaskCode)])
                  (C "opGetMask" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int xorCode)])
                  (C "opXor" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int notCode)])
                  (C "opNot" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int sumCode)])
                  -- the ONLY operator that reads the opcode payload
                  (C "opSum" [R "w", R "pay", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int eqCode)])
                  (C "opEq" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int rorCode)])
                  (C "opRor" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int muxBoolCode)])
                  (C "opMuxBool" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int muxNCode)])
                  (C "opMuxN" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int ultCode)])
                  (C "opCmp" [R "w", R "deps", R "env", R "n", bool false, bool false])
                (.ite (P .eqI [R "code", .lit (.int ugtCode)])
                  (C "opCmp" [R "w", R "deps", R "env", R "n", bool false, bool true])
                (.ite (P .eqI [R "code", .lit (.int sltCode)])
                  (C "opCmp" [R "w", R "deps", R "env", R "n", bool true, bool false])
                (.ite (P .eqI [R "code", .lit (.int sgtCode)])
                  (C "opCmp" [R "w", R "deps", R "env", R "n", bool true, bool true])
                (.ite (P .eqI [R "code", .lit (.int sextCode)])
                  (C "opSext" [R "w", R "deps", R "env", R "n"])
                (.ite (P .eqI [R "code", .lit (.int shlCode)])
                  (C "opShl" [R "w", R "deps", R "env", R "n"])
                  (P .bvMk [R "w", int 0])))))))))))))))))) ] },

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

  -- Op_Or: NOT Op_And's shape.  Seeds with `mk_bv w 0` and folds in EVERY
  -- operand including the first -- there is no `bv_resize` of a head operand
  -- here (`LGraphModel.lean:163-164`, `OperatorBridge.evalOp_Or_fold`).
  { name := "opOr", params := ["w", "deps", "env", "n"], inline := true
  , body := C "foldOr" [R "deps", R "env", R "n", P .bvMk [R "w", int 0], R "w"] },

  { name := "foldOr", params := ["deps", "env", "n", "acc", "w"], inline := true
  , body := .ite (P .isNil [R "deps"]) (R "acc")
              (C "foldOr"
                 [ P .tl [R "deps"], R "env", R "n"
                 , P .bvOr [R "w", R "acc", C "slot" [R "env", R "n", P .hd [R "deps"]]]
                 , R "w" ]) },

  -- Op_Xor: Op_Or's shape with `xor` (`OperatorBridge.evalOp_Xor_fold`).
  { name := "opXor", params := ["w", "deps", "env", "n"], inline := true
  , body := C "foldXor" [R "deps", R "env", R "n", P .bvMk [R "w", int 0], R "w"] },

  { name := "foldXor", params := ["deps", "env", "n", "acc", "w"], inline := true
  , body := .ite (P .isNil [R "deps"]) (R "acc")
              (C "foldXor"
                 [ P .tl [R "deps"], R "env", R "n"
                 , P .bvXor [R "w", R "acc", C "slot" [R "env", R "n", P .hd [R "deps"]]]
                 , R "w" ]) },

  -- Op_Not is strictly unary.
  { name := "opNot", params := ["w", "deps", "env", "n"], inline := true
  , body := P .bvNot [R "w", C "slot" [R "env", R "n", P .hd [R "deps"]]] },

  -- Op_Sum: the first `k` operands are ADDED and the rest SUBTRACTED, all
  -- through `bv_uint`, with the node width applied ONCE at the end -- so the
  -- truncation is of the sum, not of each term.  `k` is the opcode payload and
  -- therefore static, so both walks unroll.
  { name := "opSum", params := ["w", "pay", "deps", "env", "n"], inline := true
  , body := P .bvMk
              [ R "w"
              , P .subI [ C "sumAdds" [R "pay", R "deps", R "env", R "n"]
                        , C "sumSubs" [R "pay", R "deps", R "env", R "n"] ] ] },

  { name := "sumAdds", params := ["k", "deps", "env", "n"], inline := true
  , body := .ite (P .isNil [R "deps"]) (int 0)
              (.ite (P .eqI [R "k", int 0]) (int 0)
                 (P .addI
                    [ P .bvUint [C "slot" [R "env", R "n", P .hd [R "deps"]]]
                    , C "sumAdds"
                        [P .subI [R "k", int 1], P .tl [R "deps"], R "env", R "n"] ])) },

  { name := "sumSubs", params := ["k", "deps", "env", "n"], inline := true
  , body := .ite (P .isNil [R "deps"]) (int 0)
              (.ite (P .eqI [R "k", int 0])
                 (P .addI
                    [ P .bvUint [C "slot" [R "env", R "n", P .hd [R "deps"]]]
                    , C "sumSubs" [int 0, P .tl [R "deps"], R "env", R "n"] ])
                 (C "sumSubs"
                    [P .subI [R "k", int 1], P .tl [R "deps"], R "env", R "n"])) },

  -- Op_EQ: an EMPTY operand list is 1, not 0; otherwise every operand is
  -- compared against the FIRST (`OperatorBridge.evalOp_EQ_cons`).  The answer
  -- depends on operand VALUES, so a comparison legitimately survives into the
  -- residual -- what must not survive is the certificate's own structure.
  { name := "opEq", params := ["w", "deps", "env", "n"], inline := true
  , body := .ite (P .isNil [R "deps"]) (P .bvMk [R "w", int 1])
              (.ite (C "eqAll"
                       [ P .tl [R "deps"], R "env", R "n"
                       , P .bvUint [C "slot" [R "env", R "n", P .hd [R "deps"]]] ])
                    (P .bvMk [R "w", int 1])
                    (P .bvMk [R "w", int 0])) },

  { name := "eqAll", params := ["deps", "env", "n", "a"], inline := true
  , body := .ite (P .isNil [R "deps"]) (bool true)
              (P .andB
                 [ P .eqI [ P .bvUint [C "slot" [R "env", R "n", P .hd [R "deps"]]]
                          , R "a" ]
                 , C "eqAll" [P .tl [R "deps"], R "env", R "n", R "a"] ]) },

  -- Op_Ror is a REDUCTION -- any operand nonzero -- yielding 1 or 0.  It is
  -- NOT a bitwise `Op_Or`, and the node width is 1 in every emitted
  -- certificate for exactly that reason.
  { name := "opRor", params := ["w", "deps", "env", "n"], inline := true
  , body := .ite (C "anyNz" [R "deps", R "env", R "n"])
              (P .bvMk [R "w", int 1])
              (P .bvMk [R "w", int 0]) },

  { name := "anyNz", params := ["deps", "env", "n"], inline := true
  , body := .ite (P .isNil [R "deps"]) (bool false)
              (P .orB
                 [ C "nz" [C "slot" [R "env", R "n", P .hd [R "deps"]]]
                 , C "anyNz" [P .tl [R "deps"], R "env", R "n"] ]) },

  -- Op_MuxBool: operands are [sel, false_v, true_v] IN THAT ORDER, so the
  -- nonzero branch takes the THIRD.  A polarity slip here is a silent swap,
  -- which is why both directions are pinned by vectors.
  { name := "opMuxBool", params := ["w", "deps", "env", "n"], inline := true
  , body := .ite (C "nz" [C "slot" [R "env", R "n", P .hd [R "deps"]]])
              (P .bvResize
                 [R "w", C "slot" [R "env", R "n", P .hd [P .tl [P .tl [R "deps"]]]]])
              (P .bvResize
                 [R "w", C "slot" [R "env", R "n", P .hd [P .tl [R "deps"]]]]) },

  -- Op_MuxN: the FIRST operand is the selector and the rest are indexed from
  -- 0; out of range answers zero.  The dep list is STATIC and the index
  -- counter `k` is static, so this unrolls into a chain of `ite` on the
  -- selector -- a DIRECT selection.  No runtime list is walked and no `nth`
  -- chain is rebuilt: the only dynamic value here is the selector itself.
  { name := "opMuxN", params := ["w", "deps", "env", "n"], inline := true
  , body := .ite (P .isNil [R "deps"]) (P .bvMk [R "w", int 0])
              (C "muxPick"
                 [ R "w", P .tl [R "deps"], R "env", R "n"
                 , P .bvUint [C "slot" [R "env", R "n", P .hd [R "deps"]]]
                 , int 0 ]) },

  { name := "muxPick", params := ["w", "args", "env", "n", "sel", "k"], inline := true
  , body := .ite (P .isNil [R "args"]) (P .bvMk [R "w", int 0])
              (.ite (P .eqI [R "sel", R "k"])
                    (P .bvResize [R "w", C "slot" [R "env", R "n", P .hd [R "args"]]])
                    (C "muxPick"
                       [ R "w", P .tl [R "args"], R "env", R "n", R "sel"
                       , P .addI [R "k", int 1] ])) },

  -- Op_SHL is NOT an accumulator shift.  The FIRST operand is shifted
  -- INDEPENDENTLY by each remaining operand -- `a` is read once and never
  -- moves -- and those copies are XOR-folded into a ZERO seed.  So a
  -- one-operand node is 0, not `a`.  (`OperatorBridge.evalOp_SHL_cons`.)
  { name := "opShl", params := ["w", "deps", "env", "n"], inline := true
  , body := .ite (P .isNil [R "deps"]) (P .bvMk [R "w", int 0])
              (C "foldShl"
                 [ P .tl [R "deps"], R "env", R "n"
                 , P .bvMk [R "w", int 0]
                 , C "slot" [R "env", R "n", P .hd [R "deps"]]
                 , R "w" ]) },

  { name := "foldShl", params := ["bs", "env", "n", "acc", "a", "w"], inline := true
  , body := .ite (P .isNil [R "bs"]) (R "acc")
              (C "foldShl"
                 [ P .tl [R "bs"], R "env", R "n"
                 , P .bvXor
                     [ R "w", R "acc"
                     , P .bvShl
                         [R "w", R "a", C "slot" [R "env", R "n", P .hd [R "bs"]]] ]
                 , R "a", R "w" ]) },

  -- The four comparisons differ along exactly two STATIC axes: which reading
  -- of the bits (`signed`: 0 unsigned, 1 signed) and which way round
  -- (`swap`: 0 for LT, 1 for GT).  There is no "greater" primitive -- GT is LT
  -- with the operands exchanged, which is how the pinned model writes it too.
  -- Both axes come from the opcode, so both are decided during specialization
  -- and the residual holds one comparison.
  { name := "opCmp", params := ["w", "deps", "env", "n", "signed", "swap"], inline := true
  , body := .ite (C "cmpLt"
                    [ R "signed", R "swap"
                    , C "slot" [R "env", R "n", P .hd [R "deps"]]
                    , C "slot" [R "env", R "n", P .hd [P .tl [R "deps"]]] ])
              (P .bvMk [R "w", int 1])
              (P .bvMk [R "w", int 0]) },

  { name := "cmpLt", params := ["signed", "swap", "a", "b"], inline := true
  , body := .ite (R "signed")
              (.ite (R "swap")
                    (P .ltI [P .bvSint [R "b"], P .bvSint [R "a"]])
                    (P .ltI [P .bvSint [R "a"], P .bvSint [R "b"]]))
              (.ite (R "swap")
                    (P .ltI [P .bvUint [R "b"], P .bvUint [R "a"]])
                    (P .ltI [P .bvUint [R "a"], P .bvUint [R "b"]])) },

  -- Op_Sext: operands are [a, amount], and sign extension IS "truncate to the
  -- low `n` bits, then read those bits as SIGNED" -- `bv_sint (bv_resize n a)`.
  -- The pinned body spells that out as a power/mod/sign formula; nothing here
  -- re-derives it, and `OperatorBridge.evalOp_Sext` carries the obligation.
  -- `n = 0` needs no special case: `bv_resize 0` has width 0 and `bv_sint` of a
  -- width-0 vector is 0, which is the pinned answer.
  { name := "opSext", params := ["w", "deps", "env", "n"], inline := true
  , body := P .bvMk
              [ R "w"
              , P .bvSint
                  [ P .bvResize
                      [ P .bvUint [C "slot" [R "env", R "n", P .hd [P .tl [R "deps"]]]]
                      , C "slot" [R "env", R "n", P .hd [R "deps"]] ] ] ] },

  -- Op_SRA and Op_GetMask are binary and delegate directly; the operand order
  -- is the pinned model's (`eval_op .. w [a, b]`).
  { name := "opSra", params := ["w", "deps", "env", "n"], inline := true
  , body := P .bvSra [ R "w"
                     , C "slot" [R "env", R "n", P .hd [R "deps"]]
                     , C "slot" [R "env", R "n", P .hd [P .tl [R "deps"]]] ] },

  { name := "opGetMask", params := ["w", "deps", "env", "n"], inline := true
  , body := P .bvGetMask [ R "w"
                         , C "slot" [R "env", R "n", P .hd [R "deps"]]
                         , C "slot" [R "env", R "n", P .hd [P .tl [R "deps"]]] ] },

  { name := "mkOutputs", params := ["outs", "env", "n"], inline := true
  , body := .ite (P .isNil [R "outs"]) nil
              (.switch (P .hd [R "outs"])
                 [(tagOutput, ["s", "w"],
                   cons (P .bvResize [R "w", C "slot" [R "env", R "n", R "s"]])
                        (C "mkOutputs" [P .tl [R "outs"], R "env", R "n"]))]) },

  { name := "flopNexts", params := ["flops", "edges", "env", "n", "fq", "idx"], inline := true
  , body := .ite (P .isNil [R "flops"]) nil
              (cons (C "flopNext"
                       [P .hd [R "flops"], R "edges", R "env", R "n", R "fq", R "idx"])
                    (C "flopNexts"
                       [P .tl [R "flops"], R "edges", R "env", R "n", R "fq",
                        P .addI [R "idx", int 1]])) },

  -- Does this flop's domain fire?  The ORDINAL is static -- it comes out of the
  -- certificate -- so this is a statically known number of `tl` steps into a
  -- dynamic array, which is the shape Phase 1 made cost one direct read.  No
  -- bounds check, exactly like every other runtime read: see `RuntimeSized`.
  { name := "firesAt", params := ["edges", "c"], inline := true
  , body := C "nthD" [R "edges", R "c"] },

  -- reset before enable; the reset VALUE, not a hardcoded zero; and when
  -- disabled the OLD STATE, read raw -- `srcFlopNext` does not resize that
  -- branch, so neither does this one.
  -- CLOCKS.  A quiet domain HOLDS -- unless the reset is ASYNCHRONOUS and
  -- asserted, which acts regardless of the edge; a SYNCHRONOUS reset is sampled
  -- at the edge like `din` and so must not act in a quiet step.  Both `ck` and
  -- `ar` come out of the certificate and are static, so a one-domain design
  -- specializes back to exactly the rule that was here before.
  { name := "flopNext", params := ["f", "edges", "env", "n", "fq", "idx"], inline := true
  , body := .switch (R "f")
              [(tagFlop, ["w", "din", "en", "rp", "rv", "al", "ck", "ar"],
                lets [ ("edge", C "firesAt" [R "edges", R "ck"])
                     , ("cap",  C "capture"
                                  [R "en", R "edge", R "w", R "din", R "env", R "n",
                                   R "fq", R "idx"]) ]
                  -- EVERY test here that CAN be static IS static, so a residual
                  -- boolean is emitted only where the edge genuinely forces one.
                  -- `ar` and the PRESENCE of a reset pin both come out of the
                  -- certificate; spelling the rule as `rst && (edge || ar)`
                  -- would have residualized an `orB` always and an `andB` even
                  -- for a flop with no reset at all.
                  (.ite (P .isNil [R "rp"])
                        (R "cap")
                        (.ite (R "ar")
                              (.ite (C "rstActive" [R "rp", R "al", R "env", R "n"])
                                    (P .bvMk [R "w", R "rv"]) (R "cap"))
                              (.ite (P .andB [C "rstActive" [R "rp", R "al", R "env", R "n"],
                                              R "edge"])
                                    (P .bvMk [R "w", R "rv"]) (R "cap")))))] },

  -- capture-or-hold, with the PRESENCE of an enable static as well
  { name := "capture", params := ["en", "edge", "w", "din", "env", "n", "fq", "idx"]
  , inline := true
  , body := .ite (P .isNil [R "en"])
              (.ite (R "edge")
                    (P .bvResize [R "w", C "slot" [R "env", R "n", R "din"]])
                    (C "nthD" [R "fq", R "idx"]))
              (.ite (P .andB [R "edge", C "nz" [C "slot" [R "env", R "n", P .hd [R "en"]]]])
                    (P .bvResize [R "w", C "slot" [R "env", R "n", R "din"]])
                    (C "nthD" [R "fq", R "idx"])) },

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

/-- The division: the certificate is static; the edge vector, the runtime input
and the state are dynamic.  Everything else is inferred. -/
def hwA : Except BTAError AProgram := bta hwP hwInl [.stat, .dyn, .dyn, .dyn] 200

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

def runHw (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    EvalResult :=
  evalFuel 5000 hwP []
    (.call hwP.entry [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                      .lit (encState s)])

@[inline] def refOf (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    EvalResult :=
  .value (encResult (interpretDesign D e i s))

-- combinational
#guard runHw tinyD (allEdges tinyD) tinyIn tinySt == refOf tinyD (allEdges tinyD) tinyIn tinySt
-- sequential: enabled, disabled (old state held), and reset (which beats enable)
#guard runHw seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0) == refOf seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0)
#guard runHw seqD (allEdges seqD) (seqIn 3 0 0) (seqSt 4) == refOf seqD (allEdges seqD) (seqIn 3 0 0) (seqSt 4)
#guard runHw seqD (allEdges seqD) (seqIn 3 1 1) (seqSt 4) == refOf seqD (allEdges seqD) (seqIn 3 1 1) (seqSt 4)

/-! ### Batch 1 operators: `Op_Or`, `Op_SRA`, `Op_GetMask`

One two-input node per operator, run through `I_hw` and compared against the
SHARED reference.  Agreement is the check that matters: the two are independent
implementations of the same pinned `eval_op` case, so a mistake in the operand
order or the fold shape shows up here. -/

private def binD (o : LGraphOp) : DesignCert where
  sources  := #[.input 0 4, .input 1 4]
  nodes    := #[{ op := o, width := 4, deps := #[0, 1] }]
  outputs  := #[{ slot := 2, width := 4 }]
  flops    := #[]
  memories := #[]

private def binIn (a b : Int) : RuntimeInput := #[mk_bv 4 a, mk_bv 4 b]

private def binOK (o : LGraphOp) (a b : Int) : Bool :=
  runHw (binD o) (allEdges (binD o)) (binIn a b) tinySt
    == refOf (binD o) (allEdges (binD o)) (binIn a b) tinySt

#guard [(5,3),(12,10),(0,15),(8,1),(15,15)].all (fun p => binOK .Op_Or  p.1 p.2)
#guard [(5,3),(12,10),(0,15),(8,1),(15,15)].all (fun p => binOK .Op_SRA p.1 p.2)
#guard [(5,3),(12,10),(0,15),(8,1),(15,15)].all (fun p => binOK .Op_GetMask p.1 p.2)

-- `Op_Or` folds EVERY operand from a zero seed, so a three-input node is not
-- `Op_And`'s shape with a different gate; this is the case that would break if
-- `opOr` had been copied from `opAnd`
private def or3D : DesignCert where
  sources  := #[.input 0 4, .input 1 4, .input 2 4]
  nodes    := #[{ op := .Op_Or, width := 4, deps := #[0, 1, 2] }]
  outputs  := #[{ slot := 3, width := 4 }]
  flops    := #[]
  memories := #[]

#guard [(1,2,4),(8,4,2),(0,0,0),(15,0,1)].all (fun t =>
  runHw or3D (allEdges or3D) #[mk_bv 4 t.1, mk_bv 4 t.2.1, mk_bv 4 t.2.2] tinySt
    == refOf or3D (allEdges or3D) #[mk_bv 4 t.1, mk_bv 4 t.2.1, mk_bv 4 t.2.2] tinySt)

/-! ### Batch 2 operators: `Op_Xor`, `Op_Not`, `Op_Sum` -/

#guard [(5,3),(12,10),(0,15),(8,1),(15,15)].all (fun p => binOK .Op_Xor p.1 p.2)

-- `Op_Xor` is zero-seeded and folds EVERY operand, so a three-input node is
-- again not `Op_And`'s shape
private def xor3D : DesignCert where
  sources  := #[.input 0 4, .input 1 4, .input 2 4]
  nodes    := #[{ op := .Op_Xor, width := 4, deps := #[0, 1, 2] }]
  outputs  := #[{ slot := 3, width := 4 }]
  flops    := #[]
  memories := #[]

#guard [(1,2,4),(8,4,2),(0,0,0),(15,15,15)].all (fun t =>
  runHw xor3D (allEdges xor3D) #[mk_bv 4 t.1, mk_bv 4 t.2.1, mk_bv 4 t.2.2] tinySt
    == refOf xor3D (allEdges xor3D) #[mk_bv 4 t.1, mk_bv 4 t.2.1, mk_bv 4 t.2.2] tinySt)

-- `Op_Not` is strictly unary
private def notD : DesignCert where
  sources  := #[.input 0 4]
  nodes    := #[{ op := .Op_Not, width := 4, deps := #[0] }]
  outputs  := #[{ slot := 1, width := 4 }]
  flops    := #[]
  memories := #[]

#guard [0,1,5,8,15].all (fun a =>
  runHw notD (allEdges notD) #[mk_bv 4 a] tinySt
    == refOf notD (allEdges notD) #[mk_bv 4 a] tinySt)

-- `Op_Sum`'s PAYLOAD is the partition point: `n_add` operands are added and the
-- REST subtracted.  All three settings over the same two operands agree with
-- the reference…
#guard [(3,5),(15,15),(0,1),(9,9)].all (fun p => binOK (.Op_Sum 2) p.1 p.2)
#guard [(3,5),(15,15),(0,1),(9,9)].all (fun p => binOK (.Op_Sum 1) p.1 p.2)
#guard [(3,5),(15,15),(0,1),(9,9)].all (fun p => binOK (.Op_Sum 0) p.1 p.2)

-- …and these are the vectors that say the partition is real rather than
-- symmetric.  On (3, 5): both added is 8; the second subtracted is 3-5 = -2;
-- both subtracted is -8.  A payload that failed to survive encoding would
-- collapse these three to one answer.
#guard (interpretDesign (binD (.Op_Sum 2)) (allEdges (binD (.Op_Sum 2)))
          (binIn 3 5) tinySt).outputs == #[mk_bv 4 8]
#guard (interpretDesign (binD (.Op_Sum 1)) (allEdges (binD (.Op_Sum 1)))
          (binIn 3 5) tinySt).outputs == #[mk_bv 4 14]
#guard (interpretDesign (binD (.Op_Sum 0)) (allEdges (binD (.Op_Sum 0)))
          (binIn 3 5) tinySt).outputs == #[mk_bv 4 8]

-- WIDTH TRUNCATION IS OF THE SUM, applied once at the end by `mk_bv`: 15 + 15
-- is 30, which is 14 at width 4 -- not a saturation and not a per-term wrap.
#guard (interpretDesign (binD (.Op_Sum 2)) (allEdges (binD (.Op_Sum 2)))
          (binIn 15 15) tinySt).outputs == #[mk_bv 4 14]
#guard binOK (.Op_Sum 2) 15 15

/-! ### Batch 3 operators: `Op_EQ`, `Op_Ror`, `Op_MuxBool`, `Op_MuxN`

The first operators whose answer depends on operand VALUES, so these vectors
pin behaviour a residual comparison could get backwards. -/

#guard [(5,5),(5,3),(0,0),(15,15),(0,15)].all (fun p => binOK .Op_EQ p.1 p.2)

-- EQ compares every operand against the FIRST, so a three-input node is 1 only
-- when both followers match the head
private def eq3D : DesignCert where
  sources  := #[.input 0 4, .input 1 4, .input 2 4]
  nodes    := #[{ op := .Op_EQ, width := 4, deps := #[0, 1, 2] }]
  outputs  := #[{ slot := 3, width := 4 }]
  flops    := #[]
  memories := #[]

private def eq3 (a b c : Int) : Array BV := #[mk_bv 4 a, mk_bv 4 b, mk_bv 4 c]

#guard [(7,7,7),(7,7,1),(7,1,7),(1,7,7),(0,0,0)].all (fun t =>
  runHw eq3D (allEdges eq3D) (eq3 t.1 t.2.1 t.2.2) tinySt
    == refOf eq3D (allEdges eq3D) (eq3 t.1 t.2.1 t.2.2) tinySt)
#guard (interpretDesign eq3D (allEdges eq3D) (eq3 7 7 7) tinySt).outputs == #[mk_bv 4 1]
#guard (interpretDesign eq3D (allEdges eq3D) (eq3 7 7 1) tinySt).outputs == #[mk_bv 4 0]
#guard (interpretDesign eq3D (allEdges eq3D) (eq3 7 1 7) tinySt).outputs == #[mk_bv 4 0]

-- Op_Ror is a REDUCTION: any operand nonzero gives 1, at width 1.  A bitwise
-- `Op_Or` would give 5 on (1, 4); this gives 1.
private def rorD : DesignCert where
  sources  := #[.input 0 4, .input 1 4]
  nodes    := #[{ op := .Op_Ror, width := 1, deps := #[0, 1] }]
  outputs  := #[{ slot := 2, width := 1 }]
  flops    := #[]
  memories := #[]

#guard [(0,0),(1,0),(0,1),(1,4),(15,15)].all (fun q =>
  runHw rorD (allEdges rorD) #[mk_bv 4 q.1, mk_bv 4 q.2] tinySt
    == refOf rorD (allEdges rorD) #[mk_bv 4 q.1, mk_bv 4 q.2] tinySt)
#guard (interpretDesign rorD (allEdges rorD) #[mk_bv 4 0, mk_bv 4 0] tinySt).outputs
         == #[mk_bv 1 0]
#guard (interpretDesign rorD (allEdges rorD) #[mk_bv 4 1, mk_bv 4 4] tinySt).outputs
         == #[mk_bv 1 1]

-- Op_MuxBool: [sel, false_v, true_v].  BOTH polarities pinned as values, since
-- a swap is silent.
private def muxBD : DesignCert where
  sources  := #[.input 0 1, .input 1 4, .input 2 4]
  nodes    := #[{ op := .Op_MuxBool, width := 4, deps := #[0, 1, 2] }]
  outputs  := #[{ slot := 3, width := 4 }]
  flops    := #[]
  memories := #[]

private def muxBIn (sel fv tv : Int) : Array BV := #[mk_bv 1 sel, mk_bv 4 fv, mk_bv 4 tv]

#guard [(0,5,9),(1,5,9),(0,0,15),(1,15,0)].all (fun t =>
  runHw muxBD (allEdges muxBD) (muxBIn t.1 t.2.1 t.2.2) tinySt
    == refOf muxBD (allEdges muxBD) (muxBIn t.1 t.2.1 t.2.2) tinySt)
#guard (interpretDesign muxBD (allEdges muxBD) (muxBIn 0 5 9) tinySt).outputs == #[mk_bv 4 5]
#guard (interpretDesign muxBD (allEdges muxBD) (muxBIn 1 5 9) tinySt).outputs == #[mk_bv 4 9]

-- Op_MuxN: selector FIRST, data indexed from 0, out of range answers ZERO.
private def muxND : DesignCert where
  sources  := #[.input 0 4, .input 1 4, .input 2 4, .input 3 4]
  nodes    := #[{ op := .Op_MuxN, width := 4, deps := #[0, 1, 2, 3] }]
  outputs  := #[{ slot := 4, width := 4 }]
  flops    := #[]
  memories := #[]

private def muxNIn (s a b c : Int) : Array BV :=
  #[mk_bv 4 s, mk_bv 4 a, mk_bv 4 b, mk_bv 4 c]

#guard [(0,7,8,9),(1,7,8,9),(2,7,8,9),(3,7,8,9),(15,7,8,9)].all (fun t =>
  runHw muxND (allEdges muxND) (muxNIn t.1 t.2.1 t.2.2.1 t.2.2.2) tinySt
    == refOf muxND (allEdges muxND) (muxNIn t.1 t.2.1 t.2.2.1 t.2.2.2) tinySt)
-- selector 0 picks the FIRST data operand, 2 the LAST…
#guard (interpretDesign muxND (allEdges muxND) (muxNIn 0 7 8 9) tinySt).outputs == #[mk_bv 4 7]
#guard (interpretDesign muxND (allEdges muxND) (muxNIn 2 7 8 9) tinySt).outputs == #[mk_bv 4 9]
-- …and OUT OF RANGE is zero, not a wrap and not the last
#guard (interpretDesign muxND (allEdges muxND) (muxNIn 3 7 8 9) tinySt).outputs == #[mk_bv 4 0]
#guard (interpretDesign muxND (allEdges muxND) (muxNIn 15 7 8 9) tinySt).outputs == #[mk_bv 4 0]

/-! ### Batch 4 operators: the comparisons and `Op_Sext` -/

private def cmpOps : List LGraphOp := [.Op_ULT, .Op_UGT, .Op_SLT, .Op_SGT]

#guard cmpOps.all (fun o =>
  [(8,1),(1,8),(8,8),(15,0),(0,15),(7,8),(8,7)].all (fun q => binOK o q.1 q.2))

private def cmpOut (o : LGraphOp) (a b : Int) : Array BV :=
  (interpretDesign (binD o) (allEdges (binD o)) (binIn a b) tinySt).outputs

-- SIGNED AND UNSIGNED DIVERGE, and this is the table that pins it.  At width
-- 4, `8` is `0b1000`: 8 unsigned, -8 signed.  Each row is the SAME bits read
-- two ways, and both operand orders are covered so a swapped comparison cannot
-- hide.
#guard cmpOut .Op_ULT 8 1 == #[mk_bv 4 0]   -- 8 < 1 unsigned: no
#guard cmpOut .Op_SLT 8 1 == #[mk_bv 4 1]   -- -8 < 1 signed:  yes
#guard cmpOut .Op_ULT 1 8 == #[mk_bv 4 1]   -- 1 < 8 unsigned: yes
#guard cmpOut .Op_SLT 1 8 == #[mk_bv 4 0]   -- 1 < -8 signed:  no
#guard cmpOut .Op_UGT 8 1 == #[mk_bv 4 1]   -- 8 > 1 unsigned: yes
#guard cmpOut .Op_SGT 8 1 == #[mk_bv 4 0]   -- -8 > 1 signed:  no
#guard cmpOut .Op_UGT 1 8 == #[mk_bv 4 0]   -- 1 > 8 unsigned: no
#guard cmpOut .Op_SGT 1 8 == #[mk_bv 4 1]   -- 1 > -8 signed:  yes

-- equality is not strictly less-than, in either reading
#guard cmpOut .Op_ULT 8 8 == #[mk_bv 4 0]
#guard cmpOut .Op_SLT 8 8 == #[mk_bv 4 0]
#guard cmpOut .Op_UGT 8 8 == #[mk_bv 4 0]
#guard cmpOut .Op_SGT 8 8 == #[mk_bv 4 0]

/-- `Op_Sext` takes `[a, amount]`: sign-extend the low `n = amount` bits of
`a`, then wrap to the NODE width. -/
private def sextD (w : Nat) : DesignCert where
  sources  := #[.input 0 4, .input 1 4]
  nodes    := #[{ op := .Op_Sext, width := w, deps := #[0, 1] }]
  outputs  := #[{ slot := 2, width := w }]
  flops    := #[]
  memories := #[]

private def sextOut (w : Nat) (a amt : Int) : Array BV :=
  (interpretDesign (sextD w) (allEdges (sextD w)) (binIn a amt) tinySt).outputs

#guard [(3,4),(8,4),(5,0),(5,2),(6,2),(0,4),(15,4),(15,1)].all (fun q =>
  runHw (sextD 4) (allEdges (sextD 4)) (binIn q.1 q.2) tinySt
    == refOf (sextD 4) (allEdges (sextD 4)) (binIn q.1 q.2) tinySt)

#guard sextOut 4 3 4 == #[mk_bv 4 3]    -- POSITIVE: low 4 bits of 3 is 3, sign bit clear
#guard sextOut 4 8 4 == #[mk_bv 4 8]    -- NEGATIVE: 0b1000 is -8, which is 8 at width 4
#guard sextOut 4 5 0 == #[mk_bv 4 0]    -- n = 0 is ZERO, whatever `a` holds
#guard sextOut 4 8 0 == #[mk_bv 4 0]
-- n SMALLER than the operand's width: only the low n bits are read, and their
-- own top bit is the sign.  0b0101 at n=2 is 0b01 = 1; 0b0110 at n=2 is
-- 0b10 = -2, which is 14 at width 4.
#guard sextOut 4 5 2 == #[mk_bv 4 1]
#guard sextOut 4 6 2 == #[mk_bv 4 14]
-- OUTPUT TRUNCATION, applied after the extension: 5 sign-extended at n=4 is
-- still 5, and 5 at the node's width 2 is 1.
#guard sextOut 2 5 4 == #[mk_bv 2 1]
#guard runHw (sextD 2) (allEdges (sextD 2)) (binIn 5 4) tinySt
         == refOf (sextD 2) (allEdges (sextD 2)) (binIn 5 4) tinySt

/-! ### `Op_SHL`: the last operator

`a` is shifted INDEPENDENTLY by each remaining operand and the copies are
XOR-folded into a zero seed.  The vectors below separate that from the two
things it is not: an accumulator shift, and an OR-fold. -/

#guard [(1,0),(1,2),(1,3),(1,4),(3,3),(8,1),(15,0),(5,1)].all
         (fun q => binOK .Op_SHL q.1 q.2)

private def shlOut (a b : Int) : Array BV :=
  (interpretDesign (binD .Op_SHL) (allEdges (binD .Op_SHL)) (binIn a b) tinySt).outputs

-- ordinary shifts
#guard shlOut 1 0 == #[mk_bv 4 1]
#guard shlOut 1 2 == #[mk_bv 4 4]
#guard shlOut 3 1 == #[mk_bv 4 6]
-- WIDTH TRUNCATION: 1 << 4 is 16, which is 0 at width 4; 3 << 3 is 24, which
-- is 8; the high bit of 0b1000 shifts straight out
#guard shlOut 1 4 == #[mk_bv 4 0]
#guard shlOut 3 3 == #[mk_bv 4 8]
#guard shlOut 8 1 == #[mk_bv 4 0]

/-- Two shift amounts, so the FOLD is visible. -/
private def shl3D : DesignCert where
  sources  := #[.input 0 4, .input 1 4, .input 2 4]
  nodes    := #[{ op := .Op_SHL, width := 4, deps := #[0, 1, 2] }]
  outputs  := #[{ slot := 3, width := 4 }]
  flops    := #[]
  memories := #[]

private def shl3In (a b c : Int) : Array BV := #[mk_bv 4 a, mk_bv 4 b, mk_bv 4 c]
private def shl3Out (a b c : Int) : Array BV :=
  (interpretDesign shl3D (allEdges shl3D) (shl3In a b c) tinySt).outputs

#guard [(1,0,1),(1,1,1),(3,0,2),(1,2,3),(5,0,0)].all (fun t =>
  runHw shl3D (allEdges shl3D) (shl3In t.1 t.2.1 t.2.2) tinySt
    == refOf shl3D (allEdges shl3D) (shl3In t.1 t.2.1 t.2.2) tinySt)

-- `a` is shifted by EACH amount independently: 1<<0 xor 1<<1 = 1 xor 2 = 3.
-- An accumulator shift would give (1<<0)<<1 = 2.
#guard shl3Out 1 0 1 == #[mk_bv 4 3]
#guard shl3Out 3 0 2 == #[mk_bv 4 15]
-- THE DECIDING VECTOR: the fold is XOR, not OR.  Two EQUAL shift amounts
-- cancel -- 1<<1 xor 1<<1 = 0 -- where an OR-fold would give 2.
#guard shl3Out 1 1 1 == #[mk_bv 4 0]

/-- No shift amounts at all, so the fold never runs and the seed stands: a
one-operand node is ZERO, not `a`. -/
private def shl1D : DesignCert where
  sources  := #[.input 0 4]
  nodes    := #[{ op := .Op_SHL, width := 4, deps := #[0] }]
  outputs  := #[{ slot := 1, width := 4 }]
  flops    := #[]
  memories := #[]

#guard [0,1,5,15].all (fun a =>
  runHw shl1D (allEdges shl1D) #[mk_bv 4 a] tinySt
    == refOf shl1D (allEdges shl1D) #[mk_bv 4 a] tinySt)
#guard (interpretDesign shl1D (allEdges shl1D) #[mk_bv 4 5] tinySt).outputs == #[mk_bv 4 0]

/-- The EMPTY case, which no well-formed certificate emits but the pinned
semantics still fixes at zero. -/
private def shl0D : DesignCert where
  sources  := #[.input 0 4]
  nodes    := #[{ op := .Op_SHL, width := 4, deps := #[] }]
  outputs  := #[{ slot := 1, width := 4 }]
  flops    := #[]
  memories := #[]

#guard runHw shl0D (allEdges shl0D) #[mk_bv 4 5] tinySt
         == refOf shl0D (allEdges shl0D) #[mk_bv 4 5] tinySt
#guard (interpretDesign shl0D (allEdges shl0D) #[mk_bv 4 5] tinySt).outputs == #[mk_bv 4 0]

-- SRA IS ARITHMETIC, and this is the vector that says so: 0b1000 is -8 at
-- width 4, so shifting right by one gives -4 = 0b1100, not the 0b0100 a
-- LOGICAL shift would give.
#guard (interpretDesign (binD .Op_SRA) (allEdges (binD .Op_SRA))
          (binIn 8 1) tinySt).outputs == #[mk_bv 4 12]
#guard (interpretDesign (binD .Op_SRA) (allEdges (binD .Op_SRA))
          (binIn 8 1) tinySt).outputs != #[mk_bv 4 4]

end Acceptance

/-! ## The first projection, on hardware

Milestone 3's definition, here because it is one line and because the checks
below are what justify the milestone's proof being worth writing.  The theorem
(`projectDesign_correct`) is not yet written; these are `#guard`s, and the
distinction is the one `SHARED_SEMANTICS.md` and Gate 0 already draw. -/

/-! ## Milestone 2, item 1: what `I_hw` actually supports

Two predicates, deliberately separate.  `SupportedByProjection` is about the
CERTIFICATE alone and is decided once; `RuntimeSized` is about the three vectors
a particular step is handed.  Mixing them would make a design-level fact look
like a per-cycle obligation, and vice versa.

NEITHER IS "PROJECTABLE", and NEITHER IS WHAT THE CENSUS MEASURES.

The CORE-ET census discharges exactly ONE field, `ops`, and only over the READY
population: it says every node operator appearing in those certificates has an
`applyOp` case.  It says nothing about `wf`, `memFree`, `sources` or
`flopClocks` -- it never looks at dependency ordering, slot ranges, memories,
source forms or clock ordinals.  So "65/65" is not evidence that 65 designs are
supported; it is evidence about one conjunct.

And `SupportedByProjection D` does not say `projectDesign D` succeeds either:
that additionally needs the specializer's fuel to suffice, which is a property
of the RUN and not of the design, and which the theorems below therefore take
as a separate hypothesis rather than fold in. -/

/-- The source forms `srcVal` evaluates.

`memImg` and `memConst` are NOT among them, and this is the one place that
matters: `srcVal` answers both with `mk_bv 0 0`, which is defined and WRONG
rather than a refusal.  `memConst` in particular is a ROM -- a combinational
table with no entry in `RuntimeState.mems` at all -- so a design can have
`D.memories = #[]` and still contain one.  Memory-freeness does not exclude it;
only this does. -/
def SourceSupported : Compiler.SourceDesc → Bool
  | .input _ _            => true
  | .const _ _            => true
  | .flopQ _ _            => true
  | .flopQAsync _ _ _ _ _ => true
  | .memImg _ _ _         => false
  | .memConst _ _ _       => false

/-- The node operators `applyOp` dispatches on: the seventeen the CORE-ET
census finds, and no others.  `Op_Const` is absent because a constant is a
SOURCE, never a node op -- which is why the census finds it in zero node
positions.

THIS EXCLUSION IS SEMANTICALLY LOAD-BEARING, not refusal hygiene.  An earlier
version of this comment claimed the unsupported fallback agrees with
`eval_op`'s own catch-all.  That is FALSE for every excluded operator that
`eval_op` actually implements.  `applyOp` answers `mk_bv w 0`, while at width 4

    eval_op .Op_UDiv 4 [6, 2] = 3
    eval_op .Op_Sub  4 [6, 2] = 4
    eval_op .Op_Mult 4 [3, 2] = 6

so an unsupported node does not merely go unrefused -- `I_hw` returns a
DIFFERENT ANSWER from the reference.  Only operators `eval_op` itself leaves to
its catch-all coincide.  Hence this field is a genuine hypothesis of adequacy,
without which the theorem is false rather than merely unproven. -/
def OpSupported : LGraphOp → Bool
  | .Op_And     | .Op_Or   | .Op_Xor  | .Op_Not  | .Op_Sum _ => true
  | .Op_EQ      | .Op_Ror  | .Op_MuxBool | .Op_MuxN         => true
  | .Op_SHL     | .Op_SRA  | .Op_Sext | .Op_GetMask         => true
  | .Op_ULT     | .Op_UGT  | .Op_SLT   | .Op_SGT            => true
  | _                                                       => false

/-! ### Operand counts

`OpSupported` is about WHICH operator; this is about HOW MANY operands it is
given, and it is a second genuine hypothesis for the same reason.

Nine of the seventeen are FIXED-ARITY: the pinned `eval_op` matches on an exact
operand shape and falls through to its catch-all `mk_bv w 0` at every other
length, while `applyOp` reads positionally -- it ERRORS below the arity (`hd
nil` has no value at all) and IGNORES surplus operands above it.  So the two
disagree off the pinned shape, and the disagreement is not conservative:
`Op_Not` with two deps gives `mk_bv w 0` on the shared side and `bv_not w a` on
the object side (`SupportCheck.not_arity_bites` below exhibits the numbers).

Nothing else in the branch rules this out -- `DenseNodeCert.deps` is an
unconstrained `Array Nat`, and `DesignCertWF` constrains only `DepsBounded` and
`SlotsInRange` -- so it is a field of `SupportedByProjection`.

`RequiredArity` is the SINGLE SOURCE OF TRUTH for the mapping.  `ArityOK` reads
it, the census script parses it out of this file (as it already does the
operator codes), and no second table exists to drift. -/
def RequiredArity : LGraphOp → Option Nat
  | .Op_Not     => some 1
  | .Op_SRA     => some 2
  | .Op_GetMask => some 2
  | .Op_MuxBool => some 3
  | .Op_Sext    => some 2
  | .Op_ULT     => some 2
  | .Op_UGT     => some 2
  | .Op_SLT     => some 2
  | .Op_SGT     => some 2
  -- Op_And, Op_Or, Op_Xor, Op_Ror, Op_EQ, Op_SHL, Op_Sum, Op_MuxN fold over
  -- however many operands they are given, on BOTH sides, so they impose
  -- nothing.  Everything `OpSupported` rejects is irrelevant here.
  | _           => none

/-- The dep list has the length its operator requires, if its operator requires
one at all. -/
def ArityOK (op : LGraphOp) (ds : List Nat) : Prop :=
  match RequiredArity op with
  | none   => True
  | some k => ds.length = k

instance : ∀ (op : LGraphOp) (ds : List Nat), Decidable (ArityOK op ds) := by
  intro op ds
  unfold ArityOK
  split <;> infer_instance

/-- Everything about the CERTIFICATE that `I_hw` relies on.

`wf` is the SHARED well-formedness predicate, not a local restatement:
`DesignCertWF` already says dependencies name strictly earlier slots and that
outputs, flop pins and memory images name real slots, which is precisely the
"valid references" half.  Widths need no condition -- `bv_resize` and `mk_bv`
are total at every width -- so none is invented here. -/
structure SupportedByProjection (D : Compiler.DesignCert) : Prop where
  /-- dependency ordering and slot ranges, from the shared checker -/
  wf         : Compiler.DesignCert.DesignCertWF D
  /-- `StateRel`/`ResultRel` admit no memory-bearing state yet (Phase 7) -/
  memFree    : D.memories = #[]
  /-- no `memImg`/`memConst` source; see `SourceSupported` -/
  sources    : ∀ sd ∈ D.sources.toList, SourceSupported sd = true
  /-- every node operator has an `applyOp` case -/
  ops        : ∀ c ∈ D.nodes.toList, OpSupported c.op = true
  /-- …and is given the operand count the pinned model matches on.  A STATIC
  certificate-shape requirement, like `ops`, not a per-cycle assumption. -/
  arities    : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList
  /-- every flop commits on a DECLARED clock, so `fires` is not reading past
  the edge vector.  The memory analogue is vacuous under `memFree`. -/
  flopClocks : ∀ f ∈ D.flops, f.clock < D.clocks.size

/-- How far into the input vector a source reads.  `flopQAsync` reads TWO
vectors: its stored value from the state and its reset from the INPUTS. -/
def SourceInputBound : Compiler.SourceDesc → Nat → Prop
  | .input idx _,            n => idx < n
  | .flopQAsync _ _ ri _ _,  n => ri < n
  | _,                       _ => True

instance : ∀ sd n, Decidable (SourceInputBound sd n)
  | .input _ _, _            => inferInstanceAs (Decidable (_ < _))
  | .const _ _, _            => inferInstanceAs (Decidable True)
  | .flopQ _ _, _            => inferInstanceAs (Decidable True)
  | .flopQAsync _ _ _ _ _, _ => inferInstanceAs (Decidable (_ < _))
  | .memImg _ _ _, _         => inferInstanceAs (Decidable True)
  | .memConst _ _ _, _       => inferInstanceAs (Decidable True)

/-- …and how far into the flop-state vector. -/
def SourceFlopBound : Compiler.SourceDesc → Nat → Prop
  | .flopQ idx _,            n => idx < n
  | .flopQAsync idx _ _ _ _, n => idx < n
  | _,                       _ => True

instance : ∀ sd n, Decidable (SourceFlopBound sd n)
  | .input _ _, _            => inferInstanceAs (Decidable True)
  | .const _ _, _            => inferInstanceAs (Decidable True)
  | .flopQ _ _, _            => inferInstanceAs (Decidable (_ < _))
  | .flopQAsync _ _ _ _ _, _ => inferInstanceAs (Decidable (_ < _))
  | .memImg _ _ _, _         => inferInstanceAs (Decidable True)
  | .memConst _ _ _, _       => inferInstanceAs (Decidable True)

/-- What `I_hw`'s unchecked reads assume about the three vectors of ONE step.

`nthD` has no bounds check: an out-of-range read is `hd nil`, a type error, not
a default.  `interpretDesign` is total exactly where `I_hw` is not -- `fires`
reads an undeclared ordinal as `false`, and `Array.getElem?` defaults -- so the
two agree only where these hold.  Stated as a hypothesis, never discovered
inside a proof.

`edges` is `RuntimeSemWF.edgesSized` in d2's checker; the checker is not ported
here, so it is carried. -/
structure RuntimeSized (D : Compiler.DesignCert) (e : Compiler.ClockEdges)
    (i : Compiler.RuntimeInput) (s : Compiler.RuntimeState) : Prop where
  edges      : D.clocks.size ≤ e.size
  inputs     : ∀ sd ∈ D.sources.toList, SourceInputBound sd i.size
  stateReads : ∀ sd ∈ D.sources.toList, SourceFlopBound sd s.flops.size
  /-- `flopNext` reads the old value of flop `idx` for every flop in the design -/
  flopsSized : D.flops.size ≤ s.flops.size

/-- The all-fire stimulus satisfies the edge half for any supported design. -/
theorem RuntimeSized_allEdges_edges {D : Compiler.DesignCert} :
    D.clocks.size ≤ (Compiler.allEdges D).size := by
  simp [Compiler.allEdges_size]

/-! ### The shared state-shape contract, and the operational inequality

`Compiler.RuntimeWF` is the SHARED contract and says the state array has
EXACTLY the design's shape.  `RuntimeSized.flopsSized` is weaker on purpose --
an inequality is all the slot reads need -- but the weaker form is an INTERNAL
operational convenience and must not quietly become the public contract.  The
bridge below is how the public theorem's `RuntimeWF` reaches it. -/

theorem RuntimeSized.flopsSized_of_runtimeWF {D : Compiler.DesignCert}
    {i : Compiler.RuntimeInput} {s : Compiler.RuntimeState}
    (h : Compiler.RuntimeWF D i s) : D.flops.size ≤ s.flops.size :=
  Nat.le_of_eq h.flopsSized.symm

theorem RuntimeSized.of_runtimeWF {D : Compiler.DesignCert} {e : Compiler.ClockEdges}
    {i : Compiler.RuntimeInput} {s : Compiler.RuntimeState}
    (hwf : Compiler.RuntimeWF D i s)
    (hedges : D.clocks.size ≤ e.size)
    (hin : ∀ sd ∈ D.sources.toList, SourceInputBound sd i.size)
    (hst : ∀ sd ∈ D.sources.toList, SourceFlopBound sd s.flops.size) :
    RuntimeSized D e i s :=
  ⟨hedges, hin, hst, RuntimeSized.flopsSized_of_runtimeWF hwf⟩

/-! ### Milestone 2 item 2: THE TARGET, stated exactly

A DEFINITION, not a theorem.  Nothing below assumes it; it is written out so
the goal the helper lemmas are aiming at is visible, typechecked, and cannot
drift while they are built.  The public form takes the SHARED `RuntimeWF`, not
the internal inequality.

What it says: running the object interpreter on the ENCODED design, edge
vector, input and state yields exactly the encodings of what the shared
reference semantics computes -- `iff`, so neither direction is assumed, and
over `Eval` rather than `evalFuel`, so no fuel appears in the statement. -/
def IHwAdequate (D : Compiler.DesignCert) (e : Compiler.ClockEdges)
    (i : Compiler.RuntimeInput) (s : Compiler.RuntimeState) : Prop :=
  ∀ r : Val,
    Eval hwP []
      (.call hwP.entry
        [.lit (encDesign D), .lit (encEdges e), .lit (encInput i), .lit (encState s)]) r
    ↔ ResultRel r (Compiler.interpretDesign D e i s)

/-- The shape the proved theorem will take, once the helpers exist. -/
def IHwAdequacyGoal : Prop :=
  ∀ (D : Compiler.DesignCert) (e : Compiler.ClockEdges)
    (i : Compiler.RuntimeInput) (s : Compiler.RuntimeState),
    SupportedByProjection D → Compiler.RuntimeWF D i s → RuntimeSized D e i s →
    IHwAdequate D e i s

/-- A supported design's clock ordinals are in range, so `interpretDesign`'s
conservativity hypotheses are discharged by support alone. -/
theorem SupportedByProjection.conservative {D : Compiler.DesignCert}
    (h : SupportedByProjection D) (i : Compiler.RuntimeInput) (s : Compiler.RuntimeState) :
    Compiler.interpretDesign D (Compiler.allEdges D) i s
      = Compiler.interpretDesignLegacy D i s :=
  Compiler.interpretDesign_allEdges D h.flopClocks
    (by intro m hm; rw [h.memFree] at hm; simp at hm) i s

/-! ### The predicate is satisfiable, and it bites

Both shared fixtures satisfy it, and a design differing only by an unsupported
feature does not.  Without the negative controls a predicate like this can be
accidentally vacuous or accidentally universal and nobody notices. -/

namespace SupportCheck
open Compiler Projection.Acceptance

theorem tiny_supported : SupportedByProjection tinyD where
  wf := ⟨DesignCert.depsBounded_of_bool tinyD (by decide),
         by refine ⟨?_, ?_, ?_⟩ <;>
            simp [tinyD, DesignCert.numSlots]⟩
  memFree := rfl
  sources := by decide
  ops := by decide
  arities := by decide
  flopClocks := by decide

theorem seq_supported : SupportedByProjection seqD where
  wf := ⟨DesignCert.depsBounded_of_bool seqD (by decide),
         by refine ⟨?_, ?_, ?_⟩ <;>
            simp [seqD, DesignCert.numSlots]⟩
  memFree := rfl
  sources := by decide
  ops := by decide
  arities := by decide
  flopClocks := by decide

-- NEGATIVE CONTROL 1: a ROM source.  `D.memories` is still empty, so
-- memory-freeness does NOT catch it -- `SourceSupported` is what does.
private def romD : DesignCert :=
  { tinyD with sources := tinyD.sources.push (.memConst 2 4 #[0, 1, 2, 3]) }

#guard romD.memories == #[]
example : ¬ (∀ sd ∈ romD.sources.toList, SourceSupported sd = true) := by decide

-- NEGATIVE CONTROL 2: an operator with no `applyOp` case
private def divD : DesignCert :=
  { tinyD with nodes := #[{ op := .Op_UDiv, width := 4, deps := #[0, 1] }] }

example : ¬ (∀ c ∈ divD.nodes.toList, OpSupported c.op = true) := by decide

-- NEGATIVE CONTROL 3: an operator that IS supported, given the WRONG number of
-- operands.  `ops` does not catch it -- `arities` is what does.
private def notD : DesignCert :=
  { tinyD with nodes := #[{ op := .Op_Not, width := 4, deps := #[0, 1] }] }

example : ∀ c ∈ (notD : DesignCert).nodes.toList, OpSupported c.op = true := by decide
example : ¬ (∀ c ∈ (notD : DesignCert).nodes.toList,
    ArityOK c.op c.deps.toList) := by decide

/-- …and the two semantics really disagree on it, so `arities` carries weight
rather than being conservative hygiene.  The pinned model falls to its
catch-all and answers 0; `applyOp` reads the first operand and answers
`bv_not`, which at width 4 on zero is 15. -/
theorem not_arity_bites :
    eval_op .Op_Not 4 [mk_bv 4 0, mk_bv 4 0] ≠ bv_not 4 (mk_bv 4 0) := by decide

-- …and the supported operators really are the seventeen, no more
example : OpSupported .Op_GetMask = true := by decide
example : OpSupported (.Op_Const 0) = false := by decide
example : OpSupported .Op_Sub = false := by decide
example : OpSupported .Op_MemRead = false := by decide

end SupportCheck

def projectDesign (D : Compiler.DesignCert) : Except MixError Program :=
  mixDriver 20000 200 hwAP [encDesign D]

namespace Acceptance
open Compiler Projection.Acceptance

def tinyR : Program := match projectDesign tinyD with | .ok p => p | .error _ => ⟨[], 0⟩
def seqR  : Program := match projectDesign seqD  with | .ok p => p | .error _ => ⟨[], 0⟩

#guard (projectDesign tinyD).toOption.isSome
#guard (projectDesign seqD).toOption.isSome

/-- The residual takes the DYNAMIC arguments only: the design is gone. -/
def runR (R : Program) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) : EvalResult :=
  evalFuel 5000 R [] (.call R.entry [.lit (encEdges e), .lit (encInput i), .lit (encState s)])

#guard runR tinyR (allEdges tinyD) tinyIn tinySt == refOf tinyD (allEdges tinyD) tinyIn tinySt
#guard runR seqR (allEdges seqD) (seqIn 5 1 0) (seqSt 0) == refOf seqD (allEdges seqD) (seqIn 5 1 0) (seqSt 0)
#guard runR seqR (allEdges seqD) (seqIn 3 0 0) (seqSt 4) == refOf seqD (allEdges seqD) (seqIn 3 0 0) (seqSt 4)
#guard runR seqR (allEdges seqD) (seqIn 3 1 1) (seqSt 4) == refOf seqD (allEdges seqD) (seqIn 3 1 1) (seqSt 4)

-- the design is gone in the literal sense: the residual entry takes the three
-- DYNAMIC arguments (edges, input, state), not four
#guard tinyR.funs.map FunDef.arity == [3]
#guard seqR.funs.map FunDef.arity  == [3]

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

/-! ### Batch 1: the residual shape

Each new operator projects, and the OPCODE DISPATCH is gone: `applyOp`'s chain
of `ite`s is decided during specialization, so no `tagOp` and no residual `eqI`
against an operator code survives.  One `ite` per supported operator therefore
costs the residual nothing, which is what makes the chain the right structure
to keep growing. -/

private def binR (o : LGraphOp) : Program :=
  match projectDesign (binD o) with | .ok p => p | .error _ => ⟨[], 0⟩

-- operators whose answer needs NO runtime comparison, so the sharp "no `eqI`
-- survives" assertion still applies to them
private def b12Ops : List LGraphOp :=
  [.Op_Or, .Op_SRA, .Op_GetMask, .Op_Xor, .Op_Not, .Op_Sum 2, .Op_Sum 1, .Op_Sum 0,
   .Op_SHL]

#guard b12Ops.all (fun o => (projectDesign (binD o)).toOption.isSome)

-- no design tag survives, for any of them
#guard b12Ops.all (fun o =>
  [tagDesign, tagSrcInput, tagSrcConst, tagNode, tagOp, tagOutput].all
    (fun t => t ∉ tagsIn (binR o)))

-- (the primitive-level checks need `primsOf`, and follow its definition below)

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

-- `andB` is new and unavoidable: the EDGE is dynamic, so `edge && enabled`
-- cannot be decided during specialization.  One per flop, so linear.  `orB`
-- would have been avoidable and is not here -- see `flopNext`.
#guard ((seqR.funs.map (fun fd => primsOf fd.body)).flatten).eraseDups.all
         (fun p => p ∈ [Prim.consP, .bvResize, .hd, .tl, .bvAnd, .andB, .notB, .eqI, .bvUint])

-- Batch 1, at the primitive level: each residual contains exactly the hardware
-- primitive its operator names, and NO `eqI` -- the opcode comparisons in
-- `applyOp` are all decided during specialization.
#guard (((binR .Op_SRA).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.bvSra
#guard (((binR .Op_GetMask).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.bvGetMask
#guard (((binR .Op_Or).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.bvOr
#guard b12Ops.all (fun o =>
  !(((binR o).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.eqI)

/-! ### Batch 3: the residual criterion, sharpened

The B1/B2 assertion was "no `eqI` survives".  That is still right for those
operators -- none of them needs a runtime comparison -- but it is the WRONG
test from here on: `Op_EQ` and `Op_MuxN` compare operand values, and `Op_Ror`
and `Op_MuxBool` test them, so a residual comparison is the hardware's, not
leftover dispatch.

What must disappear is the CERTIFICATE's structure.  The check below is
therefore that the only `caseT` tag surviving anywhere is `tagState` -- the
runtime state record.  Every certificate and operator tag, `tagOp` included, is
gone, which is exactly what says `applyOp`'s opcode chain was decided during
specialization.  What remains is data-dependent and is counted operator by
operator. -/

private def projOf (D : DesignCert) : Program :=
  match projectDesign D with | .ok p => p | .error _ => ⟨[], 0⟩

private def cnt (D : DesignCert) (q : Prim) : Nat :=
  (((projOf D).funs.map (fun fd => primsOf fd.body)).flatten).countP (· == q)

-- the ONLY surviving tag is the runtime state record
#guard [eq3D, rorD, muxBD, muxND].all (fun D => tagsIn (projOf D) == [tagState])

-- and the surviving comparisons are data-dependent, one per operand as each
-- operator's own semantics requires
#guard cnt eq3D  .eqI == 2   -- every operand compared against the FIRST
#guard cnt rorD  .orB == 2   -- the reduction, one disjunct per operand
#guard cnt muxBD .eqI == 1   -- the single `bv_nonzero` test on the selector
#guard cnt muxND .eqI == 3   -- one selector test per DATA operand: the ite chain

/-- `Op_MuxN`'s selection must be a DIRECT `ite` chain, not a runtime walk of
the operand list.  The control is a same-shape `Op_Or` design: it reads the same
inputs and selects nothing, so if `Op_MuxN` were rebuilding or walking a list
its peel counts would be strictly higher.  They are equal. -/
private def or4D : DesignCert where
  sources  := #[.input 0 4, .input 1 4, .input 2 4, .input 3 4]
  nodes    := #[{ op := .Op_Or, width := 4, deps := #[0, 1, 2, 3] }]
  outputs  := #[{ slot := 4, width := 4 }]
  flops    := #[]
  memories := #[]

#guard cnt muxND .tl == cnt or4D .tl
#guard cnt muxND .hd == cnt or4D .hd
/-! ### Batch 4: the residual criterion

Same rule as batch 3 -- the certificate's structure must be gone, the
hardware's conditionals may remain.  The comparisons leave exactly one `ltI`
each, which is the comparison the operator IS. -/

#guard (cmpOps ++ [LGraphOp.Op_Sext]).all
         (fun o => tagsIn (projOf (binD o)) == [tagState])
#guard tagsIn (projOf (sextD 4)) == [tagState]

-- one comparison per node, for all four
#guard cmpOps.all (fun o => cnt (binD o) .ltI == 1)

-- The sharpest check available that signed and unsigned really take DIFFERENT
-- paths: an unsigned comparison must not reach `bv_sint` at all, and a signed
-- one must not reach `bv_uint`.  The residual shows exactly that
-- complementarity, so the two readings cannot have been conflated.
#guard cnt (binD .Op_ULT) .bvUint == 2 && cnt (binD .Op_ULT) .bvSint == 0
#guard cnt (binD .Op_UGT) .bvUint == 2 && cnt (binD .Op_UGT) .bvSint == 0
#guard cnt (binD .Op_SLT) .bvSint == 2 && cnt (binD .Op_SLT) .bvUint == 0
#guard cnt (binD .Op_SGT) .bvSint == 2 && cnt (binD .Op_SGT) .bvUint == 0

-- `Op_Sext` reads its amount unsigned and the truncated operand SIGNED, once
-- each: the composition `bv_sint (bv_resize n a)` and nothing else
#guard cnt (sextD 4) .bvSint == 1
#guard cnt (sextD 4) .bvUint == 1
#guard cnt (sextD 4) .bvMk   == 1

-- `Op_SHL`: one shifted copy and one XOR per shift amount, and nothing else --
-- in particular no comparison, since the operator needs none
#guard tagsIn (projOf (binD .Op_SHL)) == [tagState]
#guard tagsIn (projOf shl3D) == [tagState]
#guard cnt (binD .Op_SHL) .bvShl == 1 && cnt (binD .Op_SHL) .bvXor == 1
#guard cnt shl3D .bvShl == 2 && cnt shl3D .bvXor == 2

-- batch 2, at the primitive level
#guard (((binR .Op_Xor).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.bvXor
#guard (((binR .Op_Not).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.bvNot
-- `Op_Sum` residualizes to integer arithmetic over `bvUint`, then one `bvMk`
#guard (((binR (.Op_Sum 1)).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.bvUint
#guard (((binR (.Op_Sum 1)).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.subI
#guard (((binR (.Op_Sum 1)).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.bvMk

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
