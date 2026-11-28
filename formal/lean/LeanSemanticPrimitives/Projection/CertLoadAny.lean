/-
# The one certificate loader of the drivers and proof tools

`admitFile` reads a certificate file and admits it with `CertIO.readCertChecked`: parse (DCERT4),
then `certWfError` (at least one clock, every flop and memory clock ordinal declared, every output
slot in range, every commit-table entry and async image inside the design, every memory's depth
inside its table, no address past a depth). It is `CertIO.loadCert`, under the name the drivers
use. Every simulation driver and proof tool loads its certificates through it, or, given bytes,
through `readCertChecked` itself; an emitted theorem file decodes its embedded text with
`readCertChecked` too. So one certificate means one design to every tool, and a certificate one
tool refuses, every tool refuses.

Before this was one function there were four loader policies: DCERT1 was admitted by some drivers
with FP's `asyncOK` check and by the probes without it, so `cert_trace` could run, as a different
design, a certificate `cert_run` refused. DCERT1 is now refused everywhere (`CertIORoute` says
why); `CertLoad.lean` keeps FP's DCERT1 codec checks as history.
-/

import LeanSemanticPrimitives.Compiler.CertIOCodec
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.RunAdmission

namespace Projection
namespace CertLoadAny

open Compiler

/-- **Read a certificate file and admit it** (`CertIO.readCertChecked`). A refusal is an
`IO.userError` `<file>: parse: ...` or `<file>: refused: ...`. -/
def admitFile (p : System.FilePath) : IO DesignCert :=
  CertIO.loadCert p

end CertLoadAny
end Projection
