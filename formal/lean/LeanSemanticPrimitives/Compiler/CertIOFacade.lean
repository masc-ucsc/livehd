/-
The router's theorems (FP HISTORY): `Compiler.CertIORoute` tells DCERT1 (FP's codec,
`Compiler.CertIO.V1`) from the clock-aware codec (`Compiler.CertIO`, DCERT4) by magic. The
router cannot mis-route: `magics_are_exclusive` proves no buffer satisfies both magics; and
`parseAny_renderCert` that a rendered certificate comes back through it unchanged.

No driver reads through the router: every driver and proof tool admits with
`CertIO.readCertChecked` (DCERT4), and DCERT1 is refused (`CertIORoute` says why).
-/
import LeanSemanticPrimitives.Compiler.CertIORoute
import LeanSemanticPrimitives.Compiler.CertIOParity

namespace Compiler
namespace CertIO

-- `Format`, `detect`, `parseAny` and the admissions live in
-- `Compiler/CertIORoute.lean` (Mathlib-free, same namespace).

/-- No buffer is both. The magics differ in their last byte -- `'4'` (52)
against `'1'` (49) -- so routing on them is total and unambiguous. -/
theorem magics_are_exclusive (b : ByteArray) : ¬ (hasMagic b ∧ V1.hasMagic b) := by
  rintro ⟨h2, h1⟩
  simp only [hasMagic, V1.hasMagic, Bool.and_eq_true, decide_eq_true_eq,
             List.all_eq_true, List.mem_range] at h2 h1
  have e2 := h2.2 5 (by rw [show "DCERT4".toUTF8.size = 6 from rfl]; omega)
  have e1 := h1.2 5 (by rw [show "DCERT1".toUTF8.size = 6 from rfl]; omega)
  rw [show ("DCERT4".toUTF8[5]! : UInt8) = 52 from rfl] at e2
  rw [show ("DCERT1".toUTF8[5]! : UInt8) = 49 from rfl] at e1
  simp only [beq_iff_eq] at e1 e2
  rw [e1] at e2
  exact absurd e2 (by decide)

/-- Routing does not change what a certificate means: a rendered certificate comes
back through the facade exactly as `parseCert` returns it, tagged `v2`. -/
theorem parseAny_renderCert (D : DesignCert) :
    parseAny (renderCert D) = .ok (Format.v2, D) := by
  have hm : hasMagic (renderCert D) = true := hasMagic_render D
  simp only [parseAny, detect, hm, if_pos, parseCert_renderCert D, Except.map]

end CertIO
end Compiler

#print axioms Compiler.CertIO.magics_are_exclusive
#print axioms Compiler.CertIO.parseAny_renderCert
