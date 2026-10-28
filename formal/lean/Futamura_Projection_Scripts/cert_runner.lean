/- Thin top-level entry point: `lean --run` needs `main` in the file it runs.
   All the content is in `Projection/CertLoad.lean`, which the core build
   type-checks. -/
import LeanSemanticPrimitives.Projection.CertLoad

def main (args : List String) : IO UInt32 := Projection.CertLoad.main args
