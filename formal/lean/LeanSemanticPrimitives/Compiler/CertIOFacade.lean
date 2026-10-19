/-
One import point for both certificate codecs, with NEITHER policy widened.

`Compiler.CertIO` is the DCERT2 codec and the checked-compiler runtime that D4
contributes. `Compiler.CertIO.V1` is FP's DCERT1 codec and its `asyncOK`
transport constraint. They judge different things and they admit on different
grounds, so this module routes by MAGIC and then calls each side's own
admission -- it does not define a third policy, and it does not make either
stricter or looser than the branch it came from.

Specifically:

  * `admitV2` is exactly `readCertChecked`: parse, then `certWfError`.
  * `admitV1` is exactly FP's: parse, then `asyncOK`. It does NOT also apply
    `certWfError`. That predicate is about clock ordinals and slot bounds on
    the clock-aware record; a DCERT1 file parses to the default one-clock
    table, so applying it here would be imposing a check FP never ran, on
    fields the format never carried.

The router cannot mis-route: `magics_are_exclusive` proves no buffer satisfies
both magics.
-/
import LeanSemanticPrimitives.Compiler.CertIORoute
import LeanSemanticPrimitives.Compiler.CertIOParity

namespace Compiler
namespace CertIO

-- `Format`, `detect`, `parseAny` and the admissions live in
-- `Compiler/CertIORoute.lean` (Mathlib-free, same namespace).

/-- No buffer is both. The magics differ in their last byte -- `'2'` (50)
against `'1'` (49) -- so routing on them is total and unambiguous. -/
theorem magics_are_exclusive (b : ByteArray) : ¬ (hasMagic b ∧ V1.hasMagic b) := by
  rintro ⟨h2, h1⟩
  simp only [hasMagic, V1.hasMagic, Bool.and_eq_true, decide_eq_true_eq,
             List.all_eq_true, List.mem_range] at h2 h1
  have e2 := h2.2 5 (by rw [show "DCERT2".toUTF8.size = 6 from rfl]; omega)
  have e1 := h1.2 5 (by rw [show "DCERT1".toUTF8.size = 6 from rfl]; omega)
  rw [show ("DCERT2".toUTF8[5]! : UInt8) = 50 from rfl] at e2
  rw [show ("DCERT1".toUTF8[5]! : UInt8) = 49 from rfl] at e1
  simp only [beq_iff_eq] at e1 e2
  rw [e1] at e2
  exact absurd e2 (by decide)

/-- Routing does not change what DCERT2 means: a rendered certificate comes
back through the facade exactly as `parseCert` returns it, tagged `v2`. -/
theorem parseAny_renderCert (D : DesignCert) :
    parseAny (renderCert D) = .ok (Format.v2, D) := by
  have hm : hasMagic (renderCert D) = true := hasMagic_render D
  simp only [parseAny, detect, hm, if_pos, parseCert_renderCert D, Except.map]

end CertIO
end Compiler

#print axioms Compiler.CertIO.magics_are_exclusive
#print axioms Compiler.CertIO.parseAny_renderCert
