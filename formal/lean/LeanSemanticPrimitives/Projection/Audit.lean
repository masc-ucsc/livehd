/-
  Axiom audit for the Projection library.

  The same gate the manual path uses: every load-bearing theorem must rest on
  the three standard axioms and nothing else.  A `sorry` anywhere shows up here
  as `sorryAx`, and a `native_decide` as an axiom with a `_native` name
  component (Lean 4.31 mints one per call; `Lean.ofReduceBool` is deprecated)
  -- this library must not use it, since these theorems are about all programs
  rather than about one fixed graph.

  ENFORCED, not merely printed.  Each line is `#audit_axioms`
  (`Projection.AxiomGate`), which prints exactly what `#print axioms` prints and
  then FAILS the build on any axiom outside `propext`, `Classical.choice`,
  `Quot.sound`.  This is the last module the build script compiles, so a
  `sorry` or a native axiom anywhere under an audited theorem stops the build
  instead of scrolling past in its output.  The gate's own negative controls
  are `pass/lean/specialization/tests/axiom_gate_controls.lean`. -/

import LeanSemanticPrimitives.Projection.AxiomGate
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
-- Phase 6: the DCERT1 loader and the per-field support report
import LeanSemanticPrimitives.Projection.CertLoad

namespace Projection

#audit_axioms evalFuel_sound
#audit_axioms evalFuelList_sound_of
#audit_axioms decTerm_encTerm
#audit_axioms decTerms_encTerms
#audit_axioms decAlts_encAlts
#audit_axioms decProgram_encProgram
#audit_axioms encTerm_decTerm
#audit_axioms encProgram_decProgram
#audit_axioms encEnv_decEnv
#audit_axioms encTerm_inj
#audit_axioms encProgram_inj
#audit_axioms bta_sound
#audit_axioms bta_erases
#audit_axioms erase_annT
#audit_axioms eraseProgram_fn
#audit_axioms findAlt_eraseAlts
-- The `@[csimp]` fast path: these two equalities are what justifies the
-- COMPILED `PVal.shift` / `PEnv.shiftBy`, so they are load-bearing for every
-- native specialization run even though no other theorem mentions them.
#audit_axioms PVal.shift_eq_shiftFast
#audit_axioms PEnv.shiftBy_eq_shiftByFast
-- ... and S3: the compiled mixDriver is S3.mixDriver by this equality.
#audit_axioms S3Correct.mixDriver_eq_S3
#audit_axioms S3Correct.mixDriver_eq
#audit_axioms S3Correct.mixTerm_sim
#audit_axioms S3Correct.mixPArgs_sim
#audit_axioms S3Correct.nthSummaryEnv_rep
#audit_axioms S3Correct.prepare_rep
#audit_axioms S3Correct.toCodeAt_rep
#audit_axioms Compat_shift
#audit_axioms Compat_fields_dyn
#audit_axioms Compat_fields_stat
#audit_axioms Compat_stat_lookup
#audit_axioms Compat_dyn_lookup
#audit_axioms buildEnv_Compat
#audit_axioms mixTerm_complete
#audit_axioms mixAlts_ok
#audit_axioms mixTerms_ok
#audit_axioms specOK_all
#audit_axioms mixDriver_sound
#audit_axioms mixTerm_sound
#audit_axioms mixAlts_sound
#audit_axioms splitArgs_sound
#audit_axioms specSound_all
#audit_axioms mixDriver_complete
#audit_axioms mixDriver_correct
#audit_axioms mixDriver_iff
#audit_axioms secondProjection_correct
#audit_axioms secondProjection_correct_call
#audit_axioms Eval_entry

-- the common simulator contract (SIMULATOR_PLAN milestone 0 / 5)
#audit_axioms step_agree
#audit_axioms stepTrace_correct
#audit_axioms trace_agree
#audit_axioms evalFuel_mono
#audit_axioms evalFuel_complete
#audit_axioms generateFrom_spec
#audit_axioms Val.eq_of_beq
#audit_axioms indexOfReq_spec
#audit_axioms wrapLets_eval
#audit_axioms allStatic_forall₂
#audit_axioms toCode_forall₂
#audit_axioms Compat_allStat

