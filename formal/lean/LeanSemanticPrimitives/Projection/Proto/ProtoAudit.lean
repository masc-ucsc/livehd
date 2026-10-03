/-
# Axiom audit for the `Proto` namespace

The core audit (`Projection/Audit.lean`, reached by `.build-proj.sh`) covers
the PROVED path and deliberately does not reach `Proto/`.  But the B1 results
in `VariantTransport.lean` ARE proofs, and a proof nobody audits is a proof
nobody can rely on.  This file audits them, separately, so that promoting any
of them later is a matter of moving a line rather than discovering what it
rested on.

Run:  lean LeanSemanticPrimitives/Projection/Proto/ProtoAudit.lean

Expect `propext`, `Quot.sound`, `Classical.choice` only -- and NEVER `sorryAx`
or `ofReduceBool`.

WHAT IS AUDITED HERE IS B1 ONLY: a generic transport result plus its
instantiation at the real `hwSVar`.  It is NOT variant adequacy.  B2 (the
rewrite for `main`'s actual body), the canonical witness `main_agree_var`, and
B3 (making `specializeDesign_correct` interpreter-parametric) are not done, and
nothing in this file should be read as standing in for them.

`InterpreterVariant.lean` itself contains NO theorems -- its contents are
`partial def`s and checked side conditions, which the kernel cannot reduce and
which this audit therefore cannot cover.  That is why `ProtoVar.changed` is a
runtime check in `proto_probe --env0-ab` and not a hypothesis of anything here.
-/
import LeanSemanticPrimitives.Projection.Proto.VariantTransport

namespace Projection.ProtoVar

-- B1, the generic transport
#print axioms SEval_congr
#print axioms SEvalList_congr
#print axioms altBody_noMain

-- B1, the table facts
#print axioms funNames_replace
#print axioms idxOf_name
#print axioms sFn_replace
#print axioms sFn_mem

-- B1, `MainFreeFuns` discharged rather than assumed
#print axioms mainFreeB_sound
#print axioms mainFreeB_hwS
#print axioms MainFreeFuns_hwS

-- B1, instantiated at the REAL variant
#print axioms rewritten_name
#print axioms sFn_hwSVar

-- the corollary B2 will consume
#print axioms SEval_hwSVar_of_hwS
#print axioms SEvalList_hwSVar_of_hwS

end Projection.ProtoVar
