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
                  (P .bvMk [R "w", int 0])))))] },

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

/-- What `I_hw`'s unchecked reads ASSUME about the runtime vectors.

`nthD` has no bounds check: an out-of-range read is `hd nil`, a type error, not
a default.  `interpretDesign` is total exactly where `I_hw` is not -- `fires`
reads an undeclared ordinal as `false` -- so the two agree only where these
hold.  Stated now, and not discovered later inside an adequacy proof, because
the edge vector is the THIRD such vector and the third place the assumption
would otherwise be silent.

`edgesSized` is `RuntimeSemWF.edgesSized` in d2's checker and `flopClocks` /
`memClocks` are `DesignSemWF`'s; the checker itself is not ported here, so these
are carried as hypotheses.  They are also exactly the hypotheses
`interpretDesign_allEdges` takes. -/
structure RuntimeSized (D : Compiler.DesignCert) (e : Compiler.ClockEdges) : Prop where
  edgesSized : D.clocks.size ≤ e.size
  flopClocks : ∀ f ∈ D.flops, f.clock < D.clocks.size
  memClocks  : ∀ m ∈ D.memories, m.clock < D.clocks.size

/-- The all-fire stimulus satisfies the sizing half for any declared design. -/
theorem RuntimeSized_allEdges {D : Compiler.DesignCert}
    (hf : ∀ f ∈ D.flops, f.clock < D.clocks.size)
    (hm : ∀ m ∈ D.memories, m.clock < D.clocks.size) :
    RuntimeSized D (Compiler.allEdges D) :=
  { edgesSized := by simp [Compiler.allEdges_size]
    flopClocks := hf, memClocks := hm }

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

#guard [LGraphOp.Op_Or, .Op_SRA, .Op_GetMask].all
         (fun o => (projectDesign (binD o)).toOption.isSome)

-- no design tag survives, for any of them
#guard [LGraphOp.Op_Or, .Op_SRA, .Op_GetMask].all (fun o =>
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
#guard [LGraphOp.Op_Or, .Op_SRA, .Op_GetMask].all (fun o =>
  !(((binR o).funs.map (fun fd => primsOf fd.body)).flatten).contains Prim.eqI)

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
