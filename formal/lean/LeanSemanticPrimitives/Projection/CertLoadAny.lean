/-
# One loader for both certificate formats

`CertLoad` is FP's production runner and reads DCERT1 only, through
`Compiler.CertIO.V1`.  Master's C++ pass emits DCERT3 only.  So until this
module, no Projection runner could read a certificate master itself produces:
DCERT3 reached the projection only through driver scripts that import the
Mathlib-side `CertIOFacade`.

This module routes by magic through `Compiler.CertIORoute` (Mathlib-free) and
keeps each format's own policy:

  * DCERT1 -- exactly FP's behaviour, nothing tightened or loosened.
    `loadCertAny` parses with `V1.parseCert` and admits nothing, as
    `V1.loadCert` always did for the probes. `admitFile` (every driver and proof
    tool) runs FP's `asyncOK` transport check through `CertIO.admitAny`.
  * DCERT3 -- `CertIO.readCertChecked`: parse, then `certWfError` (at least one
    clock, every flop and memory clock ordinal declared, every output slot in
    range).  The format carries `FlopDesc.asyncReset`, so FP's `asyncOK`
    transport constraint -- which exists only because DCERT1 cannot -- does not
    apply.

`CertLoad.lean` itself is untouched, so its two audited theorems
(`slotsInRange_of_bool`, `supported_of_report`) are unchanged.
-/

import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIORoute
import LeanSemanticPrimitives.Compiler.RunAdmission

namespace Projection
namespace CertLoadAny

open Compiler Projection.Hw Projection.CertLoad

/-- Load either format and SAY WHICH.  A DCERT1 file parses to a one-clock
record indistinguishable from a genuinely one-clock DCERT3 one, so the caller
is told the format instead of inferring it. -/
def loadAny (p : System.FilePath) : IO (CertIO.Format × DesignCert) := do
  let b ← IO.FS.readBinFile p
  match CertIO.detect b with
  | some .v1 =>
      match CertIO.V1.parseCert b with
      | .ok D    => pure (.v1, D)
      | .error e => throw (IO.userError s!"{p}: {e}")
  | some .v2 =>
      match CertIO.readCertChecked b with
      | .ok D    => pure (.v2, D)
      | .error e => throw (IO.userError s!"{p}: {e}")
  | none =>
      throw (IO.userError s!"{p}: unrecognised certificate: expected DCERT4, DCERT3 or DCERT1")

/-- **Read a certificate file and admit it under its format's policy** (`CertIO.admitAny`): DCERT4
and DCERT3 through `readCertChecked` (parse, then `certWfError`: shapes, memory depths,
addresses), DCERT1 through `admitV1`, which also refuses a file whose async reset it cannot
carry (`V1.asyncOK`: such a file describes another design). Every simulation driver and proof
tool loads its certificates through this; a refusal is an `IO.userError` naming the file, as
`loadAny`'s. `loadAny` skips the DCERT1 check and is kept for the old probes only. -/
def admitFile (p : System.FilePath) : IO (CertIO.Format × DesignCert) := do
  match CertIO.admitAny (← IO.FS.readBinFile p) with
  | .ok r => pure r
  | .error e => throw (IO.userError s!"{p}: {e}")

/-- Drop-in for `CertIO.V1.loadCert` in the probes.  On a DCERT1 file it is
exactly that function (same parse, same error text); on DCERT3 it admits
through `readCertChecked`. -/
def loadCertAny (p : System.FilePath) : IO DesignCert :=
  return (← loadAny p).2

end CertLoadAny
end Projection