-- the hardware domain as object data (SIMULATOR_PLAN milestone 1)
#audit_axioms decListG_encListG
#audit_axioms decArr_encArr
#audit_axioms decOp_encOp
#audit_axioms decSource_encSource
#audit_axioms decNode_encNode
#audit_axioms decFlop_encFlop
#audit_axioms decDesign_encDesign
#audit_axioms encDesign_inj
#audit_axioms decBV_encBV
#audit_axioms StateRel_encState
#audit_axioms StateRel_memFree
#audit_axioms StateRel_functional
#audit_axioms ResultRel_encResult
#audit_axioms ResultRel_memFree
#audit_axioms ResultRel_functional
#audit_axioms interpretDesign_memFree

-- the object primitives against the pinned hardware semantics (milestone 2)
#audit_axioms prim_bvAnd
#audit_axioms prim_bvResize
#audit_axioms prim_bvUint
#audit_axioms evalOpCert_And
#audit_axioms evalOp_And_two
-- Phase 3 batch 1: Op_Or, Op_SRA, Op_GetMask
#audit_axioms prim_bvSra
#audit_axioms prim_bvGetMask
#audit_axioms evalOp_Or_fold
#audit_axioms evalOp_Or_two
#audit_axioms evalOp_SRA
#audit_axioms evalOp_GetMask
#audit_axioms evalOpCert_Or
#audit_axioms evalOpCert_SRA
#audit_axioms evalOpCert_GetMask
-- Phase 3 batch 2: Op_Xor, Op_Not, Op_Sum
#audit_axioms evalOp_Xor_fold
#audit_axioms evalOp_Xor_two
#audit_axioms evalOp_Not
#audit_axioms evalOp_Sum
#audit_axioms evalOpCert_Xor
#audit_axioms evalOpCert_Not
#audit_axioms evalOpCert_Sum
-- Phase 3 batch 3: Op_EQ, Op_Ror, Op_MuxBool, Op_MuxN
#audit_axioms evalOp_EQ_nil
#audit_axioms evalOp_EQ_cons
#audit_axioms evalOp_Ror
#audit_axioms evalOp_MuxBool
#audit_axioms evalOp_MuxN_nil
#audit_axioms evalOp_MuxN_cons
#audit_axioms evalOpCert_EQ
#audit_axioms evalOpCert_Ror
#audit_axioms evalOpCert_MuxBool
#audit_axioms evalOpCert_MuxN
-- Phase 3 batch 4: the comparisons and Op_Sext
-- the local overlay is definitionally inert
#audit_axioms bv_shl_step_unfolds
#audit_axioms evalOp_SHL_unchanged_by_overlay
#audit_axioms denoteOp_SHL_unchanged_by_overlay
#audit_axioms prim_bvSint
#audit_axioms evalOp_ULT
#audit_axioms evalOp_UGT
#audit_axioms evalOp_SLT
#audit_axioms evalOp_SGT
#audit_axioms evalOp_Sext
#audit_axioms evalOpCert_ULT
#audit_axioms evalOpCert_UGT
#audit_axioms evalOpCert_SLT
#audit_axioms evalOpCert_SGT
#audit_axioms evalOpCert_Sext
-- Phase 3, last operator: Op_SHL
#audit_axioms prim_bvShl
-- Phase 4 item 1: the support predicate
#audit_axioms Hw.SupportCheck.tiny_supported
#audit_axioms Hw.SupportCheck.seq_supported
#audit_axioms Hw.SupportedByProjection.conservative
#audit_axioms evalOp_SHL_nil
#audit_axioms evalOp_SHL_cons
#audit_axioms evalOpCert_SHL
#audit_axioms srcFlopNext_eq
-- multi-clock: the shared semantics, and conservativity
#audit_axioms Compiler.interpretDesign_allEdges
#audit_axioms Compiler.srcFlopNext_fires
#audit_axioms Compiler.srcMemNext_fires
#audit_axioms Compiler.allEdges_size
#audit_axioms Compiler.fires_allEdges
#audit_axioms Acceptance.tiny_conservative
#audit_axioms Acceptance.seq_conservative
#audit_axioms decClock_encClock
#audit_axioms decStr_encStr
#audit_axioms EdgesRel_encEdges

