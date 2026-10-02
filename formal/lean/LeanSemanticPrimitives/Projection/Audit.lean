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
-- Phase 4's theorems live here; auditing them needs the module in scope
import LeanSemanticPrimitives.Projection.HardwareInterpreter
-- Phase 4 (reordered from Phase 8): the generic surface-semantics layer
import LeanSemanticPrimitives.Projection.SurfaceSemantics
import LeanSemanticPrimitives.Projection.HardwareAdequacy
-- Phase 4 item 3: the first projection, on hardware
import LeanSemanticPrimitives.Projection.ProjectionCorrect
-- Phase 5: the residual-fragment checker and the simulator bundle
import LeanSemanticPrimitives.Projection.ResidualFragment
import LeanSemanticPrimitives.Projection.ProjectedStep

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
#print axioms mixTerms_ok
#print axioms specOK_all
#print axioms mixDriver_sound
#print axioms mixTerm_sound
#print axioms mixAlts_sound
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
-- Phase 3 batch 1: Op_Or, Op_SRA, Op_GetMask
#print axioms prim_bvSra
#print axioms prim_bvGetMask
#print axioms evalOp_Or_fold
#print axioms evalOp_Or_two
#print axioms evalOp_SRA
#print axioms evalOp_GetMask
#print axioms evalOpCert_Or
#print axioms evalOpCert_SRA
#print axioms evalOpCert_GetMask
-- Phase 3 batch 2: Op_Xor, Op_Not, Op_Sum
#print axioms evalOp_Xor_fold
#print axioms evalOp_Xor_two
#print axioms evalOp_Not
#print axioms evalOp_Sum
#print axioms evalOpCert_Xor
#print axioms evalOpCert_Not
#print axioms evalOpCert_Sum
-- Phase 3 batch 3: Op_EQ, Op_Ror, Op_MuxBool, Op_MuxN
#print axioms evalOp_EQ_nil
#print axioms evalOp_EQ_cons
#print axioms evalOp_Ror
#print axioms evalOp_MuxBool
#print axioms evalOp_MuxN_nil
#print axioms evalOp_MuxN_cons
#print axioms evalOpCert_EQ
#print axioms evalOpCert_Ror
#print axioms evalOpCert_MuxBool
#print axioms evalOpCert_MuxN
-- Phase 3 batch 4: the comparisons and Op_Sext
-- the local overlay is definitionally inert
#print axioms bv_shl_step_unfolds
#print axioms evalOp_SHL_unchanged_by_overlay
#print axioms denoteOp_SHL_unchanged_by_overlay
#print axioms prim_bvSint
#print axioms evalOp_ULT
#print axioms evalOp_UGT
#print axioms evalOp_SLT
#print axioms evalOp_SGT
#print axioms evalOp_Sext
#print axioms evalOpCert_ULT
#print axioms evalOpCert_UGT
#print axioms evalOpCert_SLT
#print axioms evalOpCert_SGT
#print axioms evalOpCert_Sext
-- Phase 3, last operator: Op_SHL
#print axioms prim_bvShl
-- Phase 4 item 1: the support predicate
#print axioms Hw.SupportCheck.tiny_supported
#print axioms Hw.SupportCheck.seq_supported
#print axioms Hw.SupportedByProjection.conservative
#print axioms evalOp_SHL_nil
#print axioms evalOp_SHL_cons
#print axioms evalOpCert_SHL
#print axioms srcFlopNext_eq
-- multi-clock: the shared semantics, and conservativity
#print axioms Compiler.interpretDesign_allEdges
#print axioms Compiler.srcFlopNext_fires
#print axioms Compiler.srcMemNext_fires
#print axioms Compiler.allEdges_size
#print axioms Compiler.fires_allEdges
#print axioms Acceptance.tiny_conservative
#print axioms Acceptance.seq_conservative
#print axioms decClock_encClock
#print axioms decStr_encStr
#print axioms EdgesRel_encEdges

-- scope preservation: successful specialization emits no dangling references
#print axioms PValOK_Scoped
#print axioms Compat_Scoped
#print axioms mixTerms_scoped
#print axioms mixAlts_scoped
#print axioms mixTerm_scoped
#print axioms buildEnv_scoped
#print axioms mixFun_scoped
#print axioms generateFrom_scoped
#print axioms mixDriver_scoped

