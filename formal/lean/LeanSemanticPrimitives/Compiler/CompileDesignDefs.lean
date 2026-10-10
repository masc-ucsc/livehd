/-
# `CompileDesign` — DEFINITIONS ONLY

Derived from `CompileDesign.lean` by `generated/port_staging/derive_defs_split.py`,
carrying D3's split onto master's CLOCK-AWARE definitions. D3's own
`CompileDesignDefs.lean` is deliberately not used: it predates the clock schema
(`compileFlop`/`compileMemory` without `clock`/`asyncReset`, a
`ResidualProgram` without `clocks`), and copying it would revert the transport.

This module imports no Mathlib. `CompileDesign.lean` imports it and keeps every
theorem, so each existing importer of `CompileDesign` is unaffected.

The split exists for MEMORY, not layering: measured on D3, `import Mathlib`
cost every sweep probe 6,027,948 kB of resident set it never used.

NOTHING HERE MAY ACQUIRE A `theorem`: a proof would pull Mathlib back in
through the one import that is meant to stay absent.
-/
import LeanSemanticPrimitives.Compiler.CompileGraphDefs


namespace Compiler
open Residual GraphRefine DesignCert
def compileOutput (o : OutputDesc) : ResidualOutput := { slot := o.slot, width := o.width }

/-- Every Flop field is carried across.  Dropping `resetValue` or flipping
`resetActiveLow` here is what `flopNext_agree` refuses to prove. -/
def compileFlop (f : FlopDesc) : ResidualFlopUpdate :=
  { width := f.width, din := f.din, enable := f.enable, resetPin := f.resetPin,
    resetValue := f.resetValue, resetActiveLow := f.resetActiveLow,
    clock := f.clock, asyncReset := f.asyncReset }

def compileMemory (m : MemoryDesc) : ResidualMemoryUpdate :=
  { aw := m.aw, dw := m.dw, nextImg := m.nextImg, clock := m.clock }

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

/-- Compile and run, in one total function.  A design the compiler refuses
returns `default` rather than being a partial function. -/
def compileAndRun (D : DesignCert) (e : ClockEdges) (inp : RuntimeInput) (st : RuntimeState) :
    RuntimeResult :=
  match compileDesign D with
  | .ok R    => denoteResidual R e inp st
  | .error _ => default

end Compiler