-- scope preservation: successful specialization emits no dangling references
#audit_axioms PValOK_Scoped
#audit_axioms Compat_Scoped
#audit_axioms mixTerms_scoped
#audit_axioms mixAlts_scoped
#audit_axioms mixTerm_scoped
#audit_axioms buildEnv_scoped
#audit_axioms mixFun_scoped
#audit_axioms generateFrom_scoped
#audit_axioms mixDriver_scoped

-- preparation: the switch's argument transfer, verified before it is wired
#audit_axioms prepare_scoped
#audit_axioms prepare_ok
#audit_axioms prepare_toPRes
#audit_axioms prepare_hot_path
#audit_axioms ScopedLets_append
#audit_axioms EvalLets_append
#audit_axioms mixPArgs_scoped
#audit_axioms Compat_after_EvalLets
#audit_axioms mixPArgs_ok
#audit_axioms wrapLets_peel
#audit_axioms prepare_peel
#audit_axioms PValOK_of_Scoped
#audit_axioms mixPArgs_sound
#audit_axioms peelHd_ok
#audit_axioms peelTl_ok
#audit_axioms peelIsNil_ok
#audit_axioms primStruct_ok
#audit_axioms prepare_total_binds
#audit_axioms PValOK_EvalLets_inv
#audit_axioms prepare_cons_split
#audit_axioms prepare_cons_join_right
#audit_axioms prepare_cons_join_left
#audit_axioms peelHd_run_inv
#audit_axioms peelTl_run_inv
#audit_axioms peelIsNil_run_inv
#audit_axioms mixTerms_sound_all
#audit_axioms primStruct_sound

-- Surface semantics (generic, hardware-independent).  `SEval_sound` transfers a
-- named-syntax derivation to the resolved de Bruijn program; `Eval_det` is what
-- turns such a transfer into the forward direction of an adequacy `iff`.
#audit_axioms Surface.idxOf_slookup
#audit_axioms Surface.resolveFuns_get
#audit_axioms Surface.resolveAlts_findAlt
#audit_axioms Surface.resolveProgram_funs
#audit_axioms Surface.SEval_sound
#audit_axioms Surface.SEvalList_sound
#audit_axioms Surface.Eval_det
#audit_axioms Surface.EvalList_det
#audit_axioms Surface.SEval_ite_of_bool
#audit_axioms Surface.SEval_switch_of_tag
#audit_axioms Surface.SEval_hd
#audit_axioms Surface.SEval_tl
#audit_axioms Surface.SEval_consP
#audit_axioms Surface.SEval_isNil_nil
#audit_axioms Surface.SEval_isNil_cons
#audit_axioms Surface.SEval_call1
#audit_axioms Surface.SEval_call2
#audit_axioms Surface.SEval_call3
#audit_axioms Surface.SEval_call4
#audit_axioms Surface.resolveProgram_entry
#audit_axioms Surface.resolveList_lits
#audit_axioms Surface.SEvalList_lits
#audit_axioms Surface.SEval_entry

-- Canonicality: an encoded value is the ONLY value its decoder accepts.  This
-- is strictly stronger than the round trip and strictly stronger than
-- functionality, and it is what makes `IHwAdequate` statable as an `iff`.
#audit_axioms canonical_listG
#audit_axioms canonical_arr
#audit_axioms field1_canonical
#audit_axioms field2_canonical
#audit_axioms canonical_BV
#audit_axioms canonical_BVs
#audit_axioms InputRel_canonical
#audit_axioms StateRel_canonical
#audit_axioms ResultRel_canonical

