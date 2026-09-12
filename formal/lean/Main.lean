/-
# `lgraph-sim` — the Direction 2 standalone direct simulator

The binary is a LAUNCHER: it packages certificates as data and calls the generic
`Compiler.Direct.simMain`.  It emits no semantic code, so running it is still
direct interpretation of the certificate.

`DirectTests` is imported deliberately — its `#guard`s run while this file
elaborates, so the binary cannot be built unless the execution gate passes.

To simulate a REAL design, generate a launcher beside its emitted certificate:

    import LeanSemanticPrimitives.Compiler.DirectSim
    import SingleCycleCPU_Lgraph
    def main (args : List String) : IO UInt32 :=
      Compiler.Direct.simMain [("SingleCycleCPU", SingleCycleCPU_designCert)] args

`pass/lean/scripts/make_sim_launcher.py` writes exactly that file.
-/
import LeanSemanticPrimitives.Compiler.DirectExamples
import LeanSemanticPrimitives.Compiler.DirectSim
import LeanSemanticPrimitives.Compiler.DirectTests

/-- The accepted shapes, plus the malformed certificates, so the binary itself
demonstrates that a refusal exits non-zero. -/
def main (args : List String) : IO UInt32 :=
  Compiler.Direct.simMain
    (Compiler.Direct.Examples.registry ++ Compiler.Direct.Examples.rejectRegistry) args
