/-
# Routing between the two certificate codecs -- Mathlib-free -- FP HISTORY

`Format`, `detect` and `parseAny`: which codec a buffer belongs to, DCERT1 (FP's,
`Compiler.CertIO.V1`) or the clock-aware one (`Compiler.CertIO`, DCERT4), and a parse that says
which. `CertIOFacade` proves the router cannot mis-route (`magics_are_exclusive`) and that a
rendered certificate comes back through it unchanged (`parseAny_renderCert`).

NO DRIVER READS A CERTIFICATE THROUGH THIS MODULE. Admission is one function,
`CertIO.readCertChecked` (DCERT4: parse, then `certWfError`); files go through
`CertIO.loadCert` = `Projection.CertLoadAny.admitFile`. DCERT1 is refused by every driver and
proof tool: it has no clocks section and no commit-side async flag, so a DCERT1 file can describe
another design than its RTL, and no tool writes it. The DCERT1 admission that lived here
(`admitV1`, `admitAny`'s DCERT1 branch: parse, then `V1.asyncOK`) was removed with that decision;
DCERT1 stays as FP history in `CertIOV1`, `Projection/CertLoad.lean` and the fp tests.
-/
import LeanSemanticPrimitives.Compiler.CertIOV1
import LeanSemanticPrimitives.Compiler.CertIOCodec

namespace Compiler
namespace CertIO

/-- Which codec a buffer belongs to. -/
inductive Format where
  | /-- DCERT1: FP's format. No clocks section, no commit-side async flag. -/
    v1
  | /-- The clock-aware codec (`Compiler.CertIO`), named for DCERT2, where it
    began: the clock table, `FlopDesc.clock`/`asyncReset` and `MemoryDesc.clock`.
    It was DCERT3 (the memory record gained `init`, `commits`, `asyncImg`) and
    is DCERT4 now (the memory record gained `depth`), the only version
    `parseCert` reads. What the C++ pass emits. -/
    v2
deriving Repr, DecidableEq, Inhabited

def detect (b : ByteArray) : Option Format :=
  if hasMagic b then some .v2
  else if V1.hasMagic b then some .v1
  else none

/-- Parse either version, and SAY WHICH. The caller is told the format rather
than left to infer it from the record, because a DCERT1 file parses to a
certificate that is indistinguishable from a genuinely one-clock DCERT4 one --
and they do not mean the same thing. A parse, not an admission: no driver uses it. -/
def parseAny (b : ByteArray) : Except String (Format × DesignCert) :=
  match detect b with
  | some .v2 => (parseCert b).map (fun D => (Format.v2, D))
  | some .v1 => (V1.parseCert b).map (fun D => (Format.v1, D))
  | none     => .error "unrecognised certificate: expected DCERT4 or DCERT1"

end CertIO
end Compiler
