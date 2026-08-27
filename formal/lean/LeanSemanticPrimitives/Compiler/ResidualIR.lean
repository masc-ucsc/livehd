/-
# `ResidualIR` — the target language

Step 4 of the B1+B2 plan.  This is a **separate Lean language for the compiler
output**.  Its semantics (`ResidualSemantics.lean`) must not call
`NodeSemantics.interpOp` / `eval_op`, and must not traverse a `GraphCert`.  If
it did, `compileOp_correct` would be structurally trivial and would validate
nothing about the operator translation.

## References

`ResidualRef := Nat` — a GLOBAL slot index in the dense space `DesignCert` sets
up.  The plan's `input | state | binding` inductive is *already* recorded, once,
in `ResidualProgram.sources`: slots below `sources.size` are sources (inputs,
flop Qs, memory images) and slots above are bindings, positionally.  Collapsing
the ref to a slot index keeps lookup O(1) (`Array.getElem?`) and makes the
dep→ref mapping the identity, which removes a whole bug class rather than
proving it away.

It does NOT weaken the operand-order check the independence is for: that check
is about which *dep position* lands in which *constructor argument*.  If
`compileOp` builds `.mux w sel trueRef falseRef`, `compile_mux_correct` fails to
prove regardless of how refs are represented.

## Constructor set

One constructor per LGraph operator family found REACHABLE by the census over
all 123 generated designs.  Eight operators appear nowhere and get no
constructor, so `compileOp` must return `CompileError` for them:

    Op_Const  Op_Sub  Op_Div  Op_UDiv  Op_SDiv  Op_LT  Op_GT  Op_SetMask

(`Op_Const` is absent as a *node* op because constants are certificate SOURCES.
`Op_Sub`/`Op_LT`/`Op_GT` are normalised away by cprop into `Op_Sum` with
subtrahends and `Op_ULT`/`Op_SLT`.)  A refusal is loud, so if one of the eight
ever appears the sweep reports a `CompileError`, never a wrong proof.
-/
import LeanSemanticPrimitives.Compiler.Runtime

namespace Compiler

/-- A slot index in the dense space. -/
abbrev ResidualRef := Nat

/-- Slot types.  Mirrors `CertVal`'s `bv | mem` split — a memory value is
`Int → BV`, not a bit vector, so the IR has to carry the distinction (item 1). -/
inductive ValueType where
  | bv  (w : Nat)
  | mem (aw dw : Nat)
deriving Repr, Inhabited, DecidableEq

/-- The target language. -/
inductive ResidualExpr where
  -- ── variadic ──────────────────────────────────────────────────────────────
  /-- `Op_Sum n_add`: first `nAdd` args added, rest subtracted -/
  | rsum        (w : Nat) (nAdd : Nat) (args : Array ResidualRef)
  | rmult       (w : Nat) (args : Array ResidualRef)
  | rand        (w : Nat) (args : Array ResidualRef)
  /-- bitwise OR (`Op_Or`) -/
  | rorBits     (w : Nat) (args : Array ResidualRef)
  | rxor        (w : Nat) (args : Array ResidualRef)
  /-- reduction OR (`Op_Ror`): 1 iff any arg is nonzero -/
  | rredOr      (w : Nat) (args : Array ResidualRef)
  /-- equality reduction (`Op_EQ`): 1 iff all args equal the first -/
  | req         (w : Nat) (args : Array ResidualRef)
  | rshl        (w : Nat) (args : Array ResidualRef)
  | rmuxN       (w : Nat) (args : Array ResidualRef)
  -- ── fixed arity ───────────────────────────────────────────────────────────
  | rnot        (w : Nat) (a : ResidualRef)
  | rult        (w : Nat) (a b : ResidualRef)
  | rugt        (w : Nat) (a b : ResidualRef)
  | rslt        (w : Nat) (a b : ResidualRef)
  | rsgt        (w : Nat) (a b : ResidualRef)
  | rsra        (w : Nat) (a b : ResidualRef)
  /-- general sign extension: keep `amt` bits, sign bit at `amt-1` -/
  | rsext       (w : Nat) (a amt : ResidualRef)
  | rgetMask    (w : Nat) (a m : ResidualRef)
  /-- `Op_MuxBool`, deps `[sel, falseVal, trueVal]` — order matters and is checked -/
  | rmux        (w : Nat) (sel fv tv : ResidualRef)
  -- ── memory (item 1: in the IR from the start, before step 5) ──────────────
  | rmemRead    (w : Nat) (m a en : ResidualRef)
  | rmemWrite   (m a d en : ResidualRef)
  | rmemWriteBE (w : Nat) (byteW : Nat) (m a d be : ResidualRef)
deriving Repr, Inhabited, DecidableEq

/-- One compiled binding.  Its slot is its POSITION: binding `i` occupies slot
`numSources + i`.  There is no `slot` field to disagree with the position. -/
structure ResidualBinding where
  ty  : ValueType
  rhs : ResidualExpr
deriving Repr, Inhabited, DecidableEq

structure ResidualOutput where
  slot  : Nat
  width : Nat
deriving Repr, Inhabited, DecidableEq

structure ResidualFlopUpdate where
  width          : Nat
  din            : Nat
  enable         : Option Nat
  resetPin       : Option Nat
  resetValue     : Int
  resetActiveLow : Bool
deriving Repr, Inhabited, DecidableEq

structure ResidualMemoryUpdate where
  aw      : Nat
  dw      : Nat
  nextImg : Nat
deriving Repr, Inhabited, DecidableEq

/-- The compiled program.  `sources` is carried so `denoteResidual` needs no
`DesignCert` at all — the target is executable on its own. -/
structure ResidualProgram where
  sources       : Array SourceDesc
  bindings      : Array ResidualBinding
  outputs       : Array ResidualOutput
  flopUpdates   : Array ResidualFlopUpdate
  memoryUpdates : Array ResidualMemoryUpdate
deriving Repr, Inhabited, DecidableEq

/-- Errors the compiler may report.  Refusing is always preferable to emitting
zero — `eval_op` has a silent `| _, w, _ => mk_bv w 0` fallback at TWO sites
(`LGraphModel.lean:200` and `:256`) and that is the defect this replaces. -/
inductive CompileError where
  /-- operator has no residual constructor (one of the eight unreachable ops) -/
  | unsupportedOp   (op : LGraphOp)
  /-- operator applied at an arity its residual constructor cannot express -/
  | badArity        (op : LGraphOp) (got : Nat)
  /-- a dependency names a slot that is not strictly earlier -/
  | depNotEarlier   (node : Nat) (dep : Nat)
  /-- a width of zero, which no operator may produce -/
  | zeroWidth       (node : Nat)
  /-- an output / flop pin / memory image names a slot outside the design -/
  | slotOutOfRange  (slot : Nat)
deriving Repr, Inhabited, DecidableEq

end Compiler
