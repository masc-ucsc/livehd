/-
# The axiom gate for the Mathlib-side certificate format theorems

The DCERT4 codec's theorems import Mathlib, so `.build-proj.sh` (Mathlib-free) does not build
them; `lake build LeanSemanticPrimitives.Compiler.CertIOAudit` does (the full test tier runs it,
`pass/lean/tools/tests/run_tier.sh full`). Their own modules only PRINT their axioms
(`#print axioms`), which passes whatever it prints. Each line here is `#audit_axioms`
(`Projection.AxiomGate`): it prints the same line and fails the build on any axiom outside
`propext`, `Classical.choice`, `Quot.sound` (a `sorry`, a `native_decide`, a declared axiom).

- `parseCert_renderCert` (`CertIORoundTrip`): the reader inverts the writer, for every
  certificate;
- `renderCert_injective`, `parse_then_render` (`CertIOParity`): equal bytes are the same
  certificate, what a byte comparison of the C++ and Lean writers may conclude
  (`pass/lean/Exporter/DesignCert/scripts/dcert_parity.py`);
- `clockName_utf8_inverse` (`CertIO`): a clock name comes back from its bytes;
- `magics_are_exclusive`, `parseAny_renderCert` (`CertIOFacade`): the DCERT1/DCERT4 router
  (history: no driver reads through it) cannot mis-route.
-/
import LeanSemanticPrimitives.Compiler.CertIOFacade
import LeanSemanticPrimitives.Projection.AxiomGate

#audit_axioms Compiler.CertIO.parseCert_renderCert
#audit_axioms Compiler.CertIO.renderCert_injective
#audit_axioms Compiler.CertIO.parse_then_render
#audit_axioms Compiler.CertIO.clockName_utf8_inverse
#audit_axioms Compiler.CertIO.magics_are_exclusive
#audit_axioms Compiler.CertIO.parseAny_renderCert