-- preparation: the switch's argument transfer, verified before it is wired
#print axioms prepare_scoped
#print axioms prepare_ok
#print axioms prepare_toPRes
#print axioms prepare_hot_path
#print axioms ScopedLets_append
#print axioms EvalLets_append
#print axioms mixPArgs_scoped
#print axioms Compat_after_EvalLets
#print axioms mixPArgs_ok
#print axioms wrapLets_peel
#print axioms prepare_peel
#print axioms PValOK_of_Scoped
#print axioms mixPArgs_sound
#print axioms peelHd_ok
#print axioms peelTl_ok
#print axioms peelIsNil_ok
#print axioms primStruct_ok
#print axioms prepare_total_binds
#print axioms PValOK_EvalLets_inv
#print axioms prepare_cons_split
#print axioms prepare_cons_join_right
#print axioms prepare_cons_join_left
#print axioms peelHd_run_inv
#print axioms peelTl_run_inv
#print axioms peelIsNil_run_inv
#print axioms mixTerms_sound_all
#print axioms primStruct_sound

-- Surface semantics (generic, hardware-independent).  `SEval_sound` transfers a
-- named-syntax derivation to the resolved de Bruijn program; `Eval_det` is what
-- turns such a transfer into the forward direction of an adequacy `iff`.
#print axioms Surface.idxOf_slookup
#print axioms Surface.resolveFuns_get
#print axioms Surface.resolveAlts_findAlt
#print axioms Surface.resolveProgram_funs
#print axioms Surface.SEval_sound
#print axioms Surface.SEvalList_sound
#print axioms Surface.Eval_det
#print axioms Surface.EvalList_det
#print axioms Surface.SEval_ite_of_bool
#print axioms Surface.SEval_switch_of_tag
#print axioms Surface.SEval_hd
#print axioms Surface.SEval_tl
#print axioms Surface.SEval_consP
#print axioms Surface.SEval_isNil_nil
#print axioms Surface.SEval_isNil_cons
#print axioms Surface.SEval_call1
#print axioms Surface.SEval_call2
#print axioms Surface.SEval_call3
#print axioms Surface.SEval_call4
#print axioms Surface.resolveProgram_entry
#print axioms Surface.resolveList_lits
#print axioms Surface.SEvalList_lits
#print axioms Surface.SEval_entry

-- Canonicality: an encoded value is the ONLY value its decoder accepts.  This
-- is strictly stronger than the round trip and strictly stronger than
-- functionality, and it is what makes `IHwAdequate` statable as an `iff`.
#print axioms canonical_listG
#print axioms canonical_arr
#print axioms field1_canonical
#print axioms field2_canonical
#print axioms canonical_BV
#print axioms canonical_BVs
#print axioms InputRel_canonical
#print axioms StateRel_canonical
#print axioms ResultRel_canonical

-- The concrete hardware bridge.
#print axioms Hw.hwResolved_isOk
#print axioms Hw.hwResolved_ok
#print axioms Hw.hwP_resolves
#print axioms Hw.hw_transfer
#print axioms Hw.hw_entry
#print axioms Hw.resultRel_iff_eq
#print axioms Hw.resultRel_interpret_iff

-- Adequacy helper group 1: slot reads and source values.
#print axioms Hw.nthD_agree
#print axioms Hw.nthD_arr
#print axioms Hw.nz_agree
#print axioms Hw.srcVal_input
#print axioms Hw.srcVal_const
#print axioms Hw.srcVal_flopQ
#print axioms Hw.srcVal_flopQAsync
#print axioms Hw.srcVal_agree

-- Adequacy helper group 2: the source environment.
#print axioms Hw.lenL_agree
#print axioms Hw.slot_agree
#print axioms Hw.mkSources_agree
#print axioms Hw.source_env_agree

