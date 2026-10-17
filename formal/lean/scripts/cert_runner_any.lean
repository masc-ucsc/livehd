/- Thin top-level entry point for `Projection/CertLoadAny.lean`: like
   `cert_runner.lean`, but it reads DCERT1 (routed to FP's unchanged `runOne`)
   and DCERT2 (admitted by `readCertChecked`).  `lean --run` needs `main` in the
   file it runs; all content is in the module, which the core build checks. -/
import LeanSemanticPrimitives.Projection.CertLoadAny

def main (args : List String) : IO UInt32 := Projection.CertLoadAny.main args
