/-
# Routing between the two certificate codecs -- Mathlib-free

`Format`, `detect`, `parseAny` and the three admissions (`admitV2`, `admitV1`,
`admitAny`), moved verbatim out of `Compiler/CertIOFacade.lean` in the same
namespace, so every name is unchanged.  The facade imports this module and
keeps the two theorems (`magics_are_exclusive`, `parseAny_renderCert`); the
second needs the round-trip proof, which needs Mathlib.  Drivers that only
ROUTE and ADMIT -- the Futamura-projection loaders and native drivers -- import
this module instead and stay Mathlib-free.

The policy is the facade's, unchanged: DCERT2 is admitted by `readCertChecked`
(parse, then `certWfError`); DCERT1 by FP's parse-then-`asyncOK`, without
`certWfError`.  See `CertIOFacade.lean` for the reasons.
-/
import LeanSemanticPrimitives.Compiler.CertIOV1
import LeanSemanticPrimitives.Compiler.CertIOCodec

namespace Compiler
namespace CertIO

/-- Which codec a buffer belongs to. -/
inductive Format where
  | /-- DCERT1: FP's format. No clocks section, no commit-side async flag. -/
    v1
  | /-- DCERT2: carries the clock table, `FlopDesc.clock`/`asyncReset` and
    `MemoryDesc.clock`. What the C++ pass emits. -/
    v2
deriving Repr, DecidableEq, Inhabited

def detect (b : ByteArray) : Option Format :=
  if hasMagic b then some .v2
  else if V1.hasMagic b then some .v1
  else none

/-- Parse either version, and SAY WHICH. The caller is told the format rather
than left to infer it from the record, because a DCERT1 file parses to a
certificate that is indistinguishable from a genuinely one-clock DCERT2 one --
and they do not mean the same thing. -/
def parseAny (b : ByteArray) : Except String (Format × DesignCert) :=
  match detect b with
  | some .v2 => (parseCert b).map (fun D => (Format.v2, D))
  | some .v1 => (V1.parseCert b).map (fun D => (Format.v1, D))
  | none     => .error "unrecognised certificate: expected DCERT2 or DCERT1"

/-- D4's admission, unchanged. -/
def admitV2 (b : ByteArray) : Except String DesignCert := readCertChecked b

/-- FP's admission, unchanged: parse, then the transport constraint the format
forces. `asyncOK` is the one field whose loss FP could not ignore -- a DCERT1
file whose `flopQAsync` sources disagree with the flops it parses to describes
a DIFFERENT design from the one its exporter meant. -/
def admitV1 (b : ByteArray) : Except String DesignCert :=
  match V1.parseCert b with
  | .error e => .error s!"parse: {e}"
  | .ok D    =>
    if V1.asyncOK D then .ok D
    else .error "refused: a flopQAsync source names a flop the file records as \
                 synchronous; DCERT1 cannot carry the commit-side flag"

/-- Admit by version, each under its own policy. -/
def admitAny (b : ByteArray) : Except String (Format × DesignCert) :=
  match detect b with
  | some .v2 => (admitV2 b).map (fun D => (Format.v2, D))
  | some .v1 => (admitV1 b).map (fun D => (Format.v1, D))
  | none     => .error "unrecognised certificate: expected DCERT2 or DCERT1"

end CertIO
end Compiler
