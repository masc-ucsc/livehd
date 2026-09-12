/-
# `DirectExamples` — the five certificate shapes the direct simulator must cover

Direction 2, phase 2's gate: combinational, sequential, asynchronous reset,
inlined ROM, and mutable memory.  These are hand-written `DesignCert` values,
NOT exporter output, so they exercise the Lean side independently of the C++
extraction boundary; real designs arrive as generated certificates and are swept
separately.

Each one is small enough that `interpretDesign` — quadratic and never otherwise
executed — can actually be run and compared against the direct evaluator.
-/
import LeanSemanticPrimitives.Compiler.DirectTrace

namespace Compiler
namespace Direct
namespace Examples

/-- **Combinational.**  `out = (a + b) & 0xF`, four bits wide.

    slot 0 = a, slot 1 = b, slot 2 = 0xF, slot 3 = a+b, slot 4 = (a+b)&0xF -/
def combAddMask : DesignCert where
  sources  := #[ .input 0 4, .input 1 4, .const 4 15 ]
  nodes    := #[ { op := .Op_Sum 2, width := 4, deps := #[0, 1] }
               , { op := .Op_And,   width := 4, deps := #[3, 2] } ]
  outputs  := #[ { slot := 4, width := 4 } ]
  flops    := #[]
  memories := #[]

/-- **Sequential.**  A 4-bit counter with an enable.  The OUTPUT is driven by a
source slot (the flop's own Q), not by a node — the off-topo case that
`evalDense_slot_agree` exists for.

    slot 0 = Q, slot 1 = 1, slot 2 = enable, slot 3 = Q+1 -/
def counterEnabled : DesignCert where
  sources  := #[ .flopQ 0 4, .const 4 1, .input 0 1 ]
  nodes    := #[ { op := .Op_Sum 2, width := 4, deps := #[0, 1] } ]
  outputs  := #[ { slot := 0, width := 4 } ]
  flops    := #[ { width := 4, din := 3, enable := some 2, resetPin := none,
                   resetValue := 0, resetActiveLow := false } ]
  memories := #[]

/-- **Asynchronous reset.**  The same counter, but `rst_n` (active low) clears Q
IMMEDIATELY: the output reads the reset value in the very cycle reset is
asserted, which a plain `flopQ` cannot express.

    slot 0 = rst_n, slot 1 = Q (async), slot 2 = 1, slot 3 = Q+1 -/
def counterAsyncReset : DesignCert where
  sources  := #[ .input 0 1, .flopQAsync 0 4 0 0 true, .const 4 1 ]
  nodes    := #[ { op := .Op_Sum 2, width := 4, deps := #[1, 2] } ]
  outputs  := #[ { slot := 1, width := 4 } ]
  flops    := #[ { width := 4, din := 3, enable := none, resetPin := some 0,
                   resetValue := 0, resetActiveLow := true } ]
  memories := #[]

/-- **Inlined ROM.**  A four-entry, 8-bit table read combinationally.  A ROM has
NO entry in `RuntimeState.mems` — there is nothing to carry between cycles.

    slot 0 = table, slot 1 = addr, slot 2 = 1 (read enable), slot 3 = data -/
def romTable : DesignCert where
  sources  := #[ .memConst 2 8 #[10, 20, 30, 40], .input 0 2, .const 1 1 ]
  nodes    := #[ { op := .Op_MemRead, width := 8, deps := #[0, 1, 2] } ]
  outputs  := #[ { slot := 3, width := 8 } ]
  flops    := #[]
  memories := #[]

/-- **Mutable memory.**  One byte-enabled write port and one read port.  The
read observes the PRE-write image (its `mem` dependency is the memory SOURCE,
not the write node), which is what makes the cycle boundary observable.

    slot 0 = image, 1 = waddr, 2 = wdata, 3 = we, 4 = raddr, 5 = 1,
    slot 6 = rdata, slot 7 = post-write image -/
