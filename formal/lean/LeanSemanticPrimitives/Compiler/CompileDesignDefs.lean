/-
# `compileDesign` — the top-level compiler, DEFINITION ONLY

This module holds the EXECUTABLE definitions only, and imports no Mathlib.
Its correctness theorems are unchanged and live in `CompileDesign.lean`, which
imports this module and remains the module every existing importer can
keep using: `CompileDesign` still exposes the definitions AND the theorems.

The split exists for MEMORY, not for layering. `import Mathlib` cost every
sweep probe 6,027,948 kB of resident set that the probe never used -- more
than the entire design term for most modules. `D3Harness`, `ReifyGen` and
`Reify` import this definition path, so a probe loads the compiler without
loading the library its proofs are written in.

Nothing here may acquire a `theorem`: a proof would pull Mathlib back in
through the one import that is supposed to stay absent.
-/
import LeanSemanticPrimitives.Compiler.CompileGraphDefs

namespace Compiler
open Residual GraphRefine DesignCert


--------------------------------------------------------------------------------
-- Step 7: outputs and sequential state
--------------------------------------------------------------------------------

def compileOutput (o : OutputDesc) : ResidualOutput := { slot := o.slot, width := o.width }

/-- Every Flop field is carried across.  Dropping `resetValue` or flipping
`resetActiveLow` here is what `flopNext_agree` refuses to prove. -/
def compileFlop (f : FlopDesc) : ResidualFlopUpdate :=
  { width := f.width, din := f.din, enable := f.enable, resetPin := f.resetPin,
    resetValue := f.resetValue, resetActiveLow := f.resetActiveLow }

def compileMemory (m : MemoryDesc) : ResidualMemoryUpdate :=
  { aw := m.aw, dw := m.dw, nextImg := m.nextImg }

--------------------------------------------------------------------------------
-- `compileDesign`
--------------------------------------------------------------------------------

/-- First dependency that is not strictly earlier, if any.  Checking this inside
`compileDesign` rather than assuming it means `hc : compileDesign D = .ok R`
WITNESSES dependency-ordering, so the final theorem needs no separate
well-formedness hypothesis for it. -/
def firstBadDep (D : DesignCert) : Option (Nat × Nat) :=
  (DesignCert.slotsFrom 0 D.nodes.size).findSome? fun i =>
    match D.nodes[i]? with
    | none   => none
    | some c => (c.deps.toList.find? fun d => decide ¬(d < D.sources.size + i)).map
                  fun d => (D.slotOfNode i, d)

def compileDesign (D : DesignCert) : Except CompileError ResidualProgram :=
  match firstBadDep D with
  | some (n, d) => .error (.depNotEarlier n d)
  | none =>
    match compileGraph D with
    | .error e => .error e
    | .ok bs =>
      .ok { sources       := D.sources
            bindings      := bs
            outputs       := D.outputs.map compileOutput
            flopUpdates   := D.flops.map compileFlop
            memoryUpdates := D.memories.map compileMemory }

/-- Boolean "did it compile?".  Its own definition rather than an `Except`
helper, so the `native_decide` target is a single constructor test and does not
depend on which spelling the library happens to provide. -/
def compilesOk (D : DesignCert) : Bool :=
  match compileDesign D with
  | .ok _    => true
  | .error _ => false

--------------------------------------------------------------------------------
-- The generated-file interface.
--
-- WHY THIS EXISTS.  The obvious shape for a generated design is
--
--     def <Top>_residual  := match compileDesign <Top>_designCert with ...
--     theorem <Top>_compiles : compileDesign <Top>_designCert = .ok <Top>_residual := ...
--
-- and it is a trap.  The second declaration's STATEMENT names a definition whose
-- body is a `match` on `compileDesign <Top>_designCert`, so type-checking it asks
-- the KERNEL to decide `.ok <Top>_residual` defeq `.ok (match compileDesign D …)`.
-- The kernel does not stop at a delta step: it reduces `compileDesign D`, and
-- `Array.push` is `⟨as.toList ++ [a]⟩`, so building 4,772 bindings costs O(N^2)
-- list cells *as kernel terms*.  Measured on `SingleCycleCPU`: >1 h and 120 GB
-- before it was killed, against 38 s / 7.4 GB for the same design when no theorem
-- names a `ResidualProgram`.
--
-- So: no `ResidualProgram` ever appears in a theorem statement.  `compileAndRun`
-- keeps it inside a function body, the witness is a Bool, and the generated
-- theorem is one delta-unfold away from `compileAndRun_correct`.
--------------------------------------------------------------------------------

/-- Compile and run, in one total function.  A design the compiler refuses
returns `default` rather than being a partial function. -/
def compileAndRun (D : DesignCert) (inp : RuntimeInput) (st : RuntimeState) : RuntimeResult :=
  match compileDesign D with
  | .ok R    => denoteResidual R inp st
  | .error _ => default

end Compiler