-- The concrete hardware bridge.
#audit_axioms Hw.hwResolved_isOk
#audit_axioms Hw.hwResolved_ok
#audit_axioms Hw.hwP_resolves
#audit_axioms Hw.hw_transfer
#audit_axioms Hw.hw_entry
#audit_axioms Hw.resultRel_iff_eq
#audit_axioms Hw.resultRel_interpret_iff

-- Adequacy helper group 1: slot reads and source values.
#audit_axioms Hw.nthD_agree
#audit_axioms Hw.nthD_arr
#audit_axioms Hw.nz_agree
#audit_axioms Hw.srcVal_input
#audit_axioms Hw.srcVal_const
#audit_axioms Hw.srcVal_flopQ
#audit_axioms Hw.srcVal_flopQAsync
#audit_axioms Hw.srcVal_agree

-- Adequacy helper group 2: the source environment.
#audit_axioms Hw.lenL_agree
#audit_axioms Hw.slot_agree
#audit_axioms Hw.mkSources_agree
#audit_axioms Hw.source_env_agree

-- Adequacy helper group 3: the operator layer.  One lemma per pinned operator,
-- each quoting an `OperatorBridge` equation rather than re-deriving it.
#audit_axioms Hw.slot_read
#audit_axioms Hw.two_pow_pos
#audit_axioms Hw.bv_uint_nonneg
#audit_axioms Hw.foldAnd_agree
#audit_axioms Hw.opAnd_agree
#audit_axioms Hw.foldOr_agree
#audit_axioms Hw.opOr_agree
#audit_axioms Hw.foldXor_agree
#audit_axioms Hw.opXor_agree
#audit_axioms Hw.anyNz_agree
#audit_axioms Hw.opRor_agree
#audit_axioms Hw.eqAll_agree
#audit_axioms Hw.opEq_agree
#audit_axioms Hw.sumAdds_agree
#audit_axioms Hw.sumSubs_agree
#audit_axioms Hw.opSum_agree
#audit_axioms Hw.foldShl_agree
#audit_axioms Hw.opShl_agree
#audit_axioms Hw.muxPick_agree
#audit_axioms Hw.opMuxN_agree
#audit_axioms Hw.opNot_agree
#audit_axioms Hw.opSra_agree
#audit_axioms Hw.opGetMask_agree
#audit_axioms Hw.opMuxBool_agree
#audit_axioms Hw.opSext_agree
#audit_axioms Hw.cmpLt_agree
#audit_axioms Hw.opCmp_agree
#audit_axioms Hw.opULT_agree
#audit_axioms Hw.opUGT_agree
#audit_axioms Hw.opSLT_agree
#audit_axioms Hw.opSGT_agree
#audit_axioms Hw.list_len_one
#audit_axioms Hw.list_len_two
#audit_axioms Hw.list_len_three
-- D6 Tier 1: Mult, UDiv/SDiv, SetMask (the last through `SetMaskLoop`)
#audit_axioms Hw.multAll_agree
#audit_axioms Hw.opMult_agree
#audit_axioms Hw.opUDiv_agree
#audit_axioms Hw.truncDiv_obj
#audit_axioms Hw.opSDiv_agree
#audit_axioms SetMaskLoop.zip_fold
#audit_axioms SetMaskLoop.resize_set_bit_high
#audit_axioms SetMaskLoop.set_mask_eq_smRun
#audit_axioms Hw.setBitV_agree
#audit_axioms Hw.smLoop_agree
#audit_axioms Hw.opSetMask_agree
#audit_axioms Hw.applyOp_agree

-- Adequacy helper group 4: the node fold, against `evalGraphG` itself.
#audit_axioms Hw.evalGraphG_rec
#audit_axioms Hw.evalOpCert_of_supported
#audit_axioms Hw.prefixVals_slotVals
#audit_axioms Hw.prefixVals_sources
#audit_axioms Hw.nodeVal_rec
#audit_axioms Hw.evalNode_agree
#audit_axioms Hw.evalNodes_agree

