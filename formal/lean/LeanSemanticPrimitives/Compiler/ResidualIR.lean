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

One constructor per LGraph operator family the exporter emits.  Five operators
get no constructor, so `compileOp` returns `CompileError` for them:

    Op_Const  Op_Sub  Op_Div  Op_LT  Op_GT

(`Op_Const` is absent as a *node* op because constants are certificate SOURCES.
`Op_Sub`/`Op_LT`/`Op_GT` are normalised away by cprop into `Op_Sum` with
subtrahends and `Op_ULT`/`Op_SLT`; a division is exported as `Op_UDiv` or
`Op_SDiv` by operand signedness, never as the signless `Op_Div`.)  A refusal is
loud, so if one ever appears the sweep reports a `CompileError`, never a wrong
proof.  `Op_UDiv`, `Op_SDiv` (b6d917c25, 0143ea8cc) and `Op_SetMask` were once
listed here as unreachable; the exporter emits all three, so they compile.
-/
import LeanSemanticPrimitives.Compiler.Runtime
import LeanSemanticPrimitives.Compiler.MemDomains

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
  /-- `Op_UDiv`: unsigned quotient; a zero divisor gives 0 -/
  | rudiv       (w : Nat) (a b : ResidualRef)
  /-- `Op_SDiv`: two's-complement quotient truncated toward zero -/
  | rsdiv       (w : Nat) (a b : ResidualRef)
  /-- `Op_SetMask`, deps `[a, mask, value]` -/
  | rsetMask    (w : Nat) (a m v : ResidualRef)
  /-- `Op_MuxBool`, deps `[sel, falseVal, trueVal]` — order matters and is checked -/
  | rmux        (w : Nat) (sel fv tv : ResidualRef)
  -- ── memory (item 1: in the IR from the start, before step 5) ──────────────
  | rmemRead    (w : Nat) (m a en : ResidualRef)
  | rmemWrite   (m a d en : ResidualRef)
  | rmemWriteBE (w : Nat) (byteW : Nat) (m a d be : ResidualRef)
  /-- `Op_MemLoad n` (decision #2), deps `[image, bus, enable]`: entries `0..n-1`
  from the bus when enabled; `w` is the entry width -/
  | rmemLoad    (w n : Nat) (m bus en : ResidualRef)
  /-- `Op_MemToBus n`: entries `0..n-1` as one `w`-bit bus, entry 0 lowest -/
  | rmemToBus   (w n : Nat) (m : ResidualRef)
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
  /-- ordinal of the clock domain this flop commits on (`FlopDesc.clock`) -/
  clock          : Nat
  /-- the reset acts even in a step where `clock` is quiet (`FlopDesc.asyncReset`) -/
  asyncReset     : Bool
deriving Repr, Inhabited, DecidableEq

structure ResidualMemoryUpdate where
  aw      : Nat
  dw      : Nat
  nextImg : Nat
  /-- ordinal of the clock domain this memory commits on (`MemoryDesc.clock`) -/
  clock   : Nat
  /-- the commit table of a memory on more than one domain (`MemoryDesc.commits`) -/
  commits : Array MemCommit := #[]
  /-- the power-on words (`MemoryDesc.init`): not part of a step, carried so a
  generated restatement of the certificate's memories (D3's `D_mems`) is exact -/
  init    : Array Nat := #[]
deriving Repr, Inhabited, DecidableEq

/-- The `MemoryDesc` view of an update: what `MemoryDesc.allFire` / `firedIs`
read (its `clock` and `commits`), so the target asks "which domains fired" with
the same predicates as the source. -/
def ResidualMemoryUpdate.toDesc (m : ResidualMemoryUpdate) : MemoryDesc :=
  { aw := m.aw, dw := m.dw, nextImg := m.nextImg, clock := m.clock, commits := m.commits }

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
