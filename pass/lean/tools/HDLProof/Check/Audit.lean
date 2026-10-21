/-
  Axiom audit for the proof tools (HDLProof, pass/lean/tools).

  The same gate as `Projection/Audit.lean`: every theorem a checker's verdict
  rests on must use only `propext`, `Classical.choice` and `Quot.sound`.
  `#audit_axioms` (`Projection.AxiomGate`) prints what `#print axioms` prints
  and FAILS the build on anything else, so a `sorry` or a native axiom under
  an audited theorem stops the build. This is the last tools module the build
  script compiles; a new checker adds its soundness theorems here. -/

import LeanSemanticPrimitives.Projection.AxiomGate
import HDLProof.Sym.Refine
import HDLProof.Sym.Conj
import HDLProof.Sym.ConjT
import HDLProof.Theory.Progress
import HDLProof.Check.RefMapRef

namespace HDLProof

-- The symbolic evaluator, the wiring normal form it keeps uncut, the
-- LRAT-checked UNSAT, and the three checkers' reference-semantics theorems.
#audit_axioms Sym.symEval_sound
#audit_axioms Sym.eval_norm
#audit_axioms Sym.checkUnsat_sound
#audit_axioms Sym.runInvKOf_sound_ref
#audit_axioms Sym.runConjOf_sound_ref
#audit_axioms Sym.stateVarT_rel
#audit_axioms Sym.runConjTOf_sound_ref
#audit_axioms Sym.runRefKOf_sound_ref

-- Refinement theory: Kahn determinism of a confluent channel spec, runs that
-- refine it, horizontal and vertical composition, progress under fairness.
#audit_axioms Theory.Spec.chan_agree
#audit_axioms Theory.Exec.upTo_valid
#audit_axioms Theory.PipelineRefinement.exec_refines
#audit_axioms Theory.compose
#audit_axioms Theory.Exec.map_refines
#audit_axioms Theory.channel_progress
#audit_axioms Theory.helpful_bound

-- Refinement maps: the spec chain over a cycle's slots, and the check.
#audit_axioms Kit.rebuildS_rel
#audit_axioms Kit.ObsW.sym_sound
#audit_axioms Check.chainS_sound
#audit_axioms Check.runRefMapOf_sound
-- The spec's own facts: Moore and confluence of its channel events.
#audit_axioms Check.runSpecFactsOf_sound
-- A checked refinement as a run of the channel spec; two of them composed; progress.
#audit_axioms Check.execOf_refines
#audit_axioms Check.implStream_eq
#audit_axioms Check.refmap_compose
#audit_axioms Check.refmap_progress
#audit_axioms Check.compose_of_facts
#audit_axioms Check.progress_of_facts
#audit_axioms Check.rm_denote
#audit_axioms Check.checked_parts
#audit_axioms Check.specFacts_checked
#audit_axioms Check.start_of_facts
#audit_axioms Check.start_ref
#audit_axioms Check.refmap_compose_ref
#audit_axioms Check.refmap_progress_ref

end HDLProof
