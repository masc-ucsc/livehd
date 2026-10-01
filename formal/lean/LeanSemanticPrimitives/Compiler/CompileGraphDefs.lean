/-
# `compileGraph` — the topological fold, DEFINITION ONLY

This module holds the EXECUTABLE definitions only, and imports no Mathlib.
Its correctness theorems are unchanged and live in `CompileGraph.lean`, which
imports this module and remains the module every existing importer can
keep using: `CompileGraph` still exposes the definitions AND the theorems.

The split exists for MEMORY, not for layering. `import Mathlib` cost every
sweep probe 6,027,948 kB of resident set that the probe never used -- more
than the entire design term for most modules. `D3Harness`, `ReifyGen` and
`Reify` import this definition path, so a probe loads the compiler without
loading the library its proofs are written in.

Nothing here may acquire a `theorem`: a proof would pull Mathlib back in
through the one import that is supposed to stay absent.
-/
import LeanSemanticPrimitives.Compiler.CompileOpDefs

namespace Compiler
open Residual GraphRefine


--------------------------------------------------------------------------------
-- The compiler over the whole graph
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

end Compiler
