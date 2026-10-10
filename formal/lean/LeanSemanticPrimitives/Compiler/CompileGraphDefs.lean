/-
# `CompileGraph` — DEFINITIONS ONLY

Derived from `CompileGraph.lean` by `generated/port_staging/derive_defs_split.py`,
carrying D3's split onto master's CLOCK-AWARE definitions. D3's own
`CompileGraphDefs.lean` is deliberately not used: it predates the clock schema
(`compileFlop`/`compileMemory` without `clock`/`asyncReset`, a
`ResidualProgram` without `clocks`), and copying it would revert the transport.

This module imports no Mathlib. `CompileGraph.lean` imports it and keeps every
theorem, so each existing importer of `CompileGraph` is unaffected.

The split exists for MEMORY, not layering: measured on D3, `import Mathlib`
cost every sweep probe 6,027,948 kB of resident set it never used.

NOTHING HERE MAY ACQUIRE A `theorem`: a proof would pull Mathlib back in
through the one import that is meant to stay absent.
-/
import LeanSemanticPrimitives.Compiler.CompileOpDefs


namespace Compiler
open Residual GraphRefine

--------------------------------------------------------------------------------
-- `runBindings`: size, stability of earlier slots, and the value at each slot
--------------------------------------------------------------------------------
/-- Advisory slot type.  The semantics never reads it — `CertVal` carries the
`bv | mem` distinction at runtime — but the exporter and any external checker
want it. -/
def opValueType (c : DenseNodeCert) : ValueType :=
  match c.op with
  | .Op_MemWrite     => .mem 0 c.width
  | .Op_MemWriteBE _ => .mem 0 c.width
  | _                => .bv c.width

/-- Compile `n` nodes starting at dense index `start`, appending to `acc`.
Structural recursion on the COUNT, so no termination proof is needed. -/
def compileFrom (D : DesignCert) (start : Nat) :
    Nat → Array ResidualBinding → Except CompileError (Array ResidualBinding)
  | 0,     acc => .ok acc
  | n + 1, acc =>
    match D.nodes[start]? with
    | none   => .error (.slotOutOfRange (D.slotOfNode start))
    | some c =>
      match compileOp (D.slotOfNode start) c with
      | .error err => .error err
      | .ok e      => compileFrom D (start + 1) n (acc.push { ty := opValueType c, rhs := e })

def compileGraph (D : DesignCert) : Except CompileError (Array ResidualBinding) :=
  compileFrom D 0 D.nodes.size #[]

--------------------------------------------------------------------------------
-- What a successful compile witnesses
--------------------------------------------------------------------------------

end Compiler
