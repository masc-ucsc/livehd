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
import LeanSemanticPrimitives.Projection.Proto.VariantAdequacy

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

/- B2's PREREQUISITE: facts about the ACTUAL rewritten body, visible to the
kernel.  `InterpreterVariant.lean`'s traversals are `partial` and therefore
opaque -- they have no computational equations, so no `rfl`/`simp`/`decide`
can reach them.  `RewriteTotal.lean` supplies structurally recursive versions
and these three reduce.

That the two agree is REGRESSION EVIDENCE from `proto_probe --rewrite-agree`
(exact `BEq` on the resulting body, plus every report field, on `hwS`'s real
`main` and on the negative fixture) -- a run, NOT a theorem, because there is
nothing to prove about an opaque definition. -/
#print axioms rewrittenT_applied
#print axioms env0_bound_once_before
#print axioms env0_binder_gone_after

/- B1 instantiated at the PROOF TARGET.  `hwSVar` (InterpreterVariant) is built
from the `partial` `goInline`; `hwSVarT` (RewriteTotal) from the TOTAL
`rewrittenT`.  They are DIFFERENT DEFINITIONS, and `--rewrite-agree` showing
they agree on this input is evidence, not a transfer -- an opaque definition
has no equations to transfer to.  Everything downstream of B2 must be stated
about `hwSVarT`. -/
#print axioms funNames_mapF
#print axioms sFn_mapF
#print axioms sFn_hwSVarT
#print axioms SEval_hwSVarT_of_hwS
#print axioms SEvalList_hwSVarT_of_hwS
#print axioms hwSVarT_rewritten
#print axioms hwS_not_rewritten

/- B2: the canonical witness and adequacy for the TOTAL variant.
`main_agree_varT` mirrors `HardwareAdequacy.main_agree` step for step, with the
`env0` binding removed and `mkSources`'s derivation handed directly to
`evalNodes_agree`.  `IHwAdequate_varT` then follows `IHwAdequate_proved`'s
pattern -- one constructed evaluation, `Eval_det` forward and
`ResultRel_canonical` back -- so no surface completeness theorem is involved.

STILL OUTSTANDING: B3.  `specializeDesign_correct` names `hwAP` literally, so
`IHwAdequate_varT` does not yet compose into a statement about a residual. -/
#print axioms mainFunVarT_params
#print axioms mainBodyVarT_mainFree
#print axioms main_agree_varT
#print axioms hwResolvedVarT_isOk
#print axioms hwResolvedVarT_ok
#print axioms hwPVarT_resolves
#print axioms hw_entry_varT
#print axioms IHwAdequate_varT

/- B3's PREREQUISITES, promoted from `#guard`s to theorems because B3 consumes
them and a check may not be cited as a discharged premise.  `hwAPVarT_erases`
goes through the generic `bta_erases` rather than kernel reduction, the same
route `hwAP_erases` takes. -/
#print axioms hwAVarT_isOk
#print axioms hwAVarT_ok
#print axioms hwAPVarT_wf
#print axioms hwAPVarT_erases
#print axioms hwAPVarT_entry_params
#print axioms hwAPVarT_entry

/- B3: the residual theorem for the total variant.  `specialize_correct_of` is
the composition with the interpreter as a PARAMETER, extracted in
`ProjectionCorrect.lean`; `specializeDesign_correct` is its `hwAP` instance and
`specializeDesign_varT_correct` its `hwAPVarT` one.  Plus a SEQUENTIAL fixture,
so the flop-commit path is exercised and not only the combinational one.

NOT COVERED by any of this: executable `evalFuel`/`runProjected` behaviour and
multi-cycle trace correctness.  `SimWF`, `ProjectedSimulator` and `stepOf` are
written against `projectDesign`, which hardcodes `hwAP`; their theorems do NOT
automatically carry to `hwAPVarT`.  That is the next link, and it is not done. -/
#print axioms specializeDesign_varT_correct
#print axioms seq_cycle_varT

/- The EXECUTABLE link.  `SimSound` (ResidualFragment) is `SimWF` with the HOW
abstracted away; `SimWF.toSimSound` makes the original an instance, so
`runProjected_correct`, `runProjected_success`, `stepOf_correct`,
`stepOf_succeeds` and `stepTrace_projected` are the SAME theorems restated over
the contract -- reused, not duplicated.  `simSound_varT` is the other instance.

`seq_cycle_varT` above checks `Eval hwPVarT`, the INTERPRETER.  The fixtures
below check a SPECIALIZED RESIDUAL through `stepOf`, which is a different
claim, and cover reset / enabled / held plus a three-cycle trace.  Both halves:
successful results are correct, AND valid runs succeed at the checker bound. -/
#print axioms simSound_varT
#print axioms seq_varT_step
#print axioms seq_varT_reset
#print axioms seq_varT_enabled
#print axioms seq_varT_held
#print axioms seq_varT_trace

end Projection.ProtoVar