-- Adequacy helper group 3: the operator layer.  One lemma per pinned operator,
-- each quoting an `OperatorBridge` equation rather than re-deriving it.
#print axioms Hw.slot_read
#print axioms Hw.two_pow_pos
#print axioms Hw.bv_uint_nonneg
#print axioms Hw.foldAnd_agree
#print axioms Hw.opAnd_agree
#print axioms Hw.foldOr_agree
#print axioms Hw.opOr_agree
#print axioms Hw.foldXor_agree
#print axioms Hw.opXor_agree
#print axioms Hw.anyNz_agree
#print axioms Hw.opRor_agree
#print axioms Hw.eqAll_agree
#print axioms Hw.opEq_agree
#print axioms Hw.sumAdds_agree
#print axioms Hw.sumSubs_agree
#print axioms Hw.opSum_agree
#print axioms Hw.foldShl_agree
#print axioms Hw.opShl_agree
#print axioms Hw.muxPick_agree
#print axioms Hw.opMuxN_agree
#print axioms Hw.opNot_agree
#print axioms Hw.opSra_agree
#print axioms Hw.opGetMask_agree
#print axioms Hw.opMuxBool_agree
#print axioms Hw.opSext_agree
#print axioms Hw.cmpLt_agree
#print axioms Hw.opCmp_agree
#print axioms Hw.opULT_agree
#print axioms Hw.opUGT_agree
#print axioms Hw.opSLT_agree
#print axioms Hw.opSGT_agree
#print axioms Hw.list_len_one
#print axioms Hw.list_len_two
#print axioms Hw.list_len_three
#print axioms Hw.applyOp_agree

-- Adequacy helper group 4: the node fold, against `evalGraphG` itself.
#print axioms Hw.evalGraphG_rec
#print axioms Hw.evalOpCert_of_supported
#print axioms Hw.prefixVals_slotVals
#print axioms Hw.prefixVals_sources
#print axioms Hw.nodeVal_rec
#print axioms Hw.evalNode_agree
#print axioms Hw.evalNodes_agree

-- Adequacy helper group 5: outputs.  Order and width resize, exactly.
#print axioms Hw.mkOutputs_agree

-- Adequacy helper group 6: flop updates.  Reset polarity, async-vs-sync edge
-- priority, then enable/din/old-state hold, in that order.
#print axioms Hw.firesAt_agree
#print axioms Hw.srcFlopNext_named
#print axioms Hw.rstActive_agree
#print axioms Hw.capture_agree
#print axioms Hw.flopNext_agree
#print axioms Hw.flopNextsFrom_getElem
#print axioms Hw.flopNexts_agree

-- Adequacy helper group 7: one cycle, and GROUP 8: the adequacy `iff` itself.
#print axioms Hw.flopNextsFrom_mapIdx
#print axioms Hw.main_agree
#print axioms Hw.IHwAdequate_proved
#print axioms Hw.IHwAdequacyGoal_proved
#print axioms Hw.IHwAdequate_of_runtimeWF
#print axioms Hw.SupportCheck.not_arity_bites

-- Phase 4 item 3: `projectDesign_correct`, and item 4: the acceptance guards
-- restated as theorems.
#print axioms Hw.mixDriver_entry
#print axioms Hw.hwA_ok
#print axioms Hw.hwAP_erases
#print axioms Hw.hwAP_entry
#print axioms Hw.specializeDesign_correct
#print axioms Hw.projectDesign_correct
#print axioms Hw.projectDesign_correct_body
#print axioms Hw.GuardCorollary.tiny_cycle
#print axioms Hw.GuardCorollary.tiny_cycle_value
#print axioms Hw.GuardCorollary.seq_cycle
#print axioms Hw.GuardCorollary.seq_cycle_value

-- Phase 5 increment 1: the checker's guarantee.  `checkResidual_sound` is the
-- one that matters -- an accepted residual never answers `.outOfFuel` at the
-- bound the checker computed for it.
#print axioms Hw.height_pos
#print axioms Hw.fragAltsB_find
#print axioms Hw.heightAlts_find
#print axioms Hw.fragB_no_outOfFuel
#print axioms Hw.fragListB_no_outOfFuel
#print axioms Hw.fragAltsB_no_outOfFuel
#print axioms Hw.checkResidual_entry
#print axioms Hw.checkResidual_sound
#print axioms Hw.mkSim_proj_chk
#print axioms Hw.runProjected_never_boundExceeded

-- Phase 5 increment 2: the runtime check, both halves of `runProjected`, and
-- the first `StepCorrect` instance on this branch.
#print axioms Hw.runtimeOK_sound
#print axioms Hw.fragB_evalFuel
#print axioms Hw.fragListB_evalFuel
#print axioms Hw.checkResidual_complete
#print axioms Hw.runProjected_correct
#print axioms Hw.runProjected_success
#print axioms Hw.stepOf_correct
#print axioms Hw.stepOf_succeeds
#print axioms Hw.stepTrace_projected
#print axioms Hw.FixtureStep.tiny_one_cycle
#print axioms Hw.FixtureStep.seq_one_cycle
#print axioms Hw.FixtureStep.tiny_two_cycles
#print axioms Hw.FixtureStep.seq_two_cycles

end Projection
