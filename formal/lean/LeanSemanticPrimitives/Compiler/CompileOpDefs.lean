/-
# `compileOp` — the per-node compiler, DEFINITION ONLY

This module holds the EXECUTABLE definitions only, and imports no Mathlib.
Its correctness theorems are unchanged and live in `CompileOp.lean`, which
imports this module and remains the module every existing importer can
keep using: `CompileOp` still exposes the definitions AND the theorems.

The split exists for MEMORY, not for layering. `import Mathlib` cost every
sweep probe 6,027,948 kB of resident set that the probe never used -- more
than the entire design term for most modules. `D3Harness`, `ReifyGen` and
`Reify` import this definition path, so a probe loads the compiler without
loading the library its proofs are written in.

Nothing here may acquire a `theorem`: a proof would pull Mathlib back in
through the one import that is supposed to stay absent.
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

end Compiler
