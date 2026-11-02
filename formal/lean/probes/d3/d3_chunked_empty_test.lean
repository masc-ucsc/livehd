/-
# Chunked prover on a design with NO compute nodes

A design whose outputs and flop inputs are wired straight to sources has zero
residual bindings. The chunked reifier still emits one (empty) chunk, and the
prover's `preL0` step then had to show `pre0.toList = pre0.toList ++ []`, which
none of its push lemmas rewrites: `simp` failed with "no progress" and the
proof was refused. Seen on all seven zero-node Vortex library blocks
(VX_popcount, VX_onehot_mux, VX_rr_arbiter, ... at their default parameters).

Run: `lake env lean probes/d3/d3_chunked_empty_test.lean` from formal/lean.
It must end with the axiom line below and no error.
-/
import LeanSemanticPrimitives.Compiler.ReifyProof

open Compiler

/-- inputs: 0 = d (4 bits). Output 0 reads d; output 1 reads the flop; the flop
samples d. No node at all. -/
def zDesign : DesignCert :=
  { sources  := #[ SourceDesc.input 0 4, SourceDesc.flopQ 0 4 ]
    nodes    := #[]
    outputs  := #[ { slot := 0, width := 4 }, { slot := 1, width := 4 } ]
    flops    := #[ { width := 4, din := 0, enable := none, resetPin := none
                   , resetValue := 0, resetActiveLow := false, clock := 0
                   , asyncReset := false } ]
    memories := #[]
    clocks   := #[ { name := "clock" } ] }

#guard (compileDesign zDesign).toBool

reify_design_chunked zDesign as zModel size 32
prove_reified_chunked zDesign as zModel size 32

/-- info: 'zModel.correct' depends on axioms: [propext, Classical.choice, Quot.sound] -/
#guard_msgs in
#print axioms zModel.correct
