/-
  Axiom audit for the Projection library.

  The same gate the manual path uses: every load-bearing theorem must rest on
  the three standard axioms and nothing else.  A `sorry` anywhere shows up here
  as `sorryAx`, and `native_decide` would show up as `Lean.ofReduceBool` -- this
  library must not use it, since these theorems are about all programs rather
  than about one fixed graph.

  This module is checked by CI shape, not by a human reading it: it is the last
  module the build script compiles, and it prints. -/

import LeanSemanticPrimitives.Projection.Encoding
import LeanSemanticPrimitives.Projection.ObjectLanguageSemantics
import LeanSemanticPrimitives.Projection.BTA
import LeanSemanticPrimitives.Projection.PartialEvaluatorCorrect
import LeanSemanticPrimitives.Projection.SecondProjection
import LeanSemanticPrimitives.Projection.SimulatorContract
import LeanSemanticPrimitives.Projection.OperatorBridge

namespace Projection

#print axioms evalFuel_sound
#print axioms evalFuelList_sound_of
#print axioms decTerm_encTerm
#print axioms decTerms_encTerms
#print axioms decAlts_encAlts
#print axioms decProgram_encProgram
#print axioms encTerm_decTerm
#print axioms encProgram_decProgram
#print axioms encEnv_decEnv
#print axioms encTerm_inj
#print axioms encProgram_inj
#print axioms bta_sound
#print axioms bta_erases
#print axioms erase_annT
#print axioms eraseProgram_fn
#print axioms findAlt_eraseAlts
#print axioms Compat_shift
#print axioms Compat_fields_dyn
#print axioms Compat_fields_stat
#print axioms Compat_stat_lookup
#print axioms Compat_dyn_lookup
#print axioms buildEnv_Compat
#print axioms mixTerm_complete
#print axioms mixAlts_ok
#print axioms mixUArgs_ok
#print axioms mixTerms_ok
#print axioms specOK_all
#print axioms mixDriver_sound
#print axioms mixTerm_sound
#print axioms mixAlts_sound
#print axioms mixUArgs_sound
#print axioms splitArgs_sound
#print axioms specSound_all
#print axioms mixDriver_complete
#print axioms mixDriver_correct
#print axioms mixDriver_iff
#print axioms secondProjection_correct
#print axioms secondProjection_correct_call
#print axioms Eval_entry

-- the common simulator contract (SIMULATOR_PLAN milestone 0 / 5)
#print axioms step_agree
#print axioms stepTrace_correct
#print axioms trace_agree
#print axioms evalFuel_mono
#print axioms evalFuel_complete
#print axioms generateFrom_spec
#print axioms Val.eq_of_beq
#print axioms indexOfReq_spec
#print axioms wrapLets_eval
#print axioms allStatic_forall₂
#print axioms toCode_forall₂
#print axioms Compat_allStat

-- the hardware domain as object data (SIMULATOR_PLAN milestone 1)
#print axioms decListG_encListG
#print axioms decArr_encArr
#print axioms decOp_encOp
#print axioms decSource_encSource
#print axioms decNode_encNode
#print axioms decFlop_encFlop
#print axioms decDesign_encDesign
#print axioms encDesign_inj
#print axioms decBV_encBV
#print axioms StateRel_encState
#print axioms StateRel_memFree
#print axioms StateRel_functional
#print axioms ResultRel_encResult
#print axioms ResultRel_memFree
#print axioms ResultRel_functional
#print axioms interpretDesign_memFree

-- the object primitives against the pinned hardware semantics (milestone 2)
#print axioms prim_bvAnd
#print axioms prim_bvResize
#print axioms prim_bvUint
#print axioms evalOpCert_And
#print axioms evalOp_And_two
#print axioms srcFlopNext_eq

-- scope preservation: successful specialization emits no dangling references
#print axioms PValOK_Scoped
#print axioms Compat_Scoped
#print axioms mixTerms_scoped
#print axioms mixAlts_scoped
#print axioms mixUArgs_inlineEnv_scoped
#print axioms mixTerm_scoped

end Projection
