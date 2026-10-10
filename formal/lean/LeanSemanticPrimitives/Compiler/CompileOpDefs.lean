/-
# `CompileOp` — DEFINITIONS ONLY

Derived from `CompileOp.lean` by `generated/port_staging/derive_defs_split.py`,
carrying D3's split onto master's CLOCK-AWARE definitions. D3's own
`CompileOpDefs.lean` is deliberately not used: it predates the clock schema
(`compileFlop`/`compileMemory` without `clock`/`asyncReset`, a
`ResidualProgram` without `clocks`), and copying it would revert the transport.

This module imports no Mathlib. `CompileOp.lean` imports it and keeps every
theorem, so each existing importer of `CompileOp` is unaffected.

The split exists for MEMORY, not layering: measured on D3, `import Mathlib`
cost every sweep probe 6,027,948 kB of resident set it never used.

NOTHING HERE MAY ACQUIRE A `theorem`: a proof would pull Mathlib back in
through the one import that is meant to stay absent.
-/
import LeanSemanticPrimitives.Compiler.ResidualSemantics
import LeanSemanticPrimitives.Compiler.DesignSemantics


namespace Compiler
open Residual

--------------------------------------------------------------------------------
-- The compiler, per node
--------------------------------------------------------------------------------

/-- Compile one node.  `ResidualRef = Nat` is the same dense slot space the
node's `deps` already live in, so the dep→ref mapping is the identity and the
only thing that can go wrong is which dep lands in which constructor POSITION —
which is exactly what `compileOp_correct` checks. -/
def compileOp (nid : Nat) (c : DenseNodeCert) : Except CompileError ResidualExpr :=
  if c.width = 0 then .error (.zeroWidth nid) else
  match c.op with
  -- variadic: any arity is meaningful
  | .Op_Sum n => .ok (.rsum c.width n c.deps)
  | .Op_Mult  => .ok (.rmult c.width c.deps)
  | .Op_And   => .ok (.rand c.width c.deps)
  | .Op_Or    => .ok (.rorBits c.width c.deps)
  | .Op_Xor   => .ok (.rxor c.width c.deps)
  | .Op_Ror   => .ok (.rredOr c.width c.deps)
  | .Op_EQ    => .ok (.req c.width c.deps)
  | .Op_SHL   => .ok (.rshl c.width c.deps)
  | .Op_MuxN  => .ok (.rmuxN c.width c.deps)
  -- fixed arity: a wrong arity is refused, not zero-filled
  | .Op_Not =>
      match c.deps.toList with
      | [a] => .ok (.rnot c.width a)
      | l   => .error (.badArity c.op l.length)
  | .Op_ULT =>
      match c.deps.toList with
      | [a, b] => .ok (.rult c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_UGT =>
      match c.deps.toList with
      | [a, b] => .ok (.rugt c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_SLT =>
      match c.deps.toList with
      | [a, b] => .ok (.rslt c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_SGT =>
      match c.deps.toList with
      | [a, b] => .ok (.rsgt c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_SRA =>
      match c.deps.toList with
      | [a, b] => .ok (.rsra c.width a b)
      | l      => .error (.badArity c.op l.length)
  | .Op_Sext =>
      match c.deps.toList with
      | [a, amt] => .ok (.rsext c.width a amt)
      | l        => .error (.badArity c.op l.length)
  | .Op_GetMask =>
      match c.deps.toList with
      | [a, m] => .ok (.rgetMask c.width a m)
      | l      => .error (.badArity c.op l.length)
  | .Op_MuxBool =>
      -- deps are [sel, falseVal, trueVal]; this ORDER is what the proof checks
      match c.deps.toList with
      | [sel, fv, tv] => .ok (.rmux c.width sel fv tv)
      | l             => .error (.badArity c.op l.length)
  -- memory
  | .Op_MemRead =>
      match c.deps.toList with
      | [m, a, en] => .ok (.rmemRead c.width m a en)
      | l          => .error (.badArity c.op l.length)
  | .Op_MemWrite =>
      match c.deps.toList with
      | [m, a, d, en] => .ok (.rmemWrite m a d en)
      | l             => .error (.badArity c.op l.length)
  | .Op_MemWriteBE bw =>
      match c.deps.toList with
      | [m, a, d, be] => .ok (.rmemWriteBE c.width bw m a d be)
      | l             => .error (.badArity c.op l.length)
  -- the eight the census found unreachable
  | op => .error (.unsupportedOp op)

--------------------------------------------------------------------------------
-- Value-level operator lemmas.  These are the substance: each says one TARGET
-- primitive agrees with one SOURCE operator, at the value level, for all
-- operands.  The graph-level plumbing (deps -> values) is done once, below.
--------------------------------------------------------------------------------

namespace Residual
end Residual
open Residual

--------------------------------------------------------------------------------
-- Step 5, the graph-level lemma.
--
-- ONE theorem per operator FAMILY, never one per graph node.  The hypothesis
-- `hagree` is the plan's "all compiled operand references have the same values
-- as the interpreter's operands"; the conclusion is "the compiled expression
-- produces the same result as the interpreter's operator".
--------------------------------------------------------------------------------
end Compiler