def memReadWrite : DesignCert where
  sources  := #[ .memImg 0 2 8, .input 0 2, .input 1 8, .input 2 1, .input 3 2, .const 1 1 ]
  nodes    := #[ { op := .Op_MemRead,        width := 8, deps := #[0, 4, 5] }
               , { op := .Op_MemWriteBE 8,   width := 8, deps := #[0, 1, 2, 3] } ]
  outputs  := #[ { slot := 6, width := 8 } ]
  flops    := #[]
  memories := #[ { aw := 2, dw := 8, nextImg := 7 } ]

/-- **Scale.**  A synthetic dependency chain of `n` adders: node `i` reads node
`i-1`, so nothing about it can be reordered or skipped.  It exists for phase 2's
stack-safety requirement — the evaluator must survive a certificate far larger
than any real design in the census (the largest is 14,860 nodes), and so must
the checker.  Both are loops, not recursions, and this is what says so. -/
def addChain (n : Nat) : DesignCert where
  sources  := #[ .input 0 8, .const 8 1 ]
  nodes    := (Array.range n).map fun i =>
    { op := .Op_Sum 2, width := 8, deps := #[(if i == 0 then 0 else i + 1), 1] }
  outputs  := #[ { slot := n + 1, width := 8 } ]
  flops    := #[]
  memories := #[]

/-- Designs the bundled `lgraph-sim` binary can run by name. -/
def registry : List (String × DesignCert) :=
  [ ("comb-add-mask",       combAddMask)
  , ("counter-enabled",     counterEnabled)
  , ("counter-async-reset", counterAsyncReset)
  , ("rom-table",           romTable)
  , ("mem-read-write",      memReadWrite)
  , ("chain-1k",            addChain 1000)
  , ("chain-200k",          addChain 200000) ]

--------------------------------------------------------------------------------
-- Malformed certificates: each must be REFUSED, not evaluated as zero
--------------------------------------------------------------------------------

/-- `Op_Not` at arity 2 reaches `eval_op`'s wildcard and would evaluate to 0. -/
def badArityNot : DesignCert :=
  { combAddMask with
    nodes := #[ { op := .Op_Sum 2, width := 4, deps := #[0, 1] }
              , { op := .Op_Not,   width := 4, deps := #[3, 2] } ] }

/-- A bit vector used where the memory operand belongs: `CertVal.asMem` on a
`.bv` is a dummy all-zero image. -/
def badKindMemRead : DesignCert :=
  { romTable with
    nodes := #[ { op := .Op_MemRead, width := 8, deps := #[1, 1, 2] } ] }

/-- A dependency on a LATER slot: the graph is not dependency-ordered. -/
def badDepOrder : DesignCert :=
  { combAddMask with
    nodes := #[ { op := .Op_Sum 2, width := 4, deps := #[0, 4] }
              , { op := .Op_And,   width := 4, deps := #[3, 2] } ] }

/-- A zero-width node. -/
def badZeroWidth : DesignCert :=
  { combAddMask with
    nodes := #[ { op := .Op_Sum 2, width := 0, deps := #[0, 1] }
              , { op := .Op_And,   width := 4, deps := #[3, 2] } ] }

/-- `Op_SDiv` is outside the accepted fragment: no exporter site, and `eval_op`'s
equation for it is ungrounded. -/
def badUnsupportedOp : DesignCert :=
  { combAddMask with
    nodes := #[ { op := .Op_SDiv, width := 4, deps := #[0, 1] }
              , { op := .Op_And,  width := 4, deps := #[3, 2] } ] }

/-- An `Op_EQ` at width 4: the LGraph comparison cells are one bit. -/
def badPredicateWidth : DesignCert :=
  { combAddMask with
    nodes := #[ { op := .Op_EQ,  width := 4, deps := #[0, 1] }
              , { op := .Op_And, width := 4, deps := #[3, 2] } ] }

/-- A flop source naming a flop ordinal the certificate does not declare. -/
def badFlopOrdinal : DesignCert :=
  { counterEnabled with sources := #[ .flopQ 7 4, .const 4 1, .input 0 1 ] }

/-- An output slot holding a memory image rather than a bit vector. -/
def badOutputKind : DesignCert :=
  { romTable with outputs := #[ { slot := 0, width := 8 } ] }

/-- An async flop source whose reset VALUE disagrees with its `FlopDesc`. -/
def badAsyncResetValue : DesignCert :=
  { counterAsyncReset with
    sources := #[ .input 0 1, .flopQAsync 0 4 0 7 true, .const 4 1 ] }

/-- A ROM holding more entries than its address width can name. -/
def badRomSize : DesignCert :=
  { romTable with
    sources := #[ .memConst 1 8 #[10, 20, 30, 40], .input 0 2, .const 1 1 ] }

--------------------------------------------------------------------------------
-- Negative controls: mutate ONE live field, require the trace to change
--
-- A mutation that changed nothing would mean the evaluator never reads that
-- field, and every agreement theorem about it would be vacuous.  Each mutant
-- below is itself a WELL-FORMED certificate (the checker accepts it), so what
-- is being tested is the evaluator's sensitivity, not the checker's.
--------------------------------------------------------------------------------

/-- the OPERATOR: `Op_Sum 2` becomes `Op_Xor` -/
def mutOperator : DesignCert :=
  { counterEnabled with nodes := #[ { op := .Op_Xor, width := 4, deps := #[0, 1] } ] }

/-- a DEPENDENCY: `Q + 1` becomes `Q + Q` -/
def mutDependency : DesignCert :=
  { counterEnabled with nodes := #[ { op := .Op_Sum 2, width := 4, deps := #[0, 0] } ] }

/-- a CONSTANT: the increment becomes 2 -/
def mutConstant : DesignCert :=
  { counterEnabled with sources := #[ .flopQ 0 4, .const 4 2, .input 0 1 ] }

/-- an OUTPUT slot: the design reports `Q+1` instead of `Q` -/
def mutOutputSlot : DesignCert :=
  { counterEnabled with outputs := #[ { slot := 3, width := 4 } ] }

/-- a FLOP UPDATE: the enable is dropped, so the counter never holds -/
def mutFlopEnable : DesignCert :=
  { counterEnabled with
    flops := #[ { width := 4, din := 3, enable := none, resetPin := none,
                  resetValue := 0, resetActiveLow := false } ] }

/-- a FLOP RESET POLARITY: `resetActiveLow` flipped on the descriptor.  The
checker deliberately does NOT cross-check this against the source's polarity
(see `DirectCheck`'s header), so the mutant is accepted — and the trace must
change, which is what makes that a judgement call rather than an oversight. -/
def mutFlopResetPolarity : DesignCert :=
  { counterAsyncReset with
    flops := #[ { width := 4, din := 3, enable := none, resetPin := some 0,
                  resetValue := 0, resetActiveLow := false } ] }

/-- a MEMORY UPDATE: the next image is the PRE-write one, so writes vanish -/
def mutMemoryNextImg : DesignCert :=
  { memReadWrite with memories := #[ { aw := 2, dw := 8, nextImg := 0 } ] }

def rejectRegistry : List (String × DesignCert) :=
  [ ("bad-arity-not",        badArityNot)
  , ("bad-kind-mem-read",    badKindMemRead)
  , ("bad-dep-order",        badDepOrder)
  , ("bad-zero-width",       badZeroWidth)
  , ("bad-unsupported-op",   badUnsupportedOp)
  , ("bad-predicate-width",  badPredicateWidth)
  , ("bad-flop-ordinal",     badFlopOrdinal)
  , ("bad-output-kind",      badOutputKind)
  , ("bad-async-reset-value", badAsyncResetValue)
  , ("bad-rom-size",         badRomSize) ]

end Examples
end Direct
end Compiler