-- Adequacy helper group 5: outputs.  Order and width resize, exactly.
#audit_axioms Hw.mkOutputs_agree

-- Adequacy helper group 6: flop updates.  Reset polarity, async-vs-sync edge
-- priority, then enable/din/old-state hold, in that order.
#audit_axioms Hw.firesAt_agree
#audit_axioms Hw.srcFlopNext_named
#audit_axioms Hw.rstActive_agree
#audit_axioms Hw.capture_agree
#audit_axioms Hw.flopNext_agree
#audit_axioms Hw.flopNextsFrom_getElem
#audit_axioms Hw.flopNexts_agree

-- Adequacy helper group 7: one cycle, and GROUP 8: the adequacy `iff` itself.
#audit_axioms Hw.flopNextsFrom_mapIdx
#audit_axioms Hw.main_agree
#audit_axioms Hw.IHwAdequate_proved
#audit_axioms Hw.IHwAdequacyGoal_proved
#audit_axioms Hw.IHwAdequate_of_runtimeWF
#audit_axioms Hw.SupportCheck.not_arity_bites

-- Phase 4 item 3: `projectDesign_correct`, and item 4: the acceptance guards
-- restated as theorems.
#audit_axioms Hw.mixDriver_entry
#audit_axioms Hw.hwA_ok
#audit_axioms Hw.hwAP_erases
#audit_axioms Hw.hwAP_entry
#audit_axioms Hw.specialize_correct_of
#audit_axioms Hw.specializeDesign_correct
#audit_axioms Hw.projectDesign_correct
#audit_axioms Hw.projectDesign_correct_body
#audit_axioms Hw.GuardCorollary.tiny_cycle
#audit_axioms Hw.GuardCorollary.tiny_cycle_value
#audit_axioms Hw.GuardCorollary.seq_cycle
#audit_axioms Hw.GuardCorollary.seq_cycle_value

-- Phase 5 increment 1: the checker's guarantee.  `checkResidual_sound` is the
-- one that matters -- an accepted residual never answers `.outOfFuel` at the
-- bound the checker computed for it.
#audit_axioms Hw.height_pos
#audit_axioms Hw.fragAltsB_find
#audit_axioms Hw.heightAlts_find
#audit_axioms Hw.fragB_no_outOfFuel
#audit_axioms Hw.fragListB_no_outOfFuel
#audit_axioms Hw.fragAltsB_no_outOfFuel
#audit_axioms Hw.checkResidual_entry
#audit_axioms Hw.checkResidual_sound
#audit_axioms Hw.mkSim_proj_chk
#audit_axioms Hw.runProjected_never_boundExceeded

-- Phase 5 increment 2: the runtime check, both halves of `runProjected`, and
-- the first `StepCorrect` instance on this branch.
#audit_axioms Hw.runtimeOK_sound
#audit_axioms Hw.fragB_evalFuel
#audit_axioms Hw.fragListB_evalFuel
#audit_axioms Hw.checkResidual_complete
#audit_axioms Hw.SimWF.toSimSound
#audit_axioms Hw.runProjected_correct
#audit_axioms Hw.runProjected_success
#audit_axioms Hw.stepOf_correct
#audit_axioms Hw.stepOf_succeeds
#audit_axioms Hw.stepTrace_projected
#audit_axioms Hw.FixtureStep.tiny_one_cycle
#audit_axioms Hw.FixtureStep.seq_one_cycle
#audit_axioms Hw.FixtureStep.tiny_two_cycles
#audit_axioms Hw.FixtureStep.seq_two_cycles

-- Phase 6: the support report is FAITHFUL -- all six booleans true really does
-- give `SupportedByProjection`.  The parser itself stays trusted.
#audit_axioms CertLoad.slotsInRange_of_bool
#audit_axioms CertLoad.supported_of_report

end Projection
