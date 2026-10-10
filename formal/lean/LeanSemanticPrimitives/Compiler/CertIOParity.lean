/-
What a BYTE comparison against `renderCert` is allowed to conclude.

The C++ pass and the Lean library both walk one `CertificateIR`/`DesignCert`
and write DCERT2. Comparing their bytes is cheap; what we want from it is a
statement about FIELDS. That implication is a theorem, not an assumption:
`renderCert` is injective, because `parseCert` inverts it unconditionally.

Kept separate from `CertIORoundTrip` so that module's audited object stays
byte-identical to the one its axioms were reported from.
-/
import LeanSemanticPrimitives.Compiler.CertIORoundTrip

namespace Compiler
namespace CertIO

/-- Equal bytes mean the SAME certificate -- every field, not just the ones a
comparison happened to look at.

So a parity run that finds the C++ bytes equal to `renderCert D` has shown
that the design the C++ pass serialised IS `D`: there is no field it could
differ in and still produce these bytes. -/
theorem renderCert_injective {A B : DesignCert} (h : renderCert A = renderCert B) :
    A = B := by
  have hA : parseCert (renderCert A) = .ok A := parseCert_renderCert A
  rw [h, parseCert_renderCert B] at hA
  exact (Except.ok.inj hA).symm

/-- The other direction of the same fact, in the form a parity harness uses:
bytes that the verified reader accepts re-render to themselves exactly when
they were rendered in the first place. -/
theorem parse_then_render {b : ByteArray} {D : DesignCert} (h : parseCert b = .ok D)
    (hb : ∃ C, renderCert C = b) : renderCert D = b := by
  obtain ⟨C, rfl⟩ := hb
  rw [parseCert_renderCert C] at h
  rw [Except.ok.inj h]

end CertIO
end Compiler

#print axioms Compiler.CertIO.renderCert_injective
#print axioms Compiler.CertIO.parse_then_render
#check @Compiler.CertIO.renderCert_injective
