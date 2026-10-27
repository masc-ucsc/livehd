-- Emitted by pass.lean in `formal.lean.mode=verified_compiler`.
-- The model comes from the verified compiler; the checks and theorems below
-- instantiate its generic correctness and all-fire compatibility results.
-- by `Compiler.compileDesign_correct`.  Slots are dense: sources occupy
-- 0..5 and node i occupies 6+i.
import LeanSemanticPrimitives.Compiler.CompileDesign

set_option maxRecDepth 1000000
set_option maxHeartbeats 0

open Compiler

def tiny_sync_ram_designCert : DesignCert :=
  {
    sources  := #[
        SourceDesc.memImg 0 2 8
      , SourceDesc.flopQ 0 8
      , SourceDesc.const 1 (Int.ofNat 1)
      , SourceDesc.const 8 ((Int.ofNat 171))
      , SourceDesc.input 0 2
      , SourceDesc.input 1 2
      ]
    nodes    := #[
        { op := LGraphOp.Op_Or, width := 2, deps := #[4], origin := 1000000001 }
      , { op := LGraphOp.Op_MemRead, width := 8, deps := #[0, 6, 2], origin := 1000000004 }
      , { op := LGraphOp.Op_Or, width := 2, deps := #[5], origin := 1000000005 }
      , { op := LGraphOp.Op_MuxBool, width := 8, deps := #[8, 1, 7], origin := 1000000006 }
      , { op := LGraphOp.Op_Or, width := 2, deps := #[4], origin := 1000000007 }
      , { op := LGraphOp.Op_Or, width := 8, deps := #[3], origin := 1000000009 }
      , { op := LGraphOp.Op_Or, width := 1, deps := #[5], origin := 1000000010 }
      , { op := LGraphOp.Op_MemWrite, width := 8, deps := #[0, 10, 11, 12], origin := 1000000011 }
      ]
    outputs  := #[
        { slot := 1, width := 8 }
      ]
    flops    := #[
        { width := 8, din := 9, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      ]
    memories := #[
        { aw := 2, dw := 8, nextImg := 13, clock := 0 }
      ]
    clocks   := #[
        { name := "clock" }
      ]
  }

/-- Compile-and-run.  The model IS the verified compiler applied to this
certificate -- there is no separately emitted model to disagree with it. -/
def tiny_sync_ram_step : RuntimeInput → RuntimeState → RuntimeResult :=
  compileAndRun tiny_sync_ram_designCert (allEdges tiny_sync_ram_designCert)

/-- Compiler acceptance, independent of clock-bound well-formedness.

`native_decide`, not `.get!`: a compile failure is a BUILD failure rather
than a runtime panic with the hypothesis left undischarged.  (`decide` is
not usable -- `Array.map` is not kernel-reducible.) -/
theorem tiny_sync_ram_compiles : compilesOk tiny_sync_ram_designCert = true := by
  native_decide

/-- Declared clock bounds, checked independently of compiler acceptance. -/
theorem tiny_sync_ram_clock_bounds :
    (∀ f ∈ tiny_sync_ram_designCert.flops, f.clock < tiny_sync_ram_designCert.clocks.size) ∧
    (∀ m ∈ tiny_sync_ram_designCert.memories, m.clock < tiny_sync_ram_designCert.clocks.size) := by
  native_decide

/-- The all-fire instance of arbitrary-edge compiler correctness. -/
theorem tiny_sync_ram_step_correct : ∀ inp st,
    tiny_sync_ram_step inp st = interpretDesign tiny_sync_ram_designCert (allEdges tiny_sync_ram_designCert) inp st :=
  compileAndRun_correct tiny_sync_ram_designCert tiny_sync_ram_compiles (allEdges tiny_sync_ram_designCert)

/-- Compatibility with the preserved every-step interpreter, using the
independent clock-bound witness rather than assuming it from compilation. -/
theorem tiny_sync_ram_step_legacy_correct : ∀ inp st,
    tiny_sync_ram_step inp st = interpretDesignLegacy tiny_sync_ram_designCert inp st := by
  intro inp st
  exact (tiny_sync_ram_step_correct inp st).trans
    (interpretDesign_allEdges tiny_sync_ram_designCert tiny_sync_ram_clock_bounds.1 tiny_sync_ram_clock_bounds.2 inp st)

/-- For evaluation only.  Deliberately NOT mentioned in any theorem
statement -- see the comment above. -/
def tiny_sync_ram_residual : ResidualProgram :=
  match compileDesign tiny_sync_ram_designCert with
  | .ok R    => R
  | .error _ => default

#print axioms tiny_sync_ram_step_correct
#print axioms tiny_sync_ram_step_legacy_correct
