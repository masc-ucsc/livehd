module BranchKillableQueue_3(
  input         clock,
  input         reset,
  output        io_enq_ready,
  input         io_enq_valid,
  input  [6:0]  io_enq_bits_uop_uopc,
  input  [31:0] io_enq_bits_uop_inst,
  input  [31:0] io_enq_bits_uop_debug_inst,
  input         io_enq_bits_uop_is_rvc,
  input  [39:0] io_enq_bits_uop_debug_pc,
  input  [2:0]  io_enq_bits_uop_iq_type,
  input  [9:0]  io_enq_bits_uop_fu_code,
  input  [3:0]  io_enq_bits_uop_ctrl_br_type,
  input  [1:0]  io_enq_bits_uop_ctrl_op1_sel,
  input  [2:0]  io_enq_bits_uop_ctrl_op2_sel,
  input  [2:0]  io_enq_bits_uop_ctrl_imm_sel,
  input  [3:0]  io_enq_bits_uop_ctrl_op_fcn,
  input         io_enq_bits_uop_ctrl_fcn_dw,
  input  [2:0]  io_enq_bits_uop_ctrl_csr_cmd,
  input         io_enq_bits_uop_ctrl_is_load,
  input         io_enq_bits_uop_ctrl_is_sta,
  input         io_enq_bits_uop_ctrl_is_std,
  input  [1:0]  io_enq_bits_uop_iw_state,
  input         io_enq_bits_uop_iw_p1_poisoned,
  input         io_enq_bits_uop_iw_p2_poisoned,
  input         io_enq_bits_uop_is_br,
  input         io_enq_bits_uop_is_jalr,
  input         io_enq_bits_uop_is_jal,
  input         io_enq_bits_uop_is_sfb,
  input  [7:0]  io_enq_bits_uop_br_mask,
  input  [2:0]  io_enq_bits_uop_br_tag,
  input  [3:0]  io_enq_bits_uop_ftq_idx,
  input         io_enq_bits_uop_edge_inst,
  input  [5:0]  io_enq_bits_uop_pc_lob,
  input         io_enq_bits_uop_taken,
  input  [19:0] io_enq_bits_uop_imm_packed,
  input  [11:0] io_enq_bits_uop_csr_addr,
  input  [4:0]  io_enq_bits_uop_rob_idx,
  input  [2:0]  io_enq_bits_uop_ldq_idx,
  input  [2:0]  io_enq_bits_uop_stq_idx,
  input  [1:0]  io_enq_bits_uop_rxq_idx,
  input  [5:0]  io_enq_bits_uop_pdst,
  input  [5:0]  io_enq_bits_uop_prs1,
  input  [5:0]  io_enq_bits_uop_prs2,
  input  [5:0]  io_enq_bits_uop_prs3,
  input  [3:0]  io_enq_bits_uop_ppred,
  input         io_enq_bits_uop_prs1_busy,
  input         io_enq_bits_uop_prs2_busy,
  input         io_enq_bits_uop_prs3_busy,
  input         io_enq_bits_uop_ppred_busy,
  input  [5:0]  io_enq_bits_uop_stale_pdst,
  input         io_enq_bits_uop_exception,
  input  [63:0] io_enq_bits_uop_exc_cause,
  input         io_enq_bits_uop_bypassable,
  input  [4:0]  io_enq_bits_uop_mem_cmd,
  input  [1:0]  io_enq_bits_uop_mem_size,
  input         io_enq_bits_uop_mem_signed,
  input         io_enq_bits_uop_is_fence,
  input         io_enq_bits_uop_is_fencei,
  input         io_enq_bits_uop_is_amo,
  input         io_enq_bits_uop_uses_ldq,
  input         io_enq_bits_uop_uses_stq,
  input         io_enq_bits_uop_is_sys_pc2epc,
  input         io_enq_bits_uop_is_unique,
  input         io_enq_bits_uop_flush_on_commit,
  input         io_enq_bits_uop_ldst_is_rs1,
  input  [5:0]  io_enq_bits_uop_ldst,
  input  [5:0]  io_enq_bits_uop_lrs1,
  input  [5:0]  io_enq_bits_uop_lrs2,
  input  [5:0]  io_enq_bits_uop_lrs3,
  input         io_enq_bits_uop_ldst_val,
  input  [1:0]  io_enq_bits_uop_dst_rtype,
  input  [1:0]  io_enq_bits_uop_lrs1_rtype,
  input  [1:0]  io_enq_bits_uop_lrs2_rtype,
  input         io_enq_bits_uop_frs3_en,
  input         io_enq_bits_uop_fp_val,
  input         io_enq_bits_uop_fp_single,
  input         io_enq_bits_uop_xcpt_pf_if,
  input         io_enq_bits_uop_xcpt_ae_if,
  input         io_enq_bits_uop_xcpt_ma_if,
  input         io_enq_bits_uop_bp_debug_if,
  input         io_enq_bits_uop_bp_xcpt_if,
  input  [1:0]  io_enq_bits_uop_debug_fsrc,
  input  [1:0]  io_enq_bits_uop_debug_tsrc,
  input  [64:0] io_enq_bits_data,
  input         io_enq_bits_predicated,
  input         io_enq_bits_fflags_valid,
  input  [6:0]  io_enq_bits_fflags_bits_uop_uopc,
  input  [31:0] io_enq_bits_fflags_bits_uop_inst,
  input  [31:0] io_enq_bits_fflags_bits_uop_debug_inst,
  input         io_enq_bits_fflags_bits_uop_is_rvc,
  input  [39:0] io_enq_bits_fflags_bits_uop_debug_pc,
  input  [2:0]  io_enq_bits_fflags_bits_uop_iq_type,
  input  [9:0]  io_enq_bits_fflags_bits_uop_fu_code,
  input  [3:0]  io_enq_bits_fflags_bits_uop_ctrl_br_type,
  input  [1:0]  io_enq_bits_fflags_bits_uop_ctrl_op1_sel,
  input  [2:0]  io_enq_bits_fflags_bits_uop_ctrl_op2_sel,
  input  [2:0]  io_enq_bits_fflags_bits_uop_ctrl_imm_sel,
  input  [3:0]  io_enq_bits_fflags_bits_uop_ctrl_op_fcn,
  input         io_enq_bits_fflags_bits_uop_ctrl_fcn_dw,
  input  [2:0]  io_enq_bits_fflags_bits_uop_ctrl_csr_cmd,
  input         io_enq_bits_fflags_bits_uop_ctrl_is_load,
  input         io_enq_bits_fflags_bits_uop_ctrl_is_sta,
  input         io_enq_bits_fflags_bits_uop_ctrl_is_std,
  input  [1:0]  io_enq_bits_fflags_bits_uop_iw_state,
  input         io_enq_bits_fflags_bits_uop_iw_p1_poisoned,
  input         io_enq_bits_fflags_bits_uop_iw_p2_poisoned,
  input         io_enq_bits_fflags_bits_uop_is_br,
  input         io_enq_bits_fflags_bits_uop_is_jalr,
  input         io_enq_bits_fflags_bits_uop_is_jal,
  input         io_enq_bits_fflags_bits_uop_is_sfb,
  input  [7:0]  io_enq_bits_fflags_bits_uop_br_mask,
  input  [2:0]  io_enq_bits_fflags_bits_uop_br_tag,
  input  [3:0]  io_enq_bits_fflags_bits_uop_ftq_idx,
  input         io_enq_bits_fflags_bits_uop_edge_inst,
  input  [5:0]  io_enq_bits_fflags_bits_uop_pc_lob,
  input         io_enq_bits_fflags_bits_uop_taken,
  input  [19:0] io_enq_bits_fflags_bits_uop_imm_packed,
  input  [11:0] io_enq_bits_fflags_bits_uop_csr_addr,
  input  [4:0]  io_enq_bits_fflags_bits_uop_rob_idx,
  input  [2:0]  io_enq_bits_fflags_bits_uop_ldq_idx,
  input  [2:0]  io_enq_bits_fflags_bits_uop_stq_idx,
  input  [1:0]  io_enq_bits_fflags_bits_uop_rxq_idx,
  input  [5:0]  io_enq_bits_fflags_bits_uop_pdst,
  input  [5:0]  io_enq_bits_fflags_bits_uop_prs1,
  input  [5:0]  io_enq_bits_fflags_bits_uop_prs2,
  input  [5:0]  io_enq_bits_fflags_bits_uop_prs3,
  input  [3:0]  io_enq_bits_fflags_bits_uop_ppred,
  input         io_enq_bits_fflags_bits_uop_prs1_busy,
  input         io_enq_bits_fflags_bits_uop_prs2_busy,
  input         io_enq_bits_fflags_bits_uop_prs3_busy,
  input         io_enq_bits_fflags_bits_uop_ppred_busy,
  input  [5:0]  io_enq_bits_fflags_bits_uop_stale_pdst,
  input         io_enq_bits_fflags_bits_uop_exception,
  input  [63:0] io_enq_bits_fflags_bits_uop_exc_cause,
  input         io_enq_bits_fflags_bits_uop_bypassable,
  input  [4:0]  io_enq_bits_fflags_bits_uop_mem_cmd,
  input  [1:0]  io_enq_bits_fflags_bits_uop_mem_size,
  input         io_enq_bits_fflags_bits_uop_mem_signed,
  input         io_enq_bits_fflags_bits_uop_is_fence,
  input         io_enq_bits_fflags_bits_uop_is_fencei,
  input         io_enq_bits_fflags_bits_uop_is_amo,
  input         io_enq_bits_fflags_bits_uop_uses_ldq,
  input         io_enq_bits_fflags_bits_uop_uses_stq,
  input         io_enq_bits_fflags_bits_uop_is_sys_pc2epc,
  input         io_enq_bits_fflags_bits_uop_is_unique,
  input         io_enq_bits_fflags_bits_uop_flush_on_commit,
  input         io_enq_bits_fflags_bits_uop_ldst_is_rs1,
  input  [5:0]  io_enq_bits_fflags_bits_uop_ldst,
  input  [5:0]  io_enq_bits_fflags_bits_uop_lrs1,
  input  [5:0]  io_enq_bits_fflags_bits_uop_lrs2,
  input  [5:0]  io_enq_bits_fflags_bits_uop_lrs3,
  input         io_enq_bits_fflags_bits_uop_ldst_val,
  input  [1:0]  io_enq_bits_fflags_bits_uop_dst_rtype,
  input  [1:0]  io_enq_bits_fflags_bits_uop_lrs1_rtype,
  input  [1:0]  io_enq_bits_fflags_bits_uop_lrs2_rtype,
  input         io_enq_bits_fflags_bits_uop_frs3_en,
  input         io_enq_bits_fflags_bits_uop_fp_val,
  input         io_enq_bits_fflags_bits_uop_fp_single,
  input         io_enq_bits_fflags_bits_uop_xcpt_pf_if,
  input         io_enq_bits_fflags_bits_uop_xcpt_ae_if,
  input         io_enq_bits_fflags_bits_uop_xcpt_ma_if,
  input         io_enq_bits_fflags_bits_uop_bp_debug_if,
  input         io_enq_bits_fflags_bits_uop_bp_xcpt_if,
  input  [1:0]  io_enq_bits_fflags_bits_uop_debug_fsrc,
  input  [1:0]  io_enq_bits_fflags_bits_uop_debug_tsrc,
  input  [4:0]  io_enq_bits_fflags_bits_flags,
  input         io_deq_ready,
  output        io_deq_valid,
  output [6:0]  io_deq_bits_uop_uopc,
  output [31:0] io_deq_bits_uop_inst,
  output [31:0] io_deq_bits_uop_debug_inst,
  output        io_deq_bits_uop_is_rvc,
  output [39:0] io_deq_bits_uop_debug_pc,
  output [2:0]  io_deq_bits_uop_iq_type,
  output [9:0]  io_deq_bits_uop_fu_code,
  output [3:0]  io_deq_bits_uop_ctrl_br_type,
  output [1:0]  io_deq_bits_uop_ctrl_op1_sel,
  output [2:0]  io_deq_bits_uop_ctrl_op2_sel,
  output [2:0]  io_deq_bits_uop_ctrl_imm_sel,
  output [3:0]  io_deq_bits_uop_ctrl_op_fcn,
  output        io_deq_bits_uop_ctrl_fcn_dw,
  output [2:0]  io_deq_bits_uop_ctrl_csr_cmd,
  output        io_deq_bits_uop_ctrl_is_load,
  output        io_deq_bits_uop_ctrl_is_sta,
  output        io_deq_bits_uop_ctrl_is_std,
  output [1:0]  io_deq_bits_uop_iw_state,
  output        io_deq_bits_uop_iw_p1_poisoned,
  output        io_deq_bits_uop_iw_p2_poisoned,
  output        io_deq_bits_uop_is_br,
  output        io_deq_bits_uop_is_jalr,
  output        io_deq_bits_uop_is_jal,
  output        io_deq_bits_uop_is_sfb,
  output [7:0]  io_deq_bits_uop_br_mask,
  output [2:0]  io_deq_bits_uop_br_tag,
  output [3:0]  io_deq_bits_uop_ftq_idx,
  output        io_deq_bits_uop_edge_inst,
  output [5:0]  io_deq_bits_uop_pc_lob,
  output        io_deq_bits_uop_taken,
  output [19:0] io_deq_bits_uop_imm_packed,
  output [11:0] io_deq_bits_uop_csr_addr,
  output [4:0]  io_deq_bits_uop_rob_idx,
  output [2:0]  io_deq_bits_uop_ldq_idx,
  output [2:0]  io_deq_bits_uop_stq_idx,
  output [1:0]  io_deq_bits_uop_rxq_idx,
  output [5:0]  io_deq_bits_uop_pdst,
  output [5:0]  io_deq_bits_uop_prs1,
  output [5:0]  io_deq_bits_uop_prs2,
  output [5:0]  io_deq_bits_uop_prs3,
  output [3:0]  io_deq_bits_uop_ppred,
  output        io_deq_bits_uop_prs1_busy,
  output        io_deq_bits_uop_prs2_busy,
  output        io_deq_bits_uop_prs3_busy,
  output        io_deq_bits_uop_ppred_busy,
  output [5:0]  io_deq_bits_uop_stale_pdst,
  output        io_deq_bits_uop_exception,
  output [63:0] io_deq_bits_uop_exc_cause,
  output        io_deq_bits_uop_bypassable,
  output [4:0]  io_deq_bits_uop_mem_cmd,
  output [1:0]  io_deq_bits_uop_mem_size,
  output        io_deq_bits_uop_mem_signed,
  output        io_deq_bits_uop_is_fence,
  output        io_deq_bits_uop_is_fencei,
  output        io_deq_bits_uop_is_amo,
  output        io_deq_bits_uop_uses_ldq,
  output        io_deq_bits_uop_uses_stq,
  output        io_deq_bits_uop_is_sys_pc2epc,
  output        io_deq_bits_uop_is_unique,
  output        io_deq_bits_uop_flush_on_commit,
  output        io_deq_bits_uop_ldst_is_rs1,
  output [5:0]  io_deq_bits_uop_ldst,
  output [5:0]  io_deq_bits_uop_lrs1,
  output [5:0]  io_deq_bits_uop_lrs2,
  output [5:0]  io_deq_bits_uop_lrs3,
  output        io_deq_bits_uop_ldst_val,
  output [1:0]  io_deq_bits_uop_dst_rtype,
  output [1:0]  io_deq_bits_uop_lrs1_rtype,
  output [1:0]  io_deq_bits_uop_lrs2_rtype,
  output        io_deq_bits_uop_frs3_en,
  output        io_deq_bits_uop_fp_val,
  output        io_deq_bits_uop_fp_single,
  output        io_deq_bits_uop_xcpt_pf_if,
  output        io_deq_bits_uop_xcpt_ae_if,
  output        io_deq_bits_uop_xcpt_ma_if,
  output        io_deq_bits_uop_bp_debug_if,
  output        io_deq_bits_uop_bp_xcpt_if,
  output [1:0]  io_deq_bits_uop_debug_fsrc,
  output [1:0]  io_deq_bits_uop_debug_tsrc,
  output [64:0] io_deq_bits_data,
  output        io_deq_bits_predicated,
  output        io_deq_bits_fflags_valid,
  output [6:0]  io_deq_bits_fflags_bits_uop_uopc,
  output [31:0] io_deq_bits_fflags_bits_uop_inst,
  output [31:0] io_deq_bits_fflags_bits_uop_debug_inst,
  output        io_deq_bits_fflags_bits_uop_is_rvc,
  output [39:0] io_deq_bits_fflags_bits_uop_debug_pc,
  output [2:0]  io_deq_bits_fflags_bits_uop_iq_type,
  output [9:0]  io_deq_bits_fflags_bits_uop_fu_code,
  output [3:0]  io_deq_bits_fflags_bits_uop_ctrl_br_type,
  output [1:0]  io_deq_bits_fflags_bits_uop_ctrl_op1_sel,
  output [2:0]  io_deq_bits_fflags_bits_uop_ctrl_op2_sel,
  output [2:0]  io_deq_bits_fflags_bits_uop_ctrl_imm_sel,
  output [3:0]  io_deq_bits_fflags_bits_uop_ctrl_op_fcn,
  output        io_deq_bits_fflags_bits_uop_ctrl_fcn_dw,
  output [2:0]  io_deq_bits_fflags_bits_uop_ctrl_csr_cmd,
  output        io_deq_bits_fflags_bits_uop_ctrl_is_load,
  output        io_deq_bits_fflags_bits_uop_ctrl_is_sta,
  output        io_deq_bits_fflags_bits_uop_ctrl_is_std,
  output [1:0]  io_deq_bits_fflags_bits_uop_iw_state,
  output        io_deq_bits_fflags_bits_uop_iw_p1_poisoned,
  output        io_deq_bits_fflags_bits_uop_iw_p2_poisoned,
  output        io_deq_bits_fflags_bits_uop_is_br,
  output        io_deq_bits_fflags_bits_uop_is_jalr,
  output        io_deq_bits_fflags_bits_uop_is_jal,
  output        io_deq_bits_fflags_bits_uop_is_sfb,
  output [7:0]  io_deq_bits_fflags_bits_uop_br_mask,
  output [2:0]  io_deq_bits_fflags_bits_uop_br_tag,
  output [3:0]  io_deq_bits_fflags_bits_uop_ftq_idx,
  output        io_deq_bits_fflags_bits_uop_edge_inst,
  output [5:0]  io_deq_bits_fflags_bits_uop_pc_lob,
  output        io_deq_bits_fflags_bits_uop_taken,
  output [19:0] io_deq_bits_fflags_bits_uop_imm_packed,
  output [11:0] io_deq_bits_fflags_bits_uop_csr_addr,
  output [4:0]  io_deq_bits_fflags_bits_uop_rob_idx,
  output [2:0]  io_deq_bits_fflags_bits_uop_ldq_idx,
  output [2:0]  io_deq_bits_fflags_bits_uop_stq_idx,
  output [1:0]  io_deq_bits_fflags_bits_uop_rxq_idx,
  output [5:0]  io_deq_bits_fflags_bits_uop_pdst,
  output [5:0]  io_deq_bits_fflags_bits_uop_prs1,
  output [5:0]  io_deq_bits_fflags_bits_uop_prs2,
  output [5:0]  io_deq_bits_fflags_bits_uop_prs3,
  output [3:0]  io_deq_bits_fflags_bits_uop_ppred,
  output        io_deq_bits_fflags_bits_uop_prs1_busy,
  output        io_deq_bits_fflags_bits_uop_prs2_busy,
  output        io_deq_bits_fflags_bits_uop_prs3_busy,
  output        io_deq_bits_fflags_bits_uop_ppred_busy,
  output [5:0]  io_deq_bits_fflags_bits_uop_stale_pdst,
  output        io_deq_bits_fflags_bits_uop_exception,
  output [63:0] io_deq_bits_fflags_bits_uop_exc_cause,
  output        io_deq_bits_fflags_bits_uop_bypassable,
  output [4:0]  io_deq_bits_fflags_bits_uop_mem_cmd,
  output [1:0]  io_deq_bits_fflags_bits_uop_mem_size,
  output        io_deq_bits_fflags_bits_uop_mem_signed,
  output        io_deq_bits_fflags_bits_uop_is_fence,
  output        io_deq_bits_fflags_bits_uop_is_fencei,
  output        io_deq_bits_fflags_bits_uop_is_amo,
  output        io_deq_bits_fflags_bits_uop_uses_ldq,
  output        io_deq_bits_fflags_bits_uop_uses_stq,
  output        io_deq_bits_fflags_bits_uop_is_sys_pc2epc,
  output        io_deq_bits_fflags_bits_uop_is_unique,
  output        io_deq_bits_fflags_bits_uop_flush_on_commit,
  output        io_deq_bits_fflags_bits_uop_ldst_is_rs1,
  output [5:0]  io_deq_bits_fflags_bits_uop_ldst,
  output [5:0]  io_deq_bits_fflags_bits_uop_lrs1,
  output [5:0]  io_deq_bits_fflags_bits_uop_lrs2,
  output [5:0]  io_deq_bits_fflags_bits_uop_lrs3,
  output        io_deq_bits_fflags_bits_uop_ldst_val,
  output [1:0]  io_deq_bits_fflags_bits_uop_dst_rtype,
  output [1:0]  io_deq_bits_fflags_bits_uop_lrs1_rtype,
  output [1:0]  io_deq_bits_fflags_bits_uop_lrs2_rtype,
  output        io_deq_bits_fflags_bits_uop_frs3_en,
  output        io_deq_bits_fflags_bits_uop_fp_val,
  output        io_deq_bits_fflags_bits_uop_fp_single,
  output        io_deq_bits_fflags_bits_uop_xcpt_pf_if,
  output        io_deq_bits_fflags_bits_uop_xcpt_ae_if,
  output        io_deq_bits_fflags_bits_uop_xcpt_ma_if,
  output        io_deq_bits_fflags_bits_uop_bp_debug_if,
  output        io_deq_bits_fflags_bits_uop_bp_xcpt_if,
  output [1:0]  io_deq_bits_fflags_bits_uop_debug_fsrc,
  output [1:0]  io_deq_bits_fflags_bits_uop_debug_tsrc,
  output [4:0]  io_deq_bits_fflags_bits_flags,
  input  [7:0]  io_brupdate_b1_resolve_mask,
  input  [7:0]  io_brupdate_b1_mispredict_mask,
  input  [6:0]  io_brupdate_b2_uop_uopc,
  input  [31:0] io_brupdate_b2_uop_inst,
  input  [31:0] io_brupdate_b2_uop_debug_inst,
  input         io_brupdate_b2_uop_is_rvc,
  input  [39:0] io_brupdate_b2_uop_debug_pc,
  input  [2:0]  io_brupdate_b2_uop_iq_type,
  input  [9:0]  io_brupdate_b2_uop_fu_code,
  input  [3:0]  io_brupdate_b2_uop_ctrl_br_type,
  input  [1:0]  io_brupdate_b2_uop_ctrl_op1_sel,
  input  [2:0]  io_brupdate_b2_uop_ctrl_op2_sel,
  input  [2:0]  io_brupdate_b2_uop_ctrl_imm_sel,
  input  [3:0]  io_brupdate_b2_uop_ctrl_op_fcn,
  input         io_brupdate_b2_uop_ctrl_fcn_dw,
  input  [2:0]  io_brupdate_b2_uop_ctrl_csr_cmd,
  input         io_brupdate_b2_uop_ctrl_is_load,
  input         io_brupdate_b2_uop_ctrl_is_sta,
  input         io_brupdate_b2_uop_ctrl_is_std,
  input  [1:0]  io_brupdate_b2_uop_iw_state,
  input         io_brupdate_b2_uop_iw_p1_poisoned,
  input         io_brupdate_b2_uop_iw_p2_poisoned,
  input         io_brupdate_b2_uop_is_br,
  input         io_brupdate_b2_uop_is_jalr,
  input         io_brupdate_b2_uop_is_jal,
  input         io_brupdate_b2_uop_is_sfb,
  input  [7:0]  io_brupdate_b2_uop_br_mask,
  input  [2:0]  io_brupdate_b2_uop_br_tag,
  input  [3:0]  io_brupdate_b2_uop_ftq_idx,
  input         io_brupdate_b2_uop_edge_inst,
  input  [5:0]  io_brupdate_b2_uop_pc_lob,
  input         io_brupdate_b2_uop_taken,
  input  [19:0] io_brupdate_b2_uop_imm_packed,
  input  [11:0] io_brupdate_b2_uop_csr_addr,
  input  [4:0]  io_brupdate_b2_uop_rob_idx,
  input  [2:0]  io_brupdate_b2_uop_ldq_idx,
  input  [2:0]  io_brupdate_b2_uop_stq_idx,
  input  [1:0]  io_brupdate_b2_uop_rxq_idx,
  input  [5:0]  io_brupdate_b2_uop_pdst,
  input  [5:0]  io_brupdate_b2_uop_prs1,
  input  [5:0]  io_brupdate_b2_uop_prs2,
  input  [5:0]  io_brupdate_b2_uop_prs3,
  input  [3:0]  io_brupdate_b2_uop_ppred,
  input         io_brupdate_b2_uop_prs1_busy,
  input         io_brupdate_b2_uop_prs2_busy,
  input         io_brupdate_b2_uop_prs3_busy,
  input         io_brupdate_b2_uop_ppred_busy,
  input  [5:0]  io_brupdate_b2_uop_stale_pdst,
  input         io_brupdate_b2_uop_exception,
  input  [63:0] io_brupdate_b2_uop_exc_cause,
  input         io_brupdate_b2_uop_bypassable,
  input  [4:0]  io_brupdate_b2_uop_mem_cmd,
  input  [1:0]  io_brupdate_b2_uop_mem_size,
  input         io_brupdate_b2_uop_mem_signed,
  input         io_brupdate_b2_uop_is_fence,
  input         io_brupdate_b2_uop_is_fencei,
  input         io_brupdate_b2_uop_is_amo,
  input         io_brupdate_b2_uop_uses_ldq,
  input         io_brupdate_b2_uop_uses_stq,
  input         io_brupdate_b2_uop_is_sys_pc2epc,
  input         io_brupdate_b2_uop_is_unique,
  input         io_brupdate_b2_uop_flush_on_commit,
  input         io_brupdate_b2_uop_ldst_is_rs1,
  input  [5:0]  io_brupdate_b2_uop_ldst,
  input  [5:0]  io_brupdate_b2_uop_lrs1,
  input  [5:0]  io_brupdate_b2_uop_lrs2,
  input  [5:0]  io_brupdate_b2_uop_lrs3,
  input         io_brupdate_b2_uop_ldst_val,
  input  [1:0]  io_brupdate_b2_uop_dst_rtype,
  input  [1:0]  io_brupdate_b2_uop_lrs1_rtype,
  input  [1:0]  io_brupdate_b2_uop_lrs2_rtype,
  input         io_brupdate_b2_uop_frs3_en,
  input         io_brupdate_b2_uop_fp_val,
  input         io_brupdate_b2_uop_fp_single,
  input         io_brupdate_b2_uop_xcpt_pf_if,
  input         io_brupdate_b2_uop_xcpt_ae_if,
  input         io_brupdate_b2_uop_xcpt_ma_if,
  input         io_brupdate_b2_uop_bp_debug_if,
  input         io_brupdate_b2_uop_bp_xcpt_if,
  input  [1:0]  io_brupdate_b2_uop_debug_fsrc,
  input  [1:0]  io_brupdate_b2_uop_debug_tsrc,
  input         io_brupdate_b2_valid,
  input         io_brupdate_b2_mispredict,
  input         io_brupdate_b2_taken,
  input  [2:0]  io_brupdate_b2_cfi_type,
  input  [1:0]  io_brupdate_b2_pc_sel,
  input  [39:0] io_brupdate_b2_jalr_target,
  input         io_brupdate_b2_target_offset,
  input         io_flush,
  output        io_empty,
  output [2:0]  io_count
);
`ifdef RANDOMIZE_GARBAGE_ASSIGN
  reg [95:0] _RAND_1;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_13;
  reg [63:0] _RAND_15;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_31;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_35;
  reg [31:0] _RAND_37;
  reg [31:0] _RAND_39;
  reg [31:0] _RAND_41;
  reg [31:0] _RAND_43;
  reg [31:0] _RAND_45;
  reg [31:0] _RAND_47;
  reg [31:0] _RAND_49;
  reg [31:0] _RAND_51;
  reg [31:0] _RAND_53;
  reg [31:0] _RAND_55;
  reg [31:0] _RAND_57;
  reg [31:0] _RAND_59;
  reg [31:0] _RAND_61;
  reg [31:0] _RAND_63;
  reg [31:0] _RAND_65;
  reg [31:0] _RAND_67;
  reg [31:0] _RAND_69;
  reg [31:0] _RAND_71;
  reg [31:0] _RAND_73;
  reg [31:0] _RAND_75;
  reg [31:0] _RAND_77;
  reg [31:0] _RAND_79;
  reg [31:0] _RAND_81;
  reg [31:0] _RAND_83;
  reg [31:0] _RAND_85;
  reg [31:0] _RAND_87;
  reg [31:0] _RAND_89;
  reg [31:0] _RAND_91;
  reg [31:0] _RAND_93;
  reg [31:0] _RAND_95;
  reg [31:0] _RAND_97;
  reg [31:0] _RAND_99;
  reg [63:0] _RAND_101;
  reg [31:0] _RAND_103;
  reg [31:0] _RAND_105;
  reg [31:0] _RAND_107;
  reg [31:0] _RAND_109;
  reg [31:0] _RAND_111;
  reg [31:0] _RAND_113;
  reg [31:0] _RAND_115;
  reg [31:0] _RAND_117;
  reg [31:0] _RAND_119;
  reg [31:0] _RAND_121;
  reg [31:0] _RAND_123;
  reg [31:0] _RAND_125;
  reg [31:0] _RAND_127;
  reg [31:0] _RAND_129;
  reg [31:0] _RAND_131;
  reg [31:0] _RAND_133;
  reg [31:0] _RAND_135;
  reg [31:0] _RAND_137;
  reg [31:0] _RAND_139;
  reg [31:0] _RAND_141;
  reg [31:0] _RAND_143;
  reg [31:0] _RAND_145;
  reg [31:0] _RAND_147;
  reg [31:0] _RAND_149;
  reg [31:0] _RAND_151;
  reg [31:0] _RAND_153;
  reg [31:0] _RAND_155;
  reg [31:0] _RAND_157;
  reg [31:0] _RAND_159;
  reg [31:0] _RAND_161;
  reg [31:0] _RAND_163;
  reg [31:0] _RAND_165;
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  reg [95:0] _RAND_0;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_12;
  reg [63:0] _RAND_14;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_36;
  reg [31:0] _RAND_38;
  reg [31:0] _RAND_40;
  reg [31:0] _RAND_42;
  reg [31:0] _RAND_44;
  reg [31:0] _RAND_46;
  reg [31:0] _RAND_48;
  reg [31:0] _RAND_50;
  reg [31:0] _RAND_52;
  reg [31:0] _RAND_54;
  reg [31:0] _RAND_56;
  reg [31:0] _RAND_58;
  reg [31:0] _RAND_60;
  reg [31:0] _RAND_62;
  reg [31:0] _RAND_64;
  reg [31:0] _RAND_66;
  reg [31:0] _RAND_68;
  reg [31:0] _RAND_70;
  reg [31:0] _RAND_72;
  reg [31:0] _RAND_74;
  reg [31:0] _RAND_76;
  reg [31:0] _RAND_78;
  reg [31:0] _RAND_80;
  reg [31:0] _RAND_82;
  reg [31:0] _RAND_84;
  reg [31:0] _RAND_86;
  reg [31:0] _RAND_88;
  reg [31:0] _RAND_90;
  reg [31:0] _RAND_92;
  reg [31:0] _RAND_94;
  reg [31:0] _RAND_96;
  reg [31:0] _RAND_98;
  reg [63:0] _RAND_100;
  reg [31:0] _RAND_102;
  reg [31:0] _RAND_104;
  reg [31:0] _RAND_106;
  reg [31:0] _RAND_108;
  reg [31:0] _RAND_110;
  reg [31:0] _RAND_112;
  reg [31:0] _RAND_114;
  reg [31:0] _RAND_116;
  reg [31:0] _RAND_118;
  reg [31:0] _RAND_120;
  reg [31:0] _RAND_122;
  reg [31:0] _RAND_124;
  reg [31:0] _RAND_126;
  reg [31:0] _RAND_128;
  reg [31:0] _RAND_130;
  reg [31:0] _RAND_132;
  reg [31:0] _RAND_134;
  reg [31:0] _RAND_136;
  reg [31:0] _RAND_138;
  reg [31:0] _RAND_140;
  reg [31:0] _RAND_142;
  reg [31:0] _RAND_144;
  reg [31:0] _RAND_146;
  reg [31:0] _RAND_148;
  reg [31:0] _RAND_150;
  reg [31:0] _RAND_152;
  reg [31:0] _RAND_154;
  reg [31:0] _RAND_156;
  reg [31:0] _RAND_158;
  reg [31:0] _RAND_160;
  reg [31:0] _RAND_162;
  reg [31:0] _RAND_164;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_166;
  reg [31:0] _RAND_167;
  reg [31:0] _RAND_168;
  reg [31:0] _RAND_169;
  reg [31:0] _RAND_170;
  reg [31:0] _RAND_171;
  reg [31:0] _RAND_172;
  reg [31:0] _RAND_173;
  reg [31:0] _RAND_174;
  reg [63:0] _RAND_175;
  reg [31:0] _RAND_176;
  reg [31:0] _RAND_177;
  reg [31:0] _RAND_178;
  reg [31:0] _RAND_179;
  reg [31:0] _RAND_180;
  reg [31:0] _RAND_181;
  reg [31:0] _RAND_182;
  reg [31:0] _RAND_183;
  reg [31:0] _RAND_184;
  reg [31:0] _RAND_185;
  reg [31:0] _RAND_186;
  reg [31:0] _RAND_187;
  reg [31:0] _RAND_188;
  reg [31:0] _RAND_189;
  reg [31:0] _RAND_190;
  reg [31:0] _RAND_191;
  reg [31:0] _RAND_192;
  reg [31:0] _RAND_193;
  reg [31:0] _RAND_194;
  reg [31:0] _RAND_195;
  reg [31:0] _RAND_196;
  reg [31:0] _RAND_197;
  reg [31:0] _RAND_198;
  reg [31:0] _RAND_199;
  reg [31:0] _RAND_200;
  reg [31:0] _RAND_201;
  reg [31:0] _RAND_202;
  reg [31:0] _RAND_203;
  reg [31:0] _RAND_204;
  reg [31:0] _RAND_205;
  reg [31:0] _RAND_206;
  reg [31:0] _RAND_207;
  reg [31:0] _RAND_208;
  reg [31:0] _RAND_209;
  reg [31:0] _RAND_210;
  reg [31:0] _RAND_211;
  reg [31:0] _RAND_212;
  reg [31:0] _RAND_213;
  reg [31:0] _RAND_214;
  reg [31:0] _RAND_215;
  reg [31:0] _RAND_216;
  reg [31:0] _RAND_217;
  reg [63:0] _RAND_218;
  reg [31:0] _RAND_219;
  reg [31:0] _RAND_220;
  reg [31:0] _RAND_221;
  reg [31:0] _RAND_222;
  reg [31:0] _RAND_223;
  reg [31:0] _RAND_224;
  reg [31:0] _RAND_225;
  reg [31:0] _RAND_226;
  reg [31:0] _RAND_227;
  reg [31:0] _RAND_228;
  reg [31:0] _RAND_229;
  reg [31:0] _RAND_230;
  reg [31:0] _RAND_231;
  reg [31:0] _RAND_232;
  reg [31:0] _RAND_233;
  reg [31:0] _RAND_234;
  reg [31:0] _RAND_235;
  reg [31:0] _RAND_236;
  reg [31:0] _RAND_237;
  reg [31:0] _RAND_238;
  reg [31:0] _RAND_239;
  reg [31:0] _RAND_240;
  reg [31:0] _RAND_241;
  reg [31:0] _RAND_242;
  reg [31:0] _RAND_243;
  reg [31:0] _RAND_244;
  reg [31:0] _RAND_245;
  reg [31:0] _RAND_246;
  reg [31:0] _RAND_247;
  reg [31:0] _RAND_248;
  reg [31:0] _RAND_249;
  reg [31:0] _RAND_250;
  reg [31:0] _RAND_251;
  reg [31:0] _RAND_252;
  reg [31:0] _RAND_253;
  reg [63:0] _RAND_254;
  reg [31:0] _RAND_255;
  reg [31:0] _RAND_256;
  reg [31:0] _RAND_257;
  reg [31:0] _RAND_258;
  reg [31:0] _RAND_259;
  reg [31:0] _RAND_260;
  reg [31:0] _RAND_261;
  reg [31:0] _RAND_262;
  reg [31:0] _RAND_263;
  reg [31:0] _RAND_264;
  reg [31:0] _RAND_265;
  reg [31:0] _RAND_266;
  reg [31:0] _RAND_267;
  reg [31:0] _RAND_268;
  reg [31:0] _RAND_269;
  reg [31:0] _RAND_270;
  reg [31:0] _RAND_271;
  reg [31:0] _RAND_272;
  reg [31:0] _RAND_273;
  reg [31:0] _RAND_274;
  reg [31:0] _RAND_275;
  reg [31:0] _RAND_276;
  reg [31:0] _RAND_277;
  reg [31:0] _RAND_278;
  reg [31:0] _RAND_279;
  reg [31:0] _RAND_280;
  reg [31:0] _RAND_281;
  reg [31:0] _RAND_282;
  reg [31:0] _RAND_283;
  reg [31:0] _RAND_284;
  reg [31:0] _RAND_285;
  reg [31:0] _RAND_286;
  reg [31:0] _RAND_287;
  reg [31:0] _RAND_288;
  reg [31:0] _RAND_289;
  reg [31:0] _RAND_290;
  reg [31:0] _RAND_291;
  reg [31:0] _RAND_292;
  reg [31:0] _RAND_293;
  reg [31:0] _RAND_294;
  reg [31:0] _RAND_295;
  reg [31:0] _RAND_296;
  reg [63:0] _RAND_297;
  reg [31:0] _RAND_298;
  reg [31:0] _RAND_299;
  reg [31:0] _RAND_300;
  reg [31:0] _RAND_301;
  reg [31:0] _RAND_302;
  reg [31:0] _RAND_303;
  reg [31:0] _RAND_304;
  reg [31:0] _RAND_305;
  reg [31:0] _RAND_306;
  reg [31:0] _RAND_307;
  reg [31:0] _RAND_308;
  reg [31:0] _RAND_309;
  reg [31:0] _RAND_310;
  reg [31:0] _RAND_311;
  reg [31:0] _RAND_312;
  reg [31:0] _RAND_313;
  reg [31:0] _RAND_314;
  reg [31:0] _RAND_315;
  reg [31:0] _RAND_316;
  reg [31:0] _RAND_317;
  reg [31:0] _RAND_318;
  reg [31:0] _RAND_319;
  reg [31:0] _RAND_320;
  reg [31:0] _RAND_321;
  reg [31:0] _RAND_322;
  reg [31:0] _RAND_323;
  reg [31:0] _RAND_324;
  reg [31:0] _RAND_325;
  reg [31:0] _RAND_326;
  reg [31:0] _RAND_327;
  reg [31:0] _RAND_328;
  reg [31:0] _RAND_329;
  reg [31:0] _RAND_330;
  reg [31:0] _RAND_331;
  reg [31:0] _RAND_332;
  reg [63:0] _RAND_333;
  reg [31:0] _RAND_334;
  reg [31:0] _RAND_335;
  reg [31:0] _RAND_336;
  reg [31:0] _RAND_337;
  reg [31:0] _RAND_338;
  reg [31:0] _RAND_339;
  reg [31:0] _RAND_340;
  reg [31:0] _RAND_341;
  reg [31:0] _RAND_342;
  reg [31:0] _RAND_343;
  reg [31:0] _RAND_344;
  reg [31:0] _RAND_345;
  reg [31:0] _RAND_346;
  reg [31:0] _RAND_347;
  reg [31:0] _RAND_348;
  reg [31:0] _RAND_349;
  reg [31:0] _RAND_350;
  reg [31:0] _RAND_351;
  reg [31:0] _RAND_352;
  reg [31:0] _RAND_353;
  reg [31:0] _RAND_354;
  reg [31:0] _RAND_355;
  reg [31:0] _RAND_356;
  reg [31:0] _RAND_357;
  reg [31:0] _RAND_358;
  reg [31:0] _RAND_359;
  reg [31:0] _RAND_360;
  reg [31:0] _RAND_361;
  reg [31:0] _RAND_362;
  reg [31:0] _RAND_363;
  reg [31:0] _RAND_364;
  reg [31:0] _RAND_365;
  reg [31:0] _RAND_366;
  reg [31:0] _RAND_367;
  reg [31:0] _RAND_368;
  reg [31:0] _RAND_369;
  reg [31:0] _RAND_370;
  reg [31:0] _RAND_371;
  reg [31:0] _RAND_372;
  reg [31:0] _RAND_373;
  reg [31:0] _RAND_374;
  reg [31:0] _RAND_375;
  reg [63:0] _RAND_376;
  reg [31:0] _RAND_377;
  reg [31:0] _RAND_378;
  reg [31:0] _RAND_379;
  reg [31:0] _RAND_380;
  reg [31:0] _RAND_381;
  reg [31:0] _RAND_382;
  reg [31:0] _RAND_383;
  reg [31:0] _RAND_384;
  reg [31:0] _RAND_385;
  reg [31:0] _RAND_386;
  reg [31:0] _RAND_387;
  reg [31:0] _RAND_388;
  reg [31:0] _RAND_389;
  reg [31:0] _RAND_390;
  reg [31:0] _RAND_391;
  reg [31:0] _RAND_392;
  reg [31:0] _RAND_393;
  reg [31:0] _RAND_394;
  reg [31:0] _RAND_395;
  reg [31:0] _RAND_396;
  reg [31:0] _RAND_397;
  reg [31:0] _RAND_398;
  reg [31:0] _RAND_399;
  reg [31:0] _RAND_400;
  reg [31:0] _RAND_401;
  reg [31:0] _RAND_402;
  reg [31:0] _RAND_403;
  reg [31:0] _RAND_404;
  reg [31:0] _RAND_405;
  reg [31:0] _RAND_406;
  reg [31:0] _RAND_407;
  reg [31:0] _RAND_408;
  reg [31:0] _RAND_409;
  reg [31:0] _RAND_410;
  reg [31:0] _RAND_411;
  reg [63:0] _RAND_412;
  reg [31:0] _RAND_413;
  reg [31:0] _RAND_414;
  reg [31:0] _RAND_415;
  reg [31:0] _RAND_416;
  reg [31:0] _RAND_417;
  reg [31:0] _RAND_418;
  reg [31:0] _RAND_419;
  reg [31:0] _RAND_420;
  reg [31:0] _RAND_421;
  reg [31:0] _RAND_422;
  reg [31:0] _RAND_423;
  reg [31:0] _RAND_424;
  reg [31:0] _RAND_425;
  reg [31:0] _RAND_426;
  reg [31:0] _RAND_427;
  reg [31:0] _RAND_428;
  reg [31:0] _RAND_429;
  reg [31:0] _RAND_430;
  reg [31:0] _RAND_431;
  reg [31:0] _RAND_432;
  reg [31:0] _RAND_433;
  reg [31:0] _RAND_434;
  reg [31:0] _RAND_435;
  reg [31:0] _RAND_436;
  reg [31:0] _RAND_437;
  reg [31:0] _RAND_438;
  reg [31:0] _RAND_439;
  reg [31:0] _RAND_440;
  reg [31:0] _RAND_441;
  reg [31:0] _RAND_442;
  reg [31:0] _RAND_443;
  reg [31:0] _RAND_444;
  reg [31:0] _RAND_445;
  reg [31:0] _RAND_446;
  reg [31:0] _RAND_447;
  reg [31:0] _RAND_448;
  reg [31:0] _RAND_449;
  reg [31:0] _RAND_450;
  reg [31:0] _RAND_451;
  reg [31:0] _RAND_452;
  reg [31:0] _RAND_453;
  reg [31:0] _RAND_454;
  reg [63:0] _RAND_455;
  reg [31:0] _RAND_456;
  reg [31:0] _RAND_457;
  reg [31:0] _RAND_458;
  reg [31:0] _RAND_459;
  reg [31:0] _RAND_460;
  reg [31:0] _RAND_461;
  reg [31:0] _RAND_462;
  reg [31:0] _RAND_463;
  reg [31:0] _RAND_464;
  reg [31:0] _RAND_465;
  reg [31:0] _RAND_466;
  reg [31:0] _RAND_467;
  reg [31:0] _RAND_468;
  reg [31:0] _RAND_469;
  reg [31:0] _RAND_470;
  reg [31:0] _RAND_471;
  reg [31:0] _RAND_472;
  reg [31:0] _RAND_473;
  reg [31:0] _RAND_474;
  reg [31:0] _RAND_475;
  reg [31:0] _RAND_476;
  reg [31:0] _RAND_477;
  reg [31:0] _RAND_478;
  reg [31:0] _RAND_479;
  reg [31:0] _RAND_480;
  reg [31:0] _RAND_481;
  reg [31:0] _RAND_482;
  reg [31:0] _RAND_483;
  reg [31:0] _RAND_484;
  reg [31:0] _RAND_485;
  reg [31:0] _RAND_486;
  reg [31:0] _RAND_487;
  reg [31:0] _RAND_488;
  reg [31:0] _RAND_489;
  reg [31:0] _RAND_490;
  reg [63:0] _RAND_491;
  reg [31:0] _RAND_492;
  reg [31:0] _RAND_493;
  reg [31:0] _RAND_494;
  reg [31:0] _RAND_495;
  reg [31:0] _RAND_496;
  reg [31:0] _RAND_497;
  reg [31:0] _RAND_498;
  reg [31:0] _RAND_499;
  reg [31:0] _RAND_500;
  reg [31:0] _RAND_501;
  reg [31:0] _RAND_502;
  reg [31:0] _RAND_503;
  reg [31:0] _RAND_504;
  reg [31:0] _RAND_505;
  reg [31:0] _RAND_506;
  reg [31:0] _RAND_507;
  reg [31:0] _RAND_508;
  reg [31:0] _RAND_509;
  reg [31:0] _RAND_510;
  reg [31:0] _RAND_511;
  reg [31:0] _RAND_512;
  reg [31:0] _RAND_513;
  reg [31:0] _RAND_514;
  reg [31:0] _RAND_515;
  reg [31:0] _RAND_516;
  reg [31:0] _RAND_517;
  reg [31:0] _RAND_518;
  reg [31:0] _RAND_519;
  reg [31:0] _RAND_520;
  reg [31:0] _RAND_521;
  reg [31:0] _RAND_522;
  reg [31:0] _RAND_523;
  reg [31:0] _RAND_524;
  reg [31:0] _RAND_525;
  reg [31:0] _RAND_526;
  reg [31:0] _RAND_527;
  reg [31:0] _RAND_528;
  reg [31:0] _RAND_529;
  reg [31:0] _RAND_530;
  reg [31:0] _RAND_531;
  reg [31:0] _RAND_532;
  reg [31:0] _RAND_533;
  reg [63:0] _RAND_534;
  reg [31:0] _RAND_535;
  reg [31:0] _RAND_536;
  reg [31:0] _RAND_537;
  reg [31:0] _RAND_538;
  reg [31:0] _RAND_539;
  reg [31:0] _RAND_540;
  reg [31:0] _RAND_541;
  reg [31:0] _RAND_542;
  reg [31:0] _RAND_543;
  reg [31:0] _RAND_544;
  reg [31:0] _RAND_545;
  reg [31:0] _RAND_546;
  reg [31:0] _RAND_547;
  reg [31:0] _RAND_548;
  reg [31:0] _RAND_549;
  reg [31:0] _RAND_550;
  reg [31:0] _RAND_551;
  reg [31:0] _RAND_552;
  reg [31:0] _RAND_553;
  reg [31:0] _RAND_554;
  reg [31:0] _RAND_555;
  reg [31:0] _RAND_556;
  reg [31:0] _RAND_557;
  reg [31:0] _RAND_558;
  reg [31:0] _RAND_559;
  reg [31:0] _RAND_560;
  reg [31:0] _RAND_561;
  reg [31:0] _RAND_562;
  reg [31:0] _RAND_563;
  reg [31:0] _RAND_564;
  reg [31:0] _RAND_565;
  reg [31:0] _RAND_566;
  reg [31:0] _RAND_567;
  reg [31:0] _RAND_568;
`endif // RANDOMIZE_REG_INIT
  reg [64:0] ram_data [0:4]; // @[util.scala 464:20]
  wire  ram_data__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_data__T_64_addr; // @[util.scala 464:20]
  wire [64:0] ram_data__T_64_data; // @[util.scala 464:20]
  wire [64:0] ram_data__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_data__T_53_addr; // @[util.scala 464:20]
  wire  ram_data__T_53_mask; // @[util.scala 464:20]
  wire  ram_data__T_53_en; // @[util.scala 464:20]
  reg  ram_predicated [0:4]; // @[util.scala 464:20]
  wire  ram_predicated__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_predicated__T_64_addr; // @[util.scala 464:20]
  wire  ram_predicated__T_64_data; // @[util.scala 464:20]
  wire  ram_predicated__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_predicated__T_53_addr; // @[util.scala 464:20]
  wire  ram_predicated__T_53_mask; // @[util.scala 464:20]
  wire  ram_predicated__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_valid [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_valid__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_valid__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_valid__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_valid__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_valid__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_valid__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_valid__T_53_en; // @[util.scala 464:20]
  reg [6:0] ram_fflags_bits_uop_uopc [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uopc__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_uopc__T_64_addr; // @[util.scala 464:20]
  wire [6:0] ram_fflags_bits_uop_uopc__T_64_data; // @[util.scala 464:20]
  wire [6:0] ram_fflags_bits_uop_uopc__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_uopc__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uopc__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uopc__T_53_en; // @[util.scala 464:20]
  reg [31:0] ram_fflags_bits_uop_inst [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_inst__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_inst__T_64_addr; // @[util.scala 464:20]
  wire [31:0] ram_fflags_bits_uop_inst__T_64_data; // @[util.scala 464:20]
  wire [31:0] ram_fflags_bits_uop_inst__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_inst__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_inst__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_inst__T_53_en; // @[util.scala 464:20]
  reg [31:0] ram_fflags_bits_uop_debug_inst [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_inst__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_inst__T_64_addr; // @[util.scala 464:20]
  wire [31:0] ram_fflags_bits_uop_debug_inst__T_64_data; // @[util.scala 464:20]
  wire [31:0] ram_fflags_bits_uop_debug_inst__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_inst__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_inst__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_inst__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_rvc [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_rvc__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_rvc__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_rvc__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_rvc__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_rvc__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_rvc__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_rvc__T_53_en; // @[util.scala 464:20]
  reg [39:0] ram_fflags_bits_uop_debug_pc [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_pc__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_pc__T_64_addr; // @[util.scala 464:20]
  wire [39:0] ram_fflags_bits_uop_debug_pc__T_64_data; // @[util.scala 464:20]
  wire [39:0] ram_fflags_bits_uop_debug_pc__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_pc__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_pc__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_pc__T_53_en; // @[util.scala 464:20]
  reg [2:0] ram_fflags_bits_uop_iq_type [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iq_type__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iq_type__T_64_addr; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iq_type__T_64_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iq_type__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iq_type__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iq_type__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iq_type__T_53_en; // @[util.scala 464:20]
  reg [9:0] ram_fflags_bits_uop_fu_code [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fu_code__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_fu_code__T_64_addr; // @[util.scala 464:20]
  wire [9:0] ram_fflags_bits_uop_fu_code__T_64_data; // @[util.scala 464:20]
  wire [9:0] ram_fflags_bits_uop_fu_code__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_fu_code__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fu_code__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fu_code__T_53_en; // @[util.scala 464:20]
  reg [3:0] ram_fflags_bits_uop_ctrl_br_type [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_br_type__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_br_type__T_64_addr; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ctrl_br_type__T_64_data; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ctrl_br_type__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_br_type__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_br_type__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_br_type__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_ctrl_op1_sel [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op1_sel__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op1_sel__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_ctrl_op1_sel__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_ctrl_op1_sel__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op1_sel__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op1_sel__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op1_sel__T_53_en; // @[util.scala 464:20]
  reg [2:0] ram_fflags_bits_uop_ctrl_op2_sel [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op2_sel__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op2_sel__T_64_addr; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op2_sel__T_64_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op2_sel__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op2_sel__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op2_sel__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op2_sel__T_53_en; // @[util.scala 464:20]
  reg [2:0] ram_fflags_bits_uop_ctrl_imm_sel [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_imm_sel__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_imm_sel__T_64_addr; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_imm_sel__T_64_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_imm_sel__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_imm_sel__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_imm_sel__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_imm_sel__T_53_en; // @[util.scala 464:20]
  reg [3:0] ram_fflags_bits_uop_ctrl_op_fcn [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op_fcn__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op_fcn__T_64_addr; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ctrl_op_fcn__T_64_data; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ctrl_op_fcn__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_op_fcn__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op_fcn__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_op_fcn__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_ctrl_fcn_dw [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_fcn_dw__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_fcn_dw__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_fcn_dw__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_fcn_dw__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_fcn_dw__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_fcn_dw__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_fcn_dw__T_53_en; // @[util.scala 464:20]
  reg [2:0] ram_fflags_bits_uop_ctrl_csr_cmd [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_csr_cmd__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_csr_cmd__T_64_addr; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_csr_cmd__T_64_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_csr_cmd__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_csr_cmd__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_csr_cmd__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_csr_cmd__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_ctrl_is_load [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_load__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_is_load__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_load__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_load__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_is_load__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_load__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_load__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_ctrl_is_sta [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_sta__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_is_sta__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_sta__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_sta__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_is_sta__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_sta__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_sta__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_ctrl_is_std [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_std__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_is_std__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_std__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_std__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ctrl_is_std__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_std__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ctrl_is_std__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_iw_state [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_state__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iw_state__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_iw_state__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_iw_state__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iw_state__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_state__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_state__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_iw_p1_poisoned [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p1_poisoned__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iw_p1_poisoned__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p1_poisoned__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p1_poisoned__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iw_p1_poisoned__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p1_poisoned__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p1_poisoned__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_iw_p2_poisoned [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p2_poisoned__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iw_p2_poisoned__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p2_poisoned__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p2_poisoned__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_iw_p2_poisoned__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p2_poisoned__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_iw_p2_poisoned__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_br [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_br__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_br__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_br__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_br__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_br__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_br__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_br__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_jalr [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jalr__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_jalr__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jalr__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jalr__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_jalr__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jalr__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jalr__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_jal [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jal__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_jal__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jal__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jal__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_jal__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jal__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_jal__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_sfb [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sfb__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_sfb__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sfb__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sfb__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_sfb__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sfb__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sfb__T_53_en; // @[util.scala 464:20]
  reg [7:0] ram_fflags_bits_uop_br_mask [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_br_mask__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_br_mask__T_64_addr; // @[util.scala 464:20]
  wire [7:0] ram_fflags_bits_uop_br_mask__T_64_data; // @[util.scala 464:20]
  wire [7:0] ram_fflags_bits_uop_br_mask__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_br_mask__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_br_mask__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_br_mask__T_53_en; // @[util.scala 464:20]
  reg [2:0] ram_fflags_bits_uop_br_tag [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_br_tag__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_br_tag__T_64_addr; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_br_tag__T_64_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_br_tag__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_br_tag__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_br_tag__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_br_tag__T_53_en; // @[util.scala 464:20]
  reg [3:0] ram_fflags_bits_uop_ftq_idx [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ftq_idx__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ftq_idx__T_64_addr; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ftq_idx__T_64_data; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ftq_idx__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ftq_idx__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ftq_idx__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ftq_idx__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_edge_inst [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_edge_inst__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_edge_inst__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_edge_inst__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_edge_inst__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_edge_inst__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_edge_inst__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_edge_inst__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_pc_lob [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_pc_lob__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_pc_lob__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_pc_lob__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_pc_lob__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_pc_lob__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_pc_lob__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_pc_lob__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_taken [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_taken__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_taken__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_taken__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_taken__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_taken__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_taken__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_taken__T_53_en; // @[util.scala 464:20]
  reg [19:0] ram_fflags_bits_uop_imm_packed [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_imm_packed__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_imm_packed__T_64_addr; // @[util.scala 464:20]
  wire [19:0] ram_fflags_bits_uop_imm_packed__T_64_data; // @[util.scala 464:20]
  wire [19:0] ram_fflags_bits_uop_imm_packed__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_imm_packed__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_imm_packed__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_imm_packed__T_53_en; // @[util.scala 464:20]
  reg [11:0] ram_fflags_bits_uop_csr_addr [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_csr_addr__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_csr_addr__T_64_addr; // @[util.scala 464:20]
  wire [11:0] ram_fflags_bits_uop_csr_addr__T_64_data; // @[util.scala 464:20]
  wire [11:0] ram_fflags_bits_uop_csr_addr__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_csr_addr__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_csr_addr__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_csr_addr__T_53_en; // @[util.scala 464:20]
  reg [4:0] ram_fflags_bits_uop_rob_idx [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_rob_idx__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_rob_idx__T_64_addr; // @[util.scala 464:20]
  wire [4:0] ram_fflags_bits_uop_rob_idx__T_64_data; // @[util.scala 464:20]
  wire [4:0] ram_fflags_bits_uop_rob_idx__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_rob_idx__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_rob_idx__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_rob_idx__T_53_en; // @[util.scala 464:20]
  reg [2:0] ram_fflags_bits_uop_ldq_idx [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldq_idx__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldq_idx__T_64_addr; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldq_idx__T_64_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldq_idx__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldq_idx__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldq_idx__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldq_idx__T_53_en; // @[util.scala 464:20]
  reg [2:0] ram_fflags_bits_uop_stq_idx [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_stq_idx__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_stq_idx__T_64_addr; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_stq_idx__T_64_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_stq_idx__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_stq_idx__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_stq_idx__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_stq_idx__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_rxq_idx [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_rxq_idx__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_rxq_idx__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_rxq_idx__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_rxq_idx__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_rxq_idx__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_rxq_idx__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_rxq_idx__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_pdst [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_pdst__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_pdst__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_pdst__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_pdst__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_pdst__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_pdst__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_pdst__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_prs1 [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs1__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_prs1__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_prs1__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs1__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_prs2 [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs2__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_prs2__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_prs2__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs2__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_prs3 [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs3__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_prs3__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_prs3__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs3__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3__T_53_en; // @[util.scala 464:20]
  reg [3:0] ram_fflags_bits_uop_ppred [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ppred__T_64_addr; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ppred__T_64_data; // @[util.scala 464:20]
  wire [3:0] ram_fflags_bits_uop_ppred__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ppred__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_prs1_busy [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1_busy__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs1_busy__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1_busy__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1_busy__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs1_busy__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1_busy__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs1_busy__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_prs2_busy [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2_busy__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs2_busy__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2_busy__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2_busy__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs2_busy__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2_busy__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs2_busy__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_prs3_busy [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3_busy__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs3_busy__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3_busy__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3_busy__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_prs3_busy__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3_busy__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_prs3_busy__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_ppred_busy [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred_busy__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ppred_busy__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred_busy__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred_busy__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ppred_busy__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred_busy__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ppred_busy__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_stale_pdst [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_stale_pdst__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_stale_pdst__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_stale_pdst__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_stale_pdst__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_stale_pdst__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_stale_pdst__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_stale_pdst__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_exception [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exception__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_exception__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exception__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exception__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_exception__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exception__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exception__T_53_en; // @[util.scala 464:20]
  reg [63:0] ram_fflags_bits_uop_exc_cause [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exc_cause__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_exc_cause__T_64_addr; // @[util.scala 464:20]
  wire [63:0] ram_fflags_bits_uop_exc_cause__T_64_data; // @[util.scala 464:20]
  wire [63:0] ram_fflags_bits_uop_exc_cause__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_exc_cause__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exc_cause__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_exc_cause__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_bypassable [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bypassable__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_bypassable__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bypassable__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bypassable__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_bypassable__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bypassable__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bypassable__T_53_en; // @[util.scala 464:20]
  reg [4:0] ram_fflags_bits_uop_mem_cmd [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_cmd__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_mem_cmd__T_64_addr; // @[util.scala 464:20]
  wire [4:0] ram_fflags_bits_uop_mem_cmd__T_64_data; // @[util.scala 464:20]
  wire [4:0] ram_fflags_bits_uop_mem_cmd__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_mem_cmd__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_cmd__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_cmd__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_mem_size [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_size__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_mem_size__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_mem_size__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_mem_size__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_mem_size__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_size__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_size__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_mem_signed [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_signed__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_mem_signed__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_signed__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_signed__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_mem_signed__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_signed__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_mem_signed__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_fence [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fence__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_fence__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fence__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fence__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_fence__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fence__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fence__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_fencei [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fencei__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_fencei__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fencei__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fencei__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_fencei__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fencei__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_fencei__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_amo [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_amo__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_amo__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_amo__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_amo__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_amo__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_amo__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_amo__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_uses_ldq [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_ldq__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_uses_ldq__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_ldq__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_ldq__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_uses_ldq__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_ldq__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_ldq__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_uses_stq [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_stq__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_uses_stq__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_stq__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_stq__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_uses_stq__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_stq__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_uses_stq__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_sys_pc2epc [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sys_pc2epc__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_sys_pc2epc__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sys_pc2epc__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sys_pc2epc__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_sys_pc2epc__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sys_pc2epc__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_sys_pc2epc__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_is_unique [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_unique__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_unique__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_unique__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_unique__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_is_unique__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_unique__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_is_unique__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_flush_on_commit [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_flush_on_commit__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_flush_on_commit__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_flush_on_commit__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_flush_on_commit__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_flush_on_commit__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_flush_on_commit__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_flush_on_commit__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_ldst_is_rs1 [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_is_rs1__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldst_is_rs1__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_is_rs1__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_is_rs1__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldst_is_rs1__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_is_rs1__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_is_rs1__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_ldst [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldst__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_ldst__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_ldst__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldst__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_lrs1 [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs1__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs1__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_lrs1__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_lrs1__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs1__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs1__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs1__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_lrs2 [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs2__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs2__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_lrs2__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_lrs2__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs2__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs2__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs2__T_53_en; // @[util.scala 464:20]
  reg [5:0] ram_fflags_bits_uop_lrs3 [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs3__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs3__T_64_addr; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_lrs3__T_64_data; // @[util.scala 464:20]
  wire [5:0] ram_fflags_bits_uop_lrs3__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs3__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs3__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs3__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_ldst_val [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_val__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldst_val__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_val__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_val__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_ldst_val__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_val__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_ldst_val__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_dst_rtype [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_dst_rtype__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_dst_rtype__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_dst_rtype__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_dst_rtype__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_dst_rtype__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_dst_rtype__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_dst_rtype__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_lrs1_rtype [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs1_rtype__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs1_rtype__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_lrs1_rtype__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_lrs1_rtype__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs1_rtype__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs1_rtype__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs1_rtype__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_lrs2_rtype [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs2_rtype__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs2_rtype__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_lrs2_rtype__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_lrs2_rtype__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_lrs2_rtype__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs2_rtype__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_lrs2_rtype__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_frs3_en [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_frs3_en__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_frs3_en__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_frs3_en__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_frs3_en__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_frs3_en__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_frs3_en__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_frs3_en__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_fp_val [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_val__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_fp_val__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_val__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_val__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_fp_val__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_val__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_val__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_fp_single [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_single__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_fp_single__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_single__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_single__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_fp_single__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_single__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_fp_single__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_xcpt_pf_if [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_pf_if__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_xcpt_pf_if__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_pf_if__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_pf_if__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_xcpt_pf_if__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_pf_if__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_pf_if__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_xcpt_ae_if [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ae_if__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_xcpt_ae_if__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ae_if__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ae_if__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_xcpt_ae_if__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ae_if__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ae_if__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_xcpt_ma_if [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ma_if__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_xcpt_ma_if__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ma_if__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ma_if__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_xcpt_ma_if__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ma_if__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_xcpt_ma_if__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_bp_debug_if [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_debug_if__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_bp_debug_if__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_debug_if__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_debug_if__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_bp_debug_if__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_debug_if__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_debug_if__T_53_en; // @[util.scala 464:20]
  reg  ram_fflags_bits_uop_bp_xcpt_if [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_xcpt_if__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_bp_xcpt_if__T_64_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_xcpt_if__T_64_data; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_xcpt_if__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_bp_xcpt_if__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_xcpt_if__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_bp_xcpt_if__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_debug_fsrc [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_fsrc__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_fsrc__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_debug_fsrc__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_debug_fsrc__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_fsrc__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_fsrc__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_fsrc__T_53_en; // @[util.scala 464:20]
  reg [1:0] ram_fflags_bits_uop_debug_tsrc [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_tsrc__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_tsrc__T_64_addr; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_debug_tsrc__T_64_data; // @[util.scala 464:20]
  wire [1:0] ram_fflags_bits_uop_debug_tsrc__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_uop_debug_tsrc__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_tsrc__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_uop_debug_tsrc__T_53_en; // @[util.scala 464:20]
  reg [4:0] ram_fflags_bits_flags [0:4]; // @[util.scala 464:20]
  wire  ram_fflags_bits_flags__T_64_en; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_flags__T_64_addr; // @[util.scala 464:20]
  wire [4:0] ram_fflags_bits_flags__T_64_data; // @[util.scala 464:20]
  wire [4:0] ram_fflags_bits_flags__T_53_data; // @[util.scala 464:20]
  wire [2:0] ram_fflags_bits_flags__T_53_addr; // @[util.scala 464:20]
  wire  ram_fflags_bits_flags__T_53_mask; // @[util.scala 464:20]
  wire  ram_fflags_bits_flags__T_53_en; // @[util.scala 464:20]
  reg  valids_0; // @[util.scala 465:24]
  reg  valids_1; // @[util.scala 465:24]
  reg  valids_2; // @[util.scala 465:24]
  reg  valids_3; // @[util.scala 465:24]
  reg  valids_4; // @[util.scala 465:24]
  reg [6:0] uops_0_uopc; // @[util.scala 466:20]
  reg [31:0] uops_0_inst; // @[util.scala 466:20]
  reg [31:0] uops_0_debug_inst; // @[util.scala 466:20]
  reg  uops_0_is_rvc; // @[util.scala 466:20]
  reg [39:0] uops_0_debug_pc; // @[util.scala 466:20]
  reg [2:0] uops_0_iq_type; // @[util.scala 466:20]
  reg [9:0] uops_0_fu_code; // @[util.scala 466:20]
  reg [3:0] uops_0_ctrl_br_type; // @[util.scala 466:20]
  reg [1:0] uops_0_ctrl_op1_sel; // @[util.scala 466:20]
  reg [2:0] uops_0_ctrl_op2_sel; // @[util.scala 466:20]
  reg [2:0] uops_0_ctrl_imm_sel; // @[util.scala 466:20]
  reg [3:0] uops_0_ctrl_op_fcn; // @[util.scala 466:20]
  reg  uops_0_ctrl_fcn_dw; // @[util.scala 466:20]
  reg [2:0] uops_0_ctrl_csr_cmd; // @[util.scala 466:20]
  reg  uops_0_ctrl_is_load; // @[util.scala 466:20]
  reg  uops_0_ctrl_is_sta; // @[util.scala 466:20]
  reg  uops_0_ctrl_is_std; // @[util.scala 466:20]
  reg [1:0] uops_0_iw_state; // @[util.scala 466:20]
  reg  uops_0_iw_p1_poisoned; // @[util.scala 466:20]
  reg  uops_0_iw_p2_poisoned; // @[util.scala 466:20]
  reg  uops_0_is_br; // @[util.scala 466:20]
  reg  uops_0_is_jalr; // @[util.scala 466:20]
  reg  uops_0_is_jal; // @[util.scala 466:20]
  reg  uops_0_is_sfb; // @[util.scala 466:20]
  reg [7:0] uops_0_br_mask; // @[util.scala 466:20]
  reg [2:0] uops_0_br_tag; // @[util.scala 466:20]
  reg [3:0] uops_0_ftq_idx; // @[util.scala 466:20]
  reg  uops_0_edge_inst; // @[util.scala 466:20]
  reg [5:0] uops_0_pc_lob; // @[util.scala 466:20]
  reg  uops_0_taken; // @[util.scala 466:20]
  reg [19:0] uops_0_imm_packed; // @[util.scala 466:20]
  reg [11:0] uops_0_csr_addr; // @[util.scala 466:20]
  reg [4:0] uops_0_rob_idx; // @[util.scala 466:20]
  reg [2:0] uops_0_ldq_idx; // @[util.scala 466:20]
  reg [2:0] uops_0_stq_idx; // @[util.scala 466:20]
  reg [1:0] uops_0_rxq_idx; // @[util.scala 466:20]
  reg [5:0] uops_0_pdst; // @[util.scala 466:20]
  reg [5:0] uops_0_prs1; // @[util.scala 466:20]
  reg [5:0] uops_0_prs2; // @[util.scala 466:20]
  reg [5:0] uops_0_prs3; // @[util.scala 466:20]
  reg [3:0] uops_0_ppred; // @[util.scala 466:20]
  reg  uops_0_prs1_busy; // @[util.scala 466:20]
  reg  uops_0_prs2_busy; // @[util.scala 466:20]
  reg  uops_0_prs3_busy; // @[util.scala 466:20]
  reg  uops_0_ppred_busy; // @[util.scala 466:20]
  reg [5:0] uops_0_stale_pdst; // @[util.scala 466:20]
  reg  uops_0_exception; // @[util.scala 466:20]
  reg [63:0] uops_0_exc_cause; // @[util.scala 466:20]
  reg  uops_0_bypassable; // @[util.scala 466:20]
  reg [4:0] uops_0_mem_cmd; // @[util.scala 466:20]
  reg [1:0] uops_0_mem_size; // @[util.scala 466:20]
  reg  uops_0_mem_signed; // @[util.scala 466:20]
  reg  uops_0_is_fence; // @[util.scala 466:20]
  reg  uops_0_is_fencei; // @[util.scala 466:20]
  reg  uops_0_is_amo; // @[util.scala 466:20]
  reg  uops_0_uses_ldq; // @[util.scala 466:20]
  reg  uops_0_uses_stq; // @[util.scala 466:20]
  reg  uops_0_is_sys_pc2epc; // @[util.scala 466:20]
  reg  uops_0_is_unique; // @[util.scala 466:20]
  reg  uops_0_flush_on_commit; // @[util.scala 466:20]
  reg  uops_0_ldst_is_rs1; // @[util.scala 466:20]
  reg [5:0] uops_0_ldst; // @[util.scala 466:20]
  reg [5:0] uops_0_lrs1; // @[util.scala 466:20]
  reg [5:0] uops_0_lrs2; // @[util.scala 466:20]
  reg [5:0] uops_0_lrs3; // @[util.scala 466:20]
  reg  uops_0_ldst_val; // @[util.scala 466:20]
  reg [1:0] uops_0_dst_rtype; // @[util.scala 466:20]
  reg [1:0] uops_0_lrs1_rtype; // @[util.scala 466:20]
  reg [1:0] uops_0_lrs2_rtype; // @[util.scala 466:20]
  reg  uops_0_frs3_en; // @[util.scala 466:20]
  reg  uops_0_fp_val; // @[util.scala 466:20]
  reg  uops_0_fp_single; // @[util.scala 466:20]
  reg  uops_0_xcpt_pf_if; // @[util.scala 466:20]
  reg  uops_0_xcpt_ae_if; // @[util.scala 466:20]
  reg  uops_0_xcpt_ma_if; // @[util.scala 466:20]
  reg  uops_0_bp_debug_if; // @[util.scala 466:20]
  reg  uops_0_bp_xcpt_if; // @[util.scala 466:20]
  reg [1:0] uops_0_debug_fsrc; // @[util.scala 466:20]
  reg [1:0] uops_0_debug_tsrc; // @[util.scala 466:20]
  reg [6:0] uops_1_uopc; // @[util.scala 466:20]
  reg [31:0] uops_1_inst; // @[util.scala 466:20]
  reg [31:0] uops_1_debug_inst; // @[util.scala 466:20]
  reg  uops_1_is_rvc; // @[util.scala 466:20]
  reg [39:0] uops_1_debug_pc; // @[util.scala 466:20]
  reg [2:0] uops_1_iq_type; // @[util.scala 466:20]
  reg [9:0] uops_1_fu_code; // @[util.scala 466:20]
  reg [3:0] uops_1_ctrl_br_type; // @[util.scala 466:20]
  reg [1:0] uops_1_ctrl_op1_sel; // @[util.scala 466:20]
  reg [2:0] uops_1_ctrl_op2_sel; // @[util.scala 466:20]
  reg [2:0] uops_1_ctrl_imm_sel; // @[util.scala 466:20]
  reg [3:0] uops_1_ctrl_op_fcn; // @[util.scala 466:20]
  reg  uops_1_ctrl_fcn_dw; // @[util.scala 466:20]
  reg [2:0] uops_1_ctrl_csr_cmd; // @[util.scala 466:20]
  reg  uops_1_ctrl_is_load; // @[util.scala 466:20]
  reg  uops_1_ctrl_is_sta; // @[util.scala 466:20]
  reg  uops_1_ctrl_is_std; // @[util.scala 466:20]
  reg [1:0] uops_1_iw_state; // @[util.scala 466:20]
  reg  uops_1_iw_p1_poisoned; // @[util.scala 466:20]
  reg  uops_1_iw_p2_poisoned; // @[util.scala 466:20]
  reg  uops_1_is_br; // @[util.scala 466:20]
  reg  uops_1_is_jalr; // @[util.scala 466:20]
  reg  uops_1_is_jal; // @[util.scala 466:20]
  reg  uops_1_is_sfb; // @[util.scala 466:20]
  reg [7:0] uops_1_br_mask; // @[util.scala 466:20]
  reg [2:0] uops_1_br_tag; // @[util.scala 466:20]
  reg [3:0] uops_1_ftq_idx; // @[util.scala 466:20]
  reg  uops_1_edge_inst; // @[util.scala 466:20]
  reg [5:0] uops_1_pc_lob; // @[util.scala 466:20]
  reg  uops_1_taken; // @[util.scala 466:20]
  reg [19:0] uops_1_imm_packed; // @[util.scala 466:20]
  reg [11:0] uops_1_csr_addr; // @[util.scala 466:20]
  reg [4:0] uops_1_rob_idx; // @[util.scala 466:20]
  reg [2:0] uops_1_ldq_idx; // @[util.scala 466:20]
  reg [2:0] uops_1_stq_idx; // @[util.scala 466:20]
  reg [1:0] uops_1_rxq_idx; // @[util.scala 466:20]
  reg [5:0] uops_1_pdst; // @[util.scala 466:20]
  reg [5:0] uops_1_prs1; // @[util.scala 466:20]
  reg [5:0] uops_1_prs2; // @[util.scala 466:20]
  reg [5:0] uops_1_prs3; // @[util.scala 466:20]
  reg [3:0] uops_1_ppred; // @[util.scala 466:20]
  reg  uops_1_prs1_busy; // @[util.scala 466:20]
  reg  uops_1_prs2_busy; // @[util.scala 466:20]
  reg  uops_1_prs3_busy; // @[util.scala 466:20]
  reg  uops_1_ppred_busy; // @[util.scala 466:20]
  reg [5:0] uops_1_stale_pdst; // @[util.scala 466:20]
  reg  uops_1_exception; // @[util.scala 466:20]
  reg [63:0] uops_1_exc_cause; // @[util.scala 466:20]
  reg  uops_1_bypassable; // @[util.scala 466:20]
  reg [4:0] uops_1_mem_cmd; // @[util.scala 466:20]
  reg [1:0] uops_1_mem_size; // @[util.scala 466:20]
  reg  uops_1_mem_signed; // @[util.scala 466:20]
  reg  uops_1_is_fence; // @[util.scala 466:20]
  reg  uops_1_is_fencei; // @[util.scala 466:20]
  reg  uops_1_is_amo; // @[util.scala 466:20]
  reg  uops_1_uses_ldq; // @[util.scala 466:20]
  reg  uops_1_uses_stq; // @[util.scala 466:20]
  reg  uops_1_is_sys_pc2epc; // @[util.scala 466:20]
  reg  uops_1_is_unique; // @[util.scala 466:20]
  reg  uops_1_flush_on_commit; // @[util.scala 466:20]
  reg  uops_1_ldst_is_rs1; // @[util.scala 466:20]
  reg [5:0] uops_1_ldst; // @[util.scala 466:20]
  reg [5:0] uops_1_lrs1; // @[util.scala 466:20]
  reg [5:0] uops_1_lrs2; // @[util.scala 466:20]
  reg [5:0] uops_1_lrs3; // @[util.scala 466:20]
  reg  uops_1_ldst_val; // @[util.scala 466:20]
  reg [1:0] uops_1_dst_rtype; // @[util.scala 466:20]
  reg [1:0] uops_1_lrs1_rtype; // @[util.scala 466:20]
  reg [1:0] uops_1_lrs2_rtype; // @[util.scala 466:20]
  reg  uops_1_frs3_en; // @[util.scala 466:20]
  reg  uops_1_fp_val; // @[util.scala 466:20]
  reg  uops_1_fp_single; // @[util.scala 466:20]
  reg  uops_1_xcpt_pf_if; // @[util.scala 466:20]
  reg  uops_1_xcpt_ae_if; // @[util.scala 466:20]
  reg  uops_1_xcpt_ma_if; // @[util.scala 466:20]
  reg  uops_1_bp_debug_if; // @[util.scala 466:20]
  reg  uops_1_bp_xcpt_if; // @[util.scala 466:20]
  reg [1:0] uops_1_debug_fsrc; // @[util.scala 466:20]
  reg [1:0] uops_1_debug_tsrc; // @[util.scala 466:20]
  reg [6:0] uops_2_uopc; // @[util.scala 466:20]
  reg [31:0] uops_2_inst; // @[util.scala 466:20]
  reg [31:0] uops_2_debug_inst; // @[util.scala 466:20]
  reg  uops_2_is_rvc; // @[util.scala 466:20]
  reg [39:0] uops_2_debug_pc; // @[util.scala 466:20]
  reg [2:0] uops_2_iq_type; // @[util.scala 466:20]
  reg [9:0] uops_2_fu_code; // @[util.scala 466:20]
  reg [3:0] uops_2_ctrl_br_type; // @[util.scala 466:20]
  reg [1:0] uops_2_ctrl_op1_sel; // @[util.scala 466:20]
  reg [2:0] uops_2_ctrl_op2_sel; // @[util.scala 466:20]
  reg [2:0] uops_2_ctrl_imm_sel; // @[util.scala 466:20]
  reg [3:0] uops_2_ctrl_op_fcn; // @[util.scala 466:20]
  reg  uops_2_ctrl_fcn_dw; // @[util.scala 466:20]
  reg [2:0] uops_2_ctrl_csr_cmd; // @[util.scala 466:20]
  reg  uops_2_ctrl_is_load; // @[util.scala 466:20]
  reg  uops_2_ctrl_is_sta; // @[util.scala 466:20]
  reg  uops_2_ctrl_is_std; // @[util.scala 466:20]
  reg [1:0] uops_2_iw_state; // @[util.scala 466:20]
  reg  uops_2_iw_p1_poisoned; // @[util.scala 466:20]
  reg  uops_2_iw_p2_poisoned; // @[util.scala 466:20]
  reg  uops_2_is_br; // @[util.scala 466:20]
  reg  uops_2_is_jalr; // @[util.scala 466:20]
  reg  uops_2_is_jal; // @[util.scala 466:20]
  reg  uops_2_is_sfb; // @[util.scala 466:20]
  reg [7:0] uops_2_br_mask; // @[util.scala 466:20]
  reg [2:0] uops_2_br_tag; // @[util.scala 466:20]
  reg [3:0] uops_2_ftq_idx; // @[util.scala 466:20]
  reg  uops_2_edge_inst; // @[util.scala 466:20]
  reg [5:0] uops_2_pc_lob; // @[util.scala 466:20]
  reg  uops_2_taken; // @[util.scala 466:20]
  reg [19:0] uops_2_imm_packed; // @[util.scala 466:20]
  reg [11:0] uops_2_csr_addr; // @[util.scala 466:20]
  reg [4:0] uops_2_rob_idx; // @[util.scala 466:20]
  reg [2:0] uops_2_ldq_idx; // @[util.scala 466:20]
  reg [2:0] uops_2_stq_idx; // @[util.scala 466:20]
  reg [1:0] uops_2_rxq_idx; // @[util.scala 466:20]
  reg [5:0] uops_2_pdst; // @[util.scala 466:20]
  reg [5:0] uops_2_prs1; // @[util.scala 466:20]
  reg [5:0] uops_2_prs2; // @[util.scala 466:20]
  reg [5:0] uops_2_prs3; // @[util.scala 466:20]
  reg [3:0] uops_2_ppred; // @[util.scala 466:20]
  reg  uops_2_prs1_busy; // @[util.scala 466:20]
  reg  uops_2_prs2_busy; // @[util.scala 466:20]
  reg  uops_2_prs3_busy; // @[util.scala 466:20]
  reg  uops_2_ppred_busy; // @[util.scala 466:20]
  reg [5:0] uops_2_stale_pdst; // @[util.scala 466:20]
  reg  uops_2_exception; // @[util.scala 466:20]
  reg [63:0] uops_2_exc_cause; // @[util.scala 466:20]
  reg  uops_2_bypassable; // @[util.scala 466:20]
  reg [4:0] uops_2_mem_cmd; // @[util.scala 466:20]
  reg [1:0] uops_2_mem_size; // @[util.scala 466:20]
  reg  uops_2_mem_signed; // @[util.scala 466:20]
  reg  uops_2_is_fence; // @[util.scala 466:20]
  reg  uops_2_is_fencei; // @[util.scala 466:20]
  reg  uops_2_is_amo; // @[util.scala 466:20]
  reg  uops_2_uses_ldq; // @[util.scala 466:20]
  reg  uops_2_uses_stq; // @[util.scala 466:20]
  reg  uops_2_is_sys_pc2epc; // @[util.scala 466:20]
  reg  uops_2_is_unique; // @[util.scala 466:20]
  reg  uops_2_flush_on_commit; // @[util.scala 466:20]
  reg  uops_2_ldst_is_rs1; // @[util.scala 466:20]
  reg [5:0] uops_2_ldst; // @[util.scala 466:20]
  reg [5:0] uops_2_lrs1; // @[util.scala 466:20]
  reg [5:0] uops_2_lrs2; // @[util.scala 466:20]
  reg [5:0] uops_2_lrs3; // @[util.scala 466:20]
  reg  uops_2_ldst_val; // @[util.scala 466:20]
  reg [1:0] uops_2_dst_rtype; // @[util.scala 466:20]
  reg [1:0] uops_2_lrs1_rtype; // @[util.scala 466:20]
  reg [1:0] uops_2_lrs2_rtype; // @[util.scala 466:20]
  reg  uops_2_frs3_en; // @[util.scala 466:20]
  reg  uops_2_fp_val; // @[util.scala 466:20]
  reg  uops_2_fp_single; // @[util.scala 466:20]
  reg  uops_2_xcpt_pf_if; // @[util.scala 466:20]
  reg  uops_2_xcpt_ae_if; // @[util.scala 466:20]
  reg  uops_2_xcpt_ma_if; // @[util.scala 466:20]
  reg  uops_2_bp_debug_if; // @[util.scala 466:20]
  reg  uops_2_bp_xcpt_if; // @[util.scala 466:20]
  reg [1:0] uops_2_debug_fsrc; // @[util.scala 466:20]
  reg [1:0] uops_2_debug_tsrc; // @[util.scala 466:20]
  reg [6:0] uops_3_uopc; // @[util.scala 466:20]
  reg [31:0] uops_3_inst; // @[util.scala 466:20]
  reg [31:0] uops_3_debug_inst; // @[util.scala 466:20]
  reg  uops_3_is_rvc; // @[util.scala 466:20]
  reg [39:0] uops_3_debug_pc; // @[util.scala 466:20]
  reg [2:0] uops_3_iq_type; // @[util.scala 466:20]
  reg [9:0] uops_3_fu_code; // @[util.scala 466:20]
  reg [3:0] uops_3_ctrl_br_type; // @[util.scala 466:20]
  reg [1:0] uops_3_ctrl_op1_sel; // @[util.scala 466:20]
  reg [2:0] uops_3_ctrl_op2_sel; // @[util.scala 466:20]
  reg [2:0] uops_3_ctrl_imm_sel; // @[util.scala 466:20]
  reg [3:0] uops_3_ctrl_op_fcn; // @[util.scala 466:20]
  reg  uops_3_ctrl_fcn_dw; // @[util.scala 466:20]
  reg [2:0] uops_3_ctrl_csr_cmd; // @[util.scala 466:20]
  reg  uops_3_ctrl_is_load; // @[util.scala 466:20]
  reg  uops_3_ctrl_is_sta; // @[util.scala 466:20]
  reg  uops_3_ctrl_is_std; // @[util.scala 466:20]
  reg [1:0] uops_3_iw_state; // @[util.scala 466:20]
  reg  uops_3_iw_p1_poisoned; // @[util.scala 466:20]
  reg  uops_3_iw_p2_poisoned; // @[util.scala 466:20]
  reg  uops_3_is_br; // @[util.scala 466:20]
  reg  uops_3_is_jalr; // @[util.scala 466:20]
  reg  uops_3_is_jal; // @[util.scala 466:20]
  reg  uops_3_is_sfb; // @[util.scala 466:20]
  reg [7:0] uops_3_br_mask; // @[util.scala 466:20]
  reg [2:0] uops_3_br_tag; // @[util.scala 466:20]
  reg [3:0] uops_3_ftq_idx; // @[util.scala 466:20]
  reg  uops_3_edge_inst; // @[util.scala 466:20]
  reg [5:0] uops_3_pc_lob; // @[util.scala 466:20]
  reg  uops_3_taken; // @[util.scala 466:20]
  reg [19:0] uops_3_imm_packed; // @[util.scala 466:20]
  reg [11:0] uops_3_csr_addr; // @[util.scala 466:20]
  reg [4:0] uops_3_rob_idx; // @[util.scala 466:20]
  reg [2:0] uops_3_ldq_idx; // @[util.scala 466:20]
  reg [2:0] uops_3_stq_idx; // @[util.scala 466:20]
  reg [1:0] uops_3_rxq_idx; // @[util.scala 466:20]
  reg [5:0] uops_3_pdst; // @[util.scala 466:20]
  reg [5:0] uops_3_prs1; // @[util.scala 466:20]
  reg [5:0] uops_3_prs2; // @[util.scala 466:20]
  reg [5:0] uops_3_prs3; // @[util.scala 466:20]
  reg [3:0] uops_3_ppred; // @[util.scala 466:20]
  reg  uops_3_prs1_busy; // @[util.scala 466:20]
  reg  uops_3_prs2_busy; // @[util.scala 466:20]
  reg  uops_3_prs3_busy; // @[util.scala 466:20]
  reg  uops_3_ppred_busy; // @[util.scala 466:20]
  reg [5:0] uops_3_stale_pdst; // @[util.scala 466:20]
  reg  uops_3_exception; // @[util.scala 466:20]
  reg [63:0] uops_3_exc_cause; // @[util.scala 466:20]
  reg  uops_3_bypassable; // @[util.scala 466:20]
  reg [4:0] uops_3_mem_cmd; // @[util.scala 466:20]
  reg [1:0] uops_3_mem_size; // @[util.scala 466:20]
  reg  uops_3_mem_signed; // @[util.scala 466:20]
  reg  uops_3_is_fence; // @[util.scala 466:20]
  reg  uops_3_is_fencei; // @[util.scala 466:20]
  reg  uops_3_is_amo; // @[util.scala 466:20]
  reg  uops_3_uses_ldq; // @[util.scala 466:20]
  reg  uops_3_uses_stq; // @[util.scala 466:20]
  reg  uops_3_is_sys_pc2epc; // @[util.scala 466:20]
  reg  uops_3_is_unique; // @[util.scala 466:20]
  reg  uops_3_flush_on_commit; // @[util.scala 466:20]
  reg  uops_3_ldst_is_rs1; // @[util.scala 466:20]
  reg [5:0] uops_3_ldst; // @[util.scala 466:20]
  reg [5:0] uops_3_lrs1; // @[util.scala 466:20]
  reg [5:0] uops_3_lrs2; // @[util.scala 466:20]
  reg [5:0] uops_3_lrs3; // @[util.scala 466:20]
  reg  uops_3_ldst_val; // @[util.scala 466:20]
  reg [1:0] uops_3_dst_rtype; // @[util.scala 466:20]
  reg [1:0] uops_3_lrs1_rtype; // @[util.scala 466:20]
  reg [1:0] uops_3_lrs2_rtype; // @[util.scala 466:20]
  reg  uops_3_frs3_en; // @[util.scala 466:20]
  reg  uops_3_fp_val; // @[util.scala 466:20]
  reg  uops_3_fp_single; // @[util.scala 466:20]
  reg  uops_3_xcpt_pf_if; // @[util.scala 466:20]
  reg  uops_3_xcpt_ae_if; // @[util.scala 466:20]
  reg  uops_3_xcpt_ma_if; // @[util.scala 466:20]
  reg  uops_3_bp_debug_if; // @[util.scala 466:20]
  reg  uops_3_bp_xcpt_if; // @[util.scala 466:20]
  reg [1:0] uops_3_debug_fsrc; // @[util.scala 466:20]
  reg [1:0] uops_3_debug_tsrc; // @[util.scala 466:20]
  reg [6:0] uops_4_uopc; // @[util.scala 466:20]
  reg [31:0] uops_4_inst; // @[util.scala 466:20]
  reg [31:0] uops_4_debug_inst; // @[util.scala 466:20]
  reg  uops_4_is_rvc; // @[util.scala 466:20]
  reg [39:0] uops_4_debug_pc; // @[util.scala 466:20]
  reg [2:0] uops_4_iq_type; // @[util.scala 466:20]
  reg [9:0] uops_4_fu_code; // @[util.scala 466:20]
  reg [3:0] uops_4_ctrl_br_type; // @[util.scala 466:20]
  reg [1:0] uops_4_ctrl_op1_sel; // @[util.scala 466:20]
  reg [2:0] uops_4_ctrl_op2_sel; // @[util.scala 466:20]
  reg [2:0] uops_4_ctrl_imm_sel; // @[util.scala 466:20]
  reg [3:0] uops_4_ctrl_op_fcn; // @[util.scala 466:20]
  reg  uops_4_ctrl_fcn_dw; // @[util.scala 466:20]
  reg [2:0] uops_4_ctrl_csr_cmd; // @[util.scala 466:20]
  reg  uops_4_ctrl_is_load; // @[util.scala 466:20]
  reg  uops_4_ctrl_is_sta; // @[util.scala 466:20]
  reg  uops_4_ctrl_is_std; // @[util.scala 466:20]
  reg [1:0] uops_4_iw_state; // @[util.scala 466:20]
  reg  uops_4_iw_p1_poisoned; // @[util.scala 466:20]
  reg  uops_4_iw_p2_poisoned; // @[util.scala 466:20]
  reg  uops_4_is_br; // @[util.scala 466:20]
  reg  uops_4_is_jalr; // @[util.scala 466:20]
  reg  uops_4_is_jal; // @[util.scala 466:20]
  reg  uops_4_is_sfb; // @[util.scala 466:20]
  reg [7:0] uops_4_br_mask; // @[util.scala 466:20]
  reg [2:0] uops_4_br_tag; // @[util.scala 466:20]
  reg [3:0] uops_4_ftq_idx; // @[util.scala 466:20]
  reg  uops_4_edge_inst; // @[util.scala 466:20]
  reg [5:0] uops_4_pc_lob; // @[util.scala 466:20]
  reg  uops_4_taken; // @[util.scala 466:20]
  reg [19:0] uops_4_imm_packed; // @[util.scala 466:20]
  reg [11:0] uops_4_csr_addr; // @[util.scala 466:20]
  reg [4:0] uops_4_rob_idx; // @[util.scala 466:20]
  reg [2:0] uops_4_ldq_idx; // @[util.scala 466:20]
  reg [2:0] uops_4_stq_idx; // @[util.scala 466:20]
  reg [1:0] uops_4_rxq_idx; // @[util.scala 466:20]
  reg [5:0] uops_4_pdst; // @[util.scala 466:20]
  reg [5:0] uops_4_prs1; // @[util.scala 466:20]
  reg [5:0] uops_4_prs2; // @[util.scala 466:20]
  reg [5:0] uops_4_prs3; // @[util.scala 466:20]
  reg [3:0] uops_4_ppred; // @[util.scala 466:20]
  reg  uops_4_prs1_busy; // @[util.scala 466:20]
  reg  uops_4_prs2_busy; // @[util.scala 466:20]
  reg  uops_4_prs3_busy; // @[util.scala 466:20]
  reg  uops_4_ppred_busy; // @[util.scala 466:20]
  reg [5:0] uops_4_stale_pdst; // @[util.scala 466:20]
  reg  uops_4_exception; // @[util.scala 466:20]
  reg [63:0] uops_4_exc_cause; // @[util.scala 466:20]
  reg  uops_4_bypassable; // @[util.scala 466:20]
  reg [4:0] uops_4_mem_cmd; // @[util.scala 466:20]
  reg [1:0] uops_4_mem_size; // @[util.scala 466:20]
  reg  uops_4_mem_signed; // @[util.scala 466:20]
  reg  uops_4_is_fence; // @[util.scala 466:20]
  reg  uops_4_is_fencei; // @[util.scala 466:20]
  reg  uops_4_is_amo; // @[util.scala 466:20]
  reg  uops_4_uses_ldq; // @[util.scala 466:20]
  reg  uops_4_uses_stq; // @[util.scala 466:20]
  reg  uops_4_is_sys_pc2epc; // @[util.scala 466:20]
  reg  uops_4_is_unique; // @[util.scala 466:20]
  reg  uops_4_flush_on_commit; // @[util.scala 466:20]
  reg  uops_4_ldst_is_rs1; // @[util.scala 466:20]
  reg [5:0] uops_4_ldst; // @[util.scala 466:20]
  reg [5:0] uops_4_lrs1; // @[util.scala 466:20]
  reg [5:0] uops_4_lrs2; // @[util.scala 466:20]
  reg [5:0] uops_4_lrs3; // @[util.scala 466:20]
  reg  uops_4_ldst_val; // @[util.scala 466:20]
  reg [1:0] uops_4_dst_rtype; // @[util.scala 466:20]
  reg [1:0] uops_4_lrs1_rtype; // @[util.scala 466:20]
  reg [1:0] uops_4_lrs2_rtype; // @[util.scala 466:20]
  reg  uops_4_frs3_en; // @[util.scala 466:20]
  reg  uops_4_fp_val; // @[util.scala 466:20]
  reg  uops_4_fp_single; // @[util.scala 466:20]
  reg  uops_4_xcpt_pf_if; // @[util.scala 466:20]
  reg  uops_4_xcpt_ae_if; // @[util.scala 466:20]
  reg  uops_4_xcpt_ma_if; // @[util.scala 466:20]
  reg  uops_4_bp_debug_if; // @[util.scala 466:20]
  reg  uops_4_bp_xcpt_if; // @[util.scala 466:20]
  reg [1:0] uops_4_debug_fsrc; // @[util.scala 466:20]
  reg [1:0] uops_4_debug_tsrc; // @[util.scala 466:20]
  reg [2:0] value; // @[Counter.scala 29:33]
  reg [2:0] value_1; // @[Counter.scala 29:33]
  reg  maybe_full; // @[util.scala 470:27]
  wire  ptr_match = value == value_1; // @[util.scala 472:33]
  wire  full = ptr_match & maybe_full; // @[util.scala 474:24]
  wire  _T_3 = io_enq_ready & io_enq_valid; // @[Decoupled.scala 40:37]
  wire  _GEN_1 = 3'h1 == value_1 ? valids_1 : valids_0; // @[util.scala 476:{42,42}]
  wire  _GEN_2 = 3'h2 == value_1 ? valids_2 : _GEN_1; // @[util.scala 476:{42,42}]
  wire  _GEN_3 = 3'h3 == value_1 ? valids_3 : _GEN_2; // @[util.scala 476:{42,42}]
  wire  _GEN_4 = 3'h4 == value_1 ? valids_4 : _GEN_3; // @[util.scala 476:{42,42}]
  wire  _T_6 = ~io_empty; // @[util.scala 476:69]
  wire  _T_7 = (io_deq_ready | ~_GEN_4) & ~io_empty; // @[util.scala 476:66]
  wire [7:0] _T_8 = io_brupdate_b1_mispredict_mask & uops_0_br_mask; // @[util.scala 118:51]
  wire  _T_9 = _T_8 != 8'h0; // @[util.scala 118:59]
  wire  _T_13 = ~io_flush; // @[util.scala 481:72]
  wire [7:0] _T_15 = ~io_brupdate_b1_resolve_mask; // @[util.scala 89:23]
  wire [7:0] _T_16 = uops_0_br_mask & _T_15; // @[util.scala 89:21]
  wire [7:0] _GEN_5 = valids_0 ? _T_16 : uops_0_br_mask; // @[util.scala 466:20 482:22 483:23]
  wire [7:0] _T_17 = io_brupdate_b1_mispredict_mask & uops_1_br_mask; // @[util.scala 118:51]
  wire  _T_18 = _T_17 != 8'h0; // @[util.scala 118:59]
  wire [7:0] _T_25 = uops_1_br_mask & _T_15; // @[util.scala 89:21]
  wire [7:0] _GEN_6 = valids_1 ? _T_25 : uops_1_br_mask; // @[util.scala 466:20 482:22 483:23]
  wire [7:0] _T_26 = io_brupdate_b1_mispredict_mask & uops_2_br_mask; // @[util.scala 118:51]
  wire  _T_27 = _T_26 != 8'h0; // @[util.scala 118:59]
  wire [7:0] _T_34 = uops_2_br_mask & _T_15; // @[util.scala 89:21]
  wire [7:0] _GEN_7 = valids_2 ? _T_34 : uops_2_br_mask; // @[util.scala 466:20 482:22 483:23]
  wire [7:0] _T_35 = io_brupdate_b1_mispredict_mask & uops_3_br_mask; // @[util.scala 118:51]
  wire  _T_36 = _T_35 != 8'h0; // @[util.scala 118:59]
  wire [7:0] _T_43 = uops_3_br_mask & _T_15; // @[util.scala 89:21]
  wire [7:0] _GEN_8 = valids_3 ? _T_43 : uops_3_br_mask; // @[util.scala 466:20 482:22 483:23]
  wire [7:0] _T_44 = io_brupdate_b1_mispredict_mask & uops_4_br_mask; // @[util.scala 118:51]
  wire  _T_45 = _T_44 != 8'h0; // @[util.scala 118:59]
  wire [7:0] _T_52 = uops_4_br_mask & _T_15; // @[util.scala 89:21]
  wire [7:0] _GEN_9 = valids_4 ? _T_52 : uops_4_br_mask; // @[util.scala 466:20 482:22 483:23]
  wire  _GEN_10 = 3'h0 == value | valids_0 & ~_T_9 & ~io_flush; // @[util.scala 481:16 489:{33,33}]
  wire  _GEN_11 = 3'h1 == value | valids_1 & ~_T_18 & ~io_flush; // @[util.scala 481:16 489:{33,33}]
  wire  _GEN_12 = 3'h2 == value | valids_2 & ~_T_27 & ~io_flush; // @[util.scala 481:16 489:{33,33}]
  wire  _GEN_13 = 3'h3 == value | valids_3 & ~_T_36 & ~io_flush; // @[util.scala 481:16 489:{33,33}]
  wire  _GEN_14 = 3'h4 == value | valids_4 & ~_T_45 & ~io_flush; // @[util.scala 481:16 489:{33,33}]
  wire [7:0] _T_55 = io_enq_bits_uop_br_mask & _T_15; // @[util.scala 85:25]
  wire  _T_56 = value == 3'h4; // @[Counter.scala 38:24]
  wire [2:0] _T_58 = value + 3'h1; // @[Counter.scala 39:22]
  wire  _GEN_1391 = io_deq_ready ? 1'h0 : _T_3; // @[util.scala 521:{27,36}]
  wire  do_enq = io_empty ? _GEN_1391 : _T_3; // @[util.scala 515:21]
  wire  _GEN_582 = do_enq ? _GEN_10 : valids_0 & ~_T_9 & ~io_flush; // @[util.scala 481:16 487:17]
  wire  _GEN_583 = do_enq ? _GEN_11 : valids_1 & ~_T_18 & ~io_flush; // @[util.scala 481:16 487:17]
  wire  _GEN_584 = do_enq ? _GEN_12 : valids_2 & ~_T_27 & ~io_flush; // @[util.scala 481:16 487:17]
  wire  _GEN_585 = do_enq ? _GEN_13 : valids_3 & ~_T_36 & ~io_flush; // @[util.scala 481:16 487:17]
  wire  _GEN_586 = do_enq ? _GEN_14 : valids_4 & ~_T_45 & ~io_flush; // @[util.scala 481:16 487:17]
  wire  _T_59 = value_1 == 3'h4; // @[Counter.scala 38:24]
  wire [2:0] _T_61 = value_1 + 3'h1; // @[Counter.scala 39:22]
  wire  do_deq = io_empty ? 1'h0 : _T_7; // @[util.scala 515:21 520:14]
  wire [1:0] _GEN_997 = 3'h1 == value_1 ? uops_1_debug_tsrc : uops_0_debug_tsrc; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_998 = 3'h2 == value_1 ? uops_2_debug_tsrc : _GEN_997; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_999 = 3'h3 == value_1 ? uops_3_debug_tsrc : _GEN_998; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_debug_tsrc = 3'h4 == value_1 ? uops_4_debug_tsrc : _GEN_999; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1002 = 3'h1 == value_1 ? uops_1_debug_fsrc : uops_0_debug_fsrc; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1003 = 3'h2 == value_1 ? uops_2_debug_fsrc : _GEN_1002; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1004 = 3'h3 == value_1 ? uops_3_debug_fsrc : _GEN_1003; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_debug_fsrc = 3'h4 == value_1 ? uops_4_debug_fsrc : _GEN_1004; // @[util.scala 508:{19,19}]
  wire  _GEN_1007 = 3'h1 == value_1 ? uops_1_bp_xcpt_if : uops_0_bp_xcpt_if; // @[util.scala 508:{19,19}]
  wire  _GEN_1008 = 3'h2 == value_1 ? uops_2_bp_xcpt_if : _GEN_1007; // @[util.scala 508:{19,19}]
  wire  _GEN_1009 = 3'h3 == value_1 ? uops_3_bp_xcpt_if : _GEN_1008; // @[util.scala 508:{19,19}]
  wire  out_uop_bp_xcpt_if = 3'h4 == value_1 ? uops_4_bp_xcpt_if : _GEN_1009; // @[util.scala 508:{19,19}]
  wire  _GEN_1012 = 3'h1 == value_1 ? uops_1_bp_debug_if : uops_0_bp_debug_if; // @[util.scala 508:{19,19}]
  wire  _GEN_1013 = 3'h2 == value_1 ? uops_2_bp_debug_if : _GEN_1012; // @[util.scala 508:{19,19}]
  wire  _GEN_1014 = 3'h3 == value_1 ? uops_3_bp_debug_if : _GEN_1013; // @[util.scala 508:{19,19}]
  wire  out_uop_bp_debug_if = 3'h4 == value_1 ? uops_4_bp_debug_if : _GEN_1014; // @[util.scala 508:{19,19}]
  wire  _GEN_1017 = 3'h1 == value_1 ? uops_1_xcpt_ma_if : uops_0_xcpt_ma_if; // @[util.scala 508:{19,19}]
  wire  _GEN_1018 = 3'h2 == value_1 ? uops_2_xcpt_ma_if : _GEN_1017; // @[util.scala 508:{19,19}]
  wire  _GEN_1019 = 3'h3 == value_1 ? uops_3_xcpt_ma_if : _GEN_1018; // @[util.scala 508:{19,19}]
  wire  out_uop_xcpt_ma_if = 3'h4 == value_1 ? uops_4_xcpt_ma_if : _GEN_1019; // @[util.scala 508:{19,19}]
  wire  _GEN_1022 = 3'h1 == value_1 ? uops_1_xcpt_ae_if : uops_0_xcpt_ae_if; // @[util.scala 508:{19,19}]
  wire  _GEN_1023 = 3'h2 == value_1 ? uops_2_xcpt_ae_if : _GEN_1022; // @[util.scala 508:{19,19}]
  wire  _GEN_1024 = 3'h3 == value_1 ? uops_3_xcpt_ae_if : _GEN_1023; // @[util.scala 508:{19,19}]
  wire  out_uop_xcpt_ae_if = 3'h4 == value_1 ? uops_4_xcpt_ae_if : _GEN_1024; // @[util.scala 508:{19,19}]
  wire  _GEN_1027 = 3'h1 == value_1 ? uops_1_xcpt_pf_if : uops_0_xcpt_pf_if; // @[util.scala 508:{19,19}]
  wire  _GEN_1028 = 3'h2 == value_1 ? uops_2_xcpt_pf_if : _GEN_1027; // @[util.scala 508:{19,19}]
  wire  _GEN_1029 = 3'h3 == value_1 ? uops_3_xcpt_pf_if : _GEN_1028; // @[util.scala 508:{19,19}]
  wire  out_uop_xcpt_pf_if = 3'h4 == value_1 ? uops_4_xcpt_pf_if : _GEN_1029; // @[util.scala 508:{19,19}]
  wire  _GEN_1032 = 3'h1 == value_1 ? uops_1_fp_single : uops_0_fp_single; // @[util.scala 508:{19,19}]
  wire  _GEN_1033 = 3'h2 == value_1 ? uops_2_fp_single : _GEN_1032; // @[util.scala 508:{19,19}]
  wire  _GEN_1034 = 3'h3 == value_1 ? uops_3_fp_single : _GEN_1033; // @[util.scala 508:{19,19}]
  wire  out_uop_fp_single = 3'h4 == value_1 ? uops_4_fp_single : _GEN_1034; // @[util.scala 508:{19,19}]
  wire  _GEN_1037 = 3'h1 == value_1 ? uops_1_fp_val : uops_0_fp_val; // @[util.scala 508:{19,19}]
  wire  _GEN_1038 = 3'h2 == value_1 ? uops_2_fp_val : _GEN_1037; // @[util.scala 508:{19,19}]
  wire  _GEN_1039 = 3'h3 == value_1 ? uops_3_fp_val : _GEN_1038; // @[util.scala 508:{19,19}]
  wire  out_uop_fp_val = 3'h4 == value_1 ? uops_4_fp_val : _GEN_1039; // @[util.scala 508:{19,19}]
  wire  _GEN_1042 = 3'h1 == value_1 ? uops_1_frs3_en : uops_0_frs3_en; // @[util.scala 508:{19,19}]
  wire  _GEN_1043 = 3'h2 == value_1 ? uops_2_frs3_en : _GEN_1042; // @[util.scala 508:{19,19}]
  wire  _GEN_1044 = 3'h3 == value_1 ? uops_3_frs3_en : _GEN_1043; // @[util.scala 508:{19,19}]
  wire  out_uop_frs3_en = 3'h4 == value_1 ? uops_4_frs3_en : _GEN_1044; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1047 = 3'h1 == value_1 ? uops_1_lrs2_rtype : uops_0_lrs2_rtype; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1048 = 3'h2 == value_1 ? uops_2_lrs2_rtype : _GEN_1047; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1049 = 3'h3 == value_1 ? uops_3_lrs2_rtype : _GEN_1048; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_lrs2_rtype = 3'h4 == value_1 ? uops_4_lrs2_rtype : _GEN_1049; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1052 = 3'h1 == value_1 ? uops_1_lrs1_rtype : uops_0_lrs1_rtype; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1053 = 3'h2 == value_1 ? uops_2_lrs1_rtype : _GEN_1052; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1054 = 3'h3 == value_1 ? uops_3_lrs1_rtype : _GEN_1053; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_lrs1_rtype = 3'h4 == value_1 ? uops_4_lrs1_rtype : _GEN_1054; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1057 = 3'h1 == value_1 ? uops_1_dst_rtype : uops_0_dst_rtype; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1058 = 3'h2 == value_1 ? uops_2_dst_rtype : _GEN_1057; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1059 = 3'h3 == value_1 ? uops_3_dst_rtype : _GEN_1058; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_dst_rtype = 3'h4 == value_1 ? uops_4_dst_rtype : _GEN_1059; // @[util.scala 508:{19,19}]
  wire  _GEN_1062 = 3'h1 == value_1 ? uops_1_ldst_val : uops_0_ldst_val; // @[util.scala 508:{19,19}]
  wire  _GEN_1063 = 3'h2 == value_1 ? uops_2_ldst_val : _GEN_1062; // @[util.scala 508:{19,19}]
  wire  _GEN_1064 = 3'h3 == value_1 ? uops_3_ldst_val : _GEN_1063; // @[util.scala 508:{19,19}]
  wire  out_uop_ldst_val = 3'h4 == value_1 ? uops_4_ldst_val : _GEN_1064; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1067 = 3'h1 == value_1 ? uops_1_lrs3 : uops_0_lrs3; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1068 = 3'h2 == value_1 ? uops_2_lrs3 : _GEN_1067; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1069 = 3'h3 == value_1 ? uops_3_lrs3 : _GEN_1068; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_lrs3 = 3'h4 == value_1 ? uops_4_lrs3 : _GEN_1069; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1072 = 3'h1 == value_1 ? uops_1_lrs2 : uops_0_lrs2; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1073 = 3'h2 == value_1 ? uops_2_lrs2 : _GEN_1072; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1074 = 3'h3 == value_1 ? uops_3_lrs2 : _GEN_1073; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_lrs2 = 3'h4 == value_1 ? uops_4_lrs2 : _GEN_1074; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1077 = 3'h1 == value_1 ? uops_1_lrs1 : uops_0_lrs1; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1078 = 3'h2 == value_1 ? uops_2_lrs1 : _GEN_1077; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1079 = 3'h3 == value_1 ? uops_3_lrs1 : _GEN_1078; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_lrs1 = 3'h4 == value_1 ? uops_4_lrs1 : _GEN_1079; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1082 = 3'h1 == value_1 ? uops_1_ldst : uops_0_ldst; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1083 = 3'h2 == value_1 ? uops_2_ldst : _GEN_1082; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1084 = 3'h3 == value_1 ? uops_3_ldst : _GEN_1083; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_ldst = 3'h4 == value_1 ? uops_4_ldst : _GEN_1084; // @[util.scala 508:{19,19}]
  wire  _GEN_1087 = 3'h1 == value_1 ? uops_1_ldst_is_rs1 : uops_0_ldst_is_rs1; // @[util.scala 508:{19,19}]
  wire  _GEN_1088 = 3'h2 == value_1 ? uops_2_ldst_is_rs1 : _GEN_1087; // @[util.scala 508:{19,19}]
  wire  _GEN_1089 = 3'h3 == value_1 ? uops_3_ldst_is_rs1 : _GEN_1088; // @[util.scala 508:{19,19}]
  wire  out_uop_ldst_is_rs1 = 3'h4 == value_1 ? uops_4_ldst_is_rs1 : _GEN_1089; // @[util.scala 508:{19,19}]
  wire  _GEN_1092 = 3'h1 == value_1 ? uops_1_flush_on_commit : uops_0_flush_on_commit; // @[util.scala 508:{19,19}]
  wire  _GEN_1093 = 3'h2 == value_1 ? uops_2_flush_on_commit : _GEN_1092; // @[util.scala 508:{19,19}]
  wire  _GEN_1094 = 3'h3 == value_1 ? uops_3_flush_on_commit : _GEN_1093; // @[util.scala 508:{19,19}]
  wire  out_uop_flush_on_commit = 3'h4 == value_1 ? uops_4_flush_on_commit : _GEN_1094; // @[util.scala 508:{19,19}]
  wire  _GEN_1097 = 3'h1 == value_1 ? uops_1_is_unique : uops_0_is_unique; // @[util.scala 508:{19,19}]
  wire  _GEN_1098 = 3'h2 == value_1 ? uops_2_is_unique : _GEN_1097; // @[util.scala 508:{19,19}]
  wire  _GEN_1099 = 3'h3 == value_1 ? uops_3_is_unique : _GEN_1098; // @[util.scala 508:{19,19}]
  wire  out_uop_is_unique = 3'h4 == value_1 ? uops_4_is_unique : _GEN_1099; // @[util.scala 508:{19,19}]
  wire  _GEN_1102 = 3'h1 == value_1 ? uops_1_is_sys_pc2epc : uops_0_is_sys_pc2epc; // @[util.scala 508:{19,19}]
  wire  _GEN_1103 = 3'h2 == value_1 ? uops_2_is_sys_pc2epc : _GEN_1102; // @[util.scala 508:{19,19}]
  wire  _GEN_1104 = 3'h3 == value_1 ? uops_3_is_sys_pc2epc : _GEN_1103; // @[util.scala 508:{19,19}]
  wire  out_uop_is_sys_pc2epc = 3'h4 == value_1 ? uops_4_is_sys_pc2epc : _GEN_1104; // @[util.scala 508:{19,19}]
  wire  _GEN_1107 = 3'h1 == value_1 ? uops_1_uses_stq : uops_0_uses_stq; // @[util.scala 508:{19,19}]
  wire  _GEN_1108 = 3'h2 == value_1 ? uops_2_uses_stq : _GEN_1107; // @[util.scala 508:{19,19}]
  wire  _GEN_1109 = 3'h3 == value_1 ? uops_3_uses_stq : _GEN_1108; // @[util.scala 508:{19,19}]
  wire  out_uop_uses_stq = 3'h4 == value_1 ? uops_4_uses_stq : _GEN_1109; // @[util.scala 508:{19,19}]
  wire  _GEN_1112 = 3'h1 == value_1 ? uops_1_uses_ldq : uops_0_uses_ldq; // @[util.scala 508:{19,19}]
  wire  _GEN_1113 = 3'h2 == value_1 ? uops_2_uses_ldq : _GEN_1112; // @[util.scala 508:{19,19}]
  wire  _GEN_1114 = 3'h3 == value_1 ? uops_3_uses_ldq : _GEN_1113; // @[util.scala 508:{19,19}]
  wire  out_uop_uses_ldq = 3'h4 == value_1 ? uops_4_uses_ldq : _GEN_1114; // @[util.scala 508:{19,19}]
  wire  _GEN_1117 = 3'h1 == value_1 ? uops_1_is_amo : uops_0_is_amo; // @[util.scala 508:{19,19}]
  wire  _GEN_1118 = 3'h2 == value_1 ? uops_2_is_amo : _GEN_1117; // @[util.scala 508:{19,19}]
  wire  _GEN_1119 = 3'h3 == value_1 ? uops_3_is_amo : _GEN_1118; // @[util.scala 508:{19,19}]
  wire  out_uop_is_amo = 3'h4 == value_1 ? uops_4_is_amo : _GEN_1119; // @[util.scala 508:{19,19}]
  wire  _GEN_1122 = 3'h1 == value_1 ? uops_1_is_fencei : uops_0_is_fencei; // @[util.scala 508:{19,19}]
  wire  _GEN_1123 = 3'h2 == value_1 ? uops_2_is_fencei : _GEN_1122; // @[util.scala 508:{19,19}]
  wire  _GEN_1124 = 3'h3 == value_1 ? uops_3_is_fencei : _GEN_1123; // @[util.scala 508:{19,19}]
  wire  out_uop_is_fencei = 3'h4 == value_1 ? uops_4_is_fencei : _GEN_1124; // @[util.scala 508:{19,19}]
  wire  _GEN_1127 = 3'h1 == value_1 ? uops_1_is_fence : uops_0_is_fence; // @[util.scala 508:{19,19}]
  wire  _GEN_1128 = 3'h2 == value_1 ? uops_2_is_fence : _GEN_1127; // @[util.scala 508:{19,19}]
  wire  _GEN_1129 = 3'h3 == value_1 ? uops_3_is_fence : _GEN_1128; // @[util.scala 508:{19,19}]
  wire  out_uop_is_fence = 3'h4 == value_1 ? uops_4_is_fence : _GEN_1129; // @[util.scala 508:{19,19}]
  wire  _GEN_1132 = 3'h1 == value_1 ? uops_1_mem_signed : uops_0_mem_signed; // @[util.scala 508:{19,19}]
  wire  _GEN_1133 = 3'h2 == value_1 ? uops_2_mem_signed : _GEN_1132; // @[util.scala 508:{19,19}]
  wire  _GEN_1134 = 3'h3 == value_1 ? uops_3_mem_signed : _GEN_1133; // @[util.scala 508:{19,19}]
  wire  out_uop_mem_signed = 3'h4 == value_1 ? uops_4_mem_signed : _GEN_1134; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1137 = 3'h1 == value_1 ? uops_1_mem_size : uops_0_mem_size; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1138 = 3'h2 == value_1 ? uops_2_mem_size : _GEN_1137; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1139 = 3'h3 == value_1 ? uops_3_mem_size : _GEN_1138; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_mem_size = 3'h4 == value_1 ? uops_4_mem_size : _GEN_1139; // @[util.scala 508:{19,19}]
  wire [4:0] _GEN_1142 = 3'h1 == value_1 ? uops_1_mem_cmd : uops_0_mem_cmd; // @[util.scala 508:{19,19}]
  wire [4:0] _GEN_1143 = 3'h2 == value_1 ? uops_2_mem_cmd : _GEN_1142; // @[util.scala 508:{19,19}]
  wire [4:0] _GEN_1144 = 3'h3 == value_1 ? uops_3_mem_cmd : _GEN_1143; // @[util.scala 508:{19,19}]
  wire [4:0] out_uop_mem_cmd = 3'h4 == value_1 ? uops_4_mem_cmd : _GEN_1144; // @[util.scala 508:{19,19}]
  wire  _GEN_1147 = 3'h1 == value_1 ? uops_1_bypassable : uops_0_bypassable; // @[util.scala 508:{19,19}]
  wire  _GEN_1148 = 3'h2 == value_1 ? uops_2_bypassable : _GEN_1147; // @[util.scala 508:{19,19}]
  wire  _GEN_1149 = 3'h3 == value_1 ? uops_3_bypassable : _GEN_1148; // @[util.scala 508:{19,19}]
  wire  out_uop_bypassable = 3'h4 == value_1 ? uops_4_bypassable : _GEN_1149; // @[util.scala 508:{19,19}]
  wire [63:0] _GEN_1152 = 3'h1 == value_1 ? uops_1_exc_cause : uops_0_exc_cause; // @[util.scala 508:{19,19}]
  wire [63:0] _GEN_1153 = 3'h2 == value_1 ? uops_2_exc_cause : _GEN_1152; // @[util.scala 508:{19,19}]
  wire [63:0] _GEN_1154 = 3'h3 == value_1 ? uops_3_exc_cause : _GEN_1153; // @[util.scala 508:{19,19}]
  wire [63:0] out_uop_exc_cause = 3'h4 == value_1 ? uops_4_exc_cause : _GEN_1154; // @[util.scala 508:{19,19}]
  wire  _GEN_1157 = 3'h1 == value_1 ? uops_1_exception : uops_0_exception; // @[util.scala 508:{19,19}]
  wire  _GEN_1158 = 3'h2 == value_1 ? uops_2_exception : _GEN_1157; // @[util.scala 508:{19,19}]
  wire  _GEN_1159 = 3'h3 == value_1 ? uops_3_exception : _GEN_1158; // @[util.scala 508:{19,19}]
  wire  out_uop_exception = 3'h4 == value_1 ? uops_4_exception : _GEN_1159; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1162 = 3'h1 == value_1 ? uops_1_stale_pdst : uops_0_stale_pdst; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1163 = 3'h2 == value_1 ? uops_2_stale_pdst : _GEN_1162; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1164 = 3'h3 == value_1 ? uops_3_stale_pdst : _GEN_1163; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_stale_pdst = 3'h4 == value_1 ? uops_4_stale_pdst : _GEN_1164; // @[util.scala 508:{19,19}]
  wire  _GEN_1167 = 3'h1 == value_1 ? uops_1_ppred_busy : uops_0_ppred_busy; // @[util.scala 508:{19,19}]
  wire  _GEN_1168 = 3'h2 == value_1 ? uops_2_ppred_busy : _GEN_1167; // @[util.scala 508:{19,19}]
  wire  _GEN_1169 = 3'h3 == value_1 ? uops_3_ppred_busy : _GEN_1168; // @[util.scala 508:{19,19}]
  wire  out_uop_ppred_busy = 3'h4 == value_1 ? uops_4_ppred_busy : _GEN_1169; // @[util.scala 508:{19,19}]
  wire  _GEN_1172 = 3'h1 == value_1 ? uops_1_prs3_busy : uops_0_prs3_busy; // @[util.scala 508:{19,19}]
  wire  _GEN_1173 = 3'h2 == value_1 ? uops_2_prs3_busy : _GEN_1172; // @[util.scala 508:{19,19}]
  wire  _GEN_1174 = 3'h3 == value_1 ? uops_3_prs3_busy : _GEN_1173; // @[util.scala 508:{19,19}]
  wire  out_uop_prs3_busy = 3'h4 == value_1 ? uops_4_prs3_busy : _GEN_1174; // @[util.scala 508:{19,19}]
  wire  _GEN_1177 = 3'h1 == value_1 ? uops_1_prs2_busy : uops_0_prs2_busy; // @[util.scala 508:{19,19}]
  wire  _GEN_1178 = 3'h2 == value_1 ? uops_2_prs2_busy : _GEN_1177; // @[util.scala 508:{19,19}]
  wire  _GEN_1179 = 3'h3 == value_1 ? uops_3_prs2_busy : _GEN_1178; // @[util.scala 508:{19,19}]
  wire  out_uop_prs2_busy = 3'h4 == value_1 ? uops_4_prs2_busy : _GEN_1179; // @[util.scala 508:{19,19}]
  wire  _GEN_1182 = 3'h1 == value_1 ? uops_1_prs1_busy : uops_0_prs1_busy; // @[util.scala 508:{19,19}]
  wire  _GEN_1183 = 3'h2 == value_1 ? uops_2_prs1_busy : _GEN_1182; // @[util.scala 508:{19,19}]
  wire  _GEN_1184 = 3'h3 == value_1 ? uops_3_prs1_busy : _GEN_1183; // @[util.scala 508:{19,19}]
  wire  out_uop_prs1_busy = 3'h4 == value_1 ? uops_4_prs1_busy : _GEN_1184; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1187 = 3'h1 == value_1 ? uops_1_ppred : uops_0_ppred; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1188 = 3'h2 == value_1 ? uops_2_ppred : _GEN_1187; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1189 = 3'h3 == value_1 ? uops_3_ppred : _GEN_1188; // @[util.scala 508:{19,19}]
  wire [3:0] out_uop_ppred = 3'h4 == value_1 ? uops_4_ppred : _GEN_1189; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1192 = 3'h1 == value_1 ? uops_1_prs3 : uops_0_prs3; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1193 = 3'h2 == value_1 ? uops_2_prs3 : _GEN_1192; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1194 = 3'h3 == value_1 ? uops_3_prs3 : _GEN_1193; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_prs3 = 3'h4 == value_1 ? uops_4_prs3 : _GEN_1194; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1197 = 3'h1 == value_1 ? uops_1_prs2 : uops_0_prs2; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1198 = 3'h2 == value_1 ? uops_2_prs2 : _GEN_1197; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1199 = 3'h3 == value_1 ? uops_3_prs2 : _GEN_1198; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_prs2 = 3'h4 == value_1 ? uops_4_prs2 : _GEN_1199; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1202 = 3'h1 == value_1 ? uops_1_prs1 : uops_0_prs1; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1203 = 3'h2 == value_1 ? uops_2_prs1 : _GEN_1202; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1204 = 3'h3 == value_1 ? uops_3_prs1 : _GEN_1203; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_prs1 = 3'h4 == value_1 ? uops_4_prs1 : _GEN_1204; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1207 = 3'h1 == value_1 ? uops_1_pdst : uops_0_pdst; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1208 = 3'h2 == value_1 ? uops_2_pdst : _GEN_1207; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1209 = 3'h3 == value_1 ? uops_3_pdst : _GEN_1208; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_pdst = 3'h4 == value_1 ? uops_4_pdst : _GEN_1209; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1212 = 3'h1 == value_1 ? uops_1_rxq_idx : uops_0_rxq_idx; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1213 = 3'h2 == value_1 ? uops_2_rxq_idx : _GEN_1212; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1214 = 3'h3 == value_1 ? uops_3_rxq_idx : _GEN_1213; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_rxq_idx = 3'h4 == value_1 ? uops_4_rxq_idx : _GEN_1214; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1217 = 3'h1 == value_1 ? uops_1_stq_idx : uops_0_stq_idx; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1218 = 3'h2 == value_1 ? uops_2_stq_idx : _GEN_1217; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1219 = 3'h3 == value_1 ? uops_3_stq_idx : _GEN_1218; // @[util.scala 508:{19,19}]
  wire [2:0] out_uop_stq_idx = 3'h4 == value_1 ? uops_4_stq_idx : _GEN_1219; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1222 = 3'h1 == value_1 ? uops_1_ldq_idx : uops_0_ldq_idx; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1223 = 3'h2 == value_1 ? uops_2_ldq_idx : _GEN_1222; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1224 = 3'h3 == value_1 ? uops_3_ldq_idx : _GEN_1223; // @[util.scala 508:{19,19}]
  wire [2:0] out_uop_ldq_idx = 3'h4 == value_1 ? uops_4_ldq_idx : _GEN_1224; // @[util.scala 508:{19,19}]
  wire [4:0] _GEN_1227 = 3'h1 == value_1 ? uops_1_rob_idx : uops_0_rob_idx; // @[util.scala 508:{19,19}]
  wire [4:0] _GEN_1228 = 3'h2 == value_1 ? uops_2_rob_idx : _GEN_1227; // @[util.scala 508:{19,19}]
  wire [4:0] _GEN_1229 = 3'h3 == value_1 ? uops_3_rob_idx : _GEN_1228; // @[util.scala 508:{19,19}]
  wire [4:0] out_uop_rob_idx = 3'h4 == value_1 ? uops_4_rob_idx : _GEN_1229; // @[util.scala 508:{19,19}]
  wire [11:0] _GEN_1232 = 3'h1 == value_1 ? uops_1_csr_addr : uops_0_csr_addr; // @[util.scala 508:{19,19}]
  wire [11:0] _GEN_1233 = 3'h2 == value_1 ? uops_2_csr_addr : _GEN_1232; // @[util.scala 508:{19,19}]
  wire [11:0] _GEN_1234 = 3'h3 == value_1 ? uops_3_csr_addr : _GEN_1233; // @[util.scala 508:{19,19}]
  wire [11:0] out_uop_csr_addr = 3'h4 == value_1 ? uops_4_csr_addr : _GEN_1234; // @[util.scala 508:{19,19}]
  wire [19:0] _GEN_1237 = 3'h1 == value_1 ? uops_1_imm_packed : uops_0_imm_packed; // @[util.scala 508:{19,19}]
  wire [19:0] _GEN_1238 = 3'h2 == value_1 ? uops_2_imm_packed : _GEN_1237; // @[util.scala 508:{19,19}]
  wire [19:0] _GEN_1239 = 3'h3 == value_1 ? uops_3_imm_packed : _GEN_1238; // @[util.scala 508:{19,19}]
  wire [19:0] out_uop_imm_packed = 3'h4 == value_1 ? uops_4_imm_packed : _GEN_1239; // @[util.scala 508:{19,19}]
  wire  _GEN_1242 = 3'h1 == value_1 ? uops_1_taken : uops_0_taken; // @[util.scala 508:{19,19}]
  wire  _GEN_1243 = 3'h2 == value_1 ? uops_2_taken : _GEN_1242; // @[util.scala 508:{19,19}]
  wire  _GEN_1244 = 3'h3 == value_1 ? uops_3_taken : _GEN_1243; // @[util.scala 508:{19,19}]
  wire  out_uop_taken = 3'h4 == value_1 ? uops_4_taken : _GEN_1244; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1247 = 3'h1 == value_1 ? uops_1_pc_lob : uops_0_pc_lob; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1248 = 3'h2 == value_1 ? uops_2_pc_lob : _GEN_1247; // @[util.scala 508:{19,19}]
  wire [5:0] _GEN_1249 = 3'h3 == value_1 ? uops_3_pc_lob : _GEN_1248; // @[util.scala 508:{19,19}]
  wire [5:0] out_uop_pc_lob = 3'h4 == value_1 ? uops_4_pc_lob : _GEN_1249; // @[util.scala 508:{19,19}]
  wire  _GEN_1252 = 3'h1 == value_1 ? uops_1_edge_inst : uops_0_edge_inst; // @[util.scala 508:{19,19}]
  wire  _GEN_1253 = 3'h2 == value_1 ? uops_2_edge_inst : _GEN_1252; // @[util.scala 508:{19,19}]
  wire  _GEN_1254 = 3'h3 == value_1 ? uops_3_edge_inst : _GEN_1253; // @[util.scala 508:{19,19}]
  wire  out_uop_edge_inst = 3'h4 == value_1 ? uops_4_edge_inst : _GEN_1254; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1257 = 3'h1 == value_1 ? uops_1_ftq_idx : uops_0_ftq_idx; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1258 = 3'h2 == value_1 ? uops_2_ftq_idx : _GEN_1257; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1259 = 3'h3 == value_1 ? uops_3_ftq_idx : _GEN_1258; // @[util.scala 508:{19,19}]
  wire [3:0] out_uop_ftq_idx = 3'h4 == value_1 ? uops_4_ftq_idx : _GEN_1259; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1262 = 3'h1 == value_1 ? uops_1_br_tag : uops_0_br_tag; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1263 = 3'h2 == value_1 ? uops_2_br_tag : _GEN_1262; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1264 = 3'h3 == value_1 ? uops_3_br_tag : _GEN_1263; // @[util.scala 508:{19,19}]
  wire [2:0] out_uop_br_tag = 3'h4 == value_1 ? uops_4_br_tag : _GEN_1264; // @[util.scala 508:{19,19}]
  wire [7:0] _GEN_1267 = 3'h1 == value_1 ? uops_1_br_mask : uops_0_br_mask; // @[util.scala 508:{19,19}]
  wire [7:0] _GEN_1268 = 3'h2 == value_1 ? uops_2_br_mask : _GEN_1267; // @[util.scala 508:{19,19}]
  wire [7:0] _GEN_1269 = 3'h3 == value_1 ? uops_3_br_mask : _GEN_1268; // @[util.scala 508:{19,19}]
  wire [7:0] out_uop_br_mask = 3'h4 == value_1 ? uops_4_br_mask : _GEN_1269; // @[util.scala 508:{19,19}]
  wire  _GEN_1272 = 3'h1 == value_1 ? uops_1_is_sfb : uops_0_is_sfb; // @[util.scala 508:{19,19}]
  wire  _GEN_1273 = 3'h2 == value_1 ? uops_2_is_sfb : _GEN_1272; // @[util.scala 508:{19,19}]
  wire  _GEN_1274 = 3'h3 == value_1 ? uops_3_is_sfb : _GEN_1273; // @[util.scala 508:{19,19}]
  wire  out_uop_is_sfb = 3'h4 == value_1 ? uops_4_is_sfb : _GEN_1274; // @[util.scala 508:{19,19}]
  wire  _GEN_1277 = 3'h1 == value_1 ? uops_1_is_jal : uops_0_is_jal; // @[util.scala 508:{19,19}]
  wire  _GEN_1278 = 3'h2 == value_1 ? uops_2_is_jal : _GEN_1277; // @[util.scala 508:{19,19}]
  wire  _GEN_1279 = 3'h3 == value_1 ? uops_3_is_jal : _GEN_1278; // @[util.scala 508:{19,19}]
  wire  out_uop_is_jal = 3'h4 == value_1 ? uops_4_is_jal : _GEN_1279; // @[util.scala 508:{19,19}]
  wire  _GEN_1282 = 3'h1 == value_1 ? uops_1_is_jalr : uops_0_is_jalr; // @[util.scala 508:{19,19}]
  wire  _GEN_1283 = 3'h2 == value_1 ? uops_2_is_jalr : _GEN_1282; // @[util.scala 508:{19,19}]
  wire  _GEN_1284 = 3'h3 == value_1 ? uops_3_is_jalr : _GEN_1283; // @[util.scala 508:{19,19}]
  wire  out_uop_is_jalr = 3'h4 == value_1 ? uops_4_is_jalr : _GEN_1284; // @[util.scala 508:{19,19}]
  wire  _GEN_1287 = 3'h1 == value_1 ? uops_1_is_br : uops_0_is_br; // @[util.scala 508:{19,19}]
  wire  _GEN_1288 = 3'h2 == value_1 ? uops_2_is_br : _GEN_1287; // @[util.scala 508:{19,19}]
  wire  _GEN_1289 = 3'h3 == value_1 ? uops_3_is_br : _GEN_1288; // @[util.scala 508:{19,19}]
  wire  out_uop_is_br = 3'h4 == value_1 ? uops_4_is_br : _GEN_1289; // @[util.scala 508:{19,19}]
  wire  _GEN_1292 = 3'h1 == value_1 ? uops_1_iw_p2_poisoned : uops_0_iw_p2_poisoned; // @[util.scala 508:{19,19}]
  wire  _GEN_1293 = 3'h2 == value_1 ? uops_2_iw_p2_poisoned : _GEN_1292; // @[util.scala 508:{19,19}]
  wire  _GEN_1294 = 3'h3 == value_1 ? uops_3_iw_p2_poisoned : _GEN_1293; // @[util.scala 508:{19,19}]
  wire  out_uop_iw_p2_poisoned = 3'h4 == value_1 ? uops_4_iw_p2_poisoned : _GEN_1294; // @[util.scala 508:{19,19}]
  wire  _GEN_1297 = 3'h1 == value_1 ? uops_1_iw_p1_poisoned : uops_0_iw_p1_poisoned; // @[util.scala 508:{19,19}]
  wire  _GEN_1298 = 3'h2 == value_1 ? uops_2_iw_p1_poisoned : _GEN_1297; // @[util.scala 508:{19,19}]
  wire  _GEN_1299 = 3'h3 == value_1 ? uops_3_iw_p1_poisoned : _GEN_1298; // @[util.scala 508:{19,19}]
  wire  out_uop_iw_p1_poisoned = 3'h4 == value_1 ? uops_4_iw_p1_poisoned : _GEN_1299; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1302 = 3'h1 == value_1 ? uops_1_iw_state : uops_0_iw_state; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1303 = 3'h2 == value_1 ? uops_2_iw_state : _GEN_1302; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1304 = 3'h3 == value_1 ? uops_3_iw_state : _GEN_1303; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_iw_state = 3'h4 == value_1 ? uops_4_iw_state : _GEN_1304; // @[util.scala 508:{19,19}]
  wire  _GEN_1307 = 3'h1 == value_1 ? uops_1_ctrl_is_std : uops_0_ctrl_is_std; // @[util.scala 508:{19,19}]
  wire  _GEN_1308 = 3'h2 == value_1 ? uops_2_ctrl_is_std : _GEN_1307; // @[util.scala 508:{19,19}]
  wire  _GEN_1309 = 3'h3 == value_1 ? uops_3_ctrl_is_std : _GEN_1308; // @[util.scala 508:{19,19}]
  wire  out_uop_ctrl_is_std = 3'h4 == value_1 ? uops_4_ctrl_is_std : _GEN_1309; // @[util.scala 508:{19,19}]
  wire  _GEN_1312 = 3'h1 == value_1 ? uops_1_ctrl_is_sta : uops_0_ctrl_is_sta; // @[util.scala 508:{19,19}]
  wire  _GEN_1313 = 3'h2 == value_1 ? uops_2_ctrl_is_sta : _GEN_1312; // @[util.scala 508:{19,19}]
  wire  _GEN_1314 = 3'h3 == value_1 ? uops_3_ctrl_is_sta : _GEN_1313; // @[util.scala 508:{19,19}]
  wire  out_uop_ctrl_is_sta = 3'h4 == value_1 ? uops_4_ctrl_is_sta : _GEN_1314; // @[util.scala 508:{19,19}]
  wire  _GEN_1317 = 3'h1 == value_1 ? uops_1_ctrl_is_load : uops_0_ctrl_is_load; // @[util.scala 508:{19,19}]
  wire  _GEN_1318 = 3'h2 == value_1 ? uops_2_ctrl_is_load : _GEN_1317; // @[util.scala 508:{19,19}]
  wire  _GEN_1319 = 3'h3 == value_1 ? uops_3_ctrl_is_load : _GEN_1318; // @[util.scala 508:{19,19}]
  wire  out_uop_ctrl_is_load = 3'h4 == value_1 ? uops_4_ctrl_is_load : _GEN_1319; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1322 = 3'h1 == value_1 ? uops_1_ctrl_csr_cmd : uops_0_ctrl_csr_cmd; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1323 = 3'h2 == value_1 ? uops_2_ctrl_csr_cmd : _GEN_1322; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1324 = 3'h3 == value_1 ? uops_3_ctrl_csr_cmd : _GEN_1323; // @[util.scala 508:{19,19}]
  wire [2:0] out_uop_ctrl_csr_cmd = 3'h4 == value_1 ? uops_4_ctrl_csr_cmd : _GEN_1324; // @[util.scala 508:{19,19}]
  wire  _GEN_1327 = 3'h1 == value_1 ? uops_1_ctrl_fcn_dw : uops_0_ctrl_fcn_dw; // @[util.scala 508:{19,19}]
  wire  _GEN_1328 = 3'h2 == value_1 ? uops_2_ctrl_fcn_dw : _GEN_1327; // @[util.scala 508:{19,19}]
  wire  _GEN_1329 = 3'h3 == value_1 ? uops_3_ctrl_fcn_dw : _GEN_1328; // @[util.scala 508:{19,19}]
  wire  out_uop_ctrl_fcn_dw = 3'h4 == value_1 ? uops_4_ctrl_fcn_dw : _GEN_1329; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1332 = 3'h1 == value_1 ? uops_1_ctrl_op_fcn : uops_0_ctrl_op_fcn; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1333 = 3'h2 == value_1 ? uops_2_ctrl_op_fcn : _GEN_1332; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1334 = 3'h3 == value_1 ? uops_3_ctrl_op_fcn : _GEN_1333; // @[util.scala 508:{19,19}]
  wire [3:0] out_uop_ctrl_op_fcn = 3'h4 == value_1 ? uops_4_ctrl_op_fcn : _GEN_1334; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1337 = 3'h1 == value_1 ? uops_1_ctrl_imm_sel : uops_0_ctrl_imm_sel; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1338 = 3'h2 == value_1 ? uops_2_ctrl_imm_sel : _GEN_1337; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1339 = 3'h3 == value_1 ? uops_3_ctrl_imm_sel : _GEN_1338; // @[util.scala 508:{19,19}]
  wire [2:0] out_uop_ctrl_imm_sel = 3'h4 == value_1 ? uops_4_ctrl_imm_sel : _GEN_1339; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1342 = 3'h1 == value_1 ? uops_1_ctrl_op2_sel : uops_0_ctrl_op2_sel; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1343 = 3'h2 == value_1 ? uops_2_ctrl_op2_sel : _GEN_1342; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1344 = 3'h3 == value_1 ? uops_3_ctrl_op2_sel : _GEN_1343; // @[util.scala 508:{19,19}]
  wire [2:0] out_uop_ctrl_op2_sel = 3'h4 == value_1 ? uops_4_ctrl_op2_sel : _GEN_1344; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1347 = 3'h1 == value_1 ? uops_1_ctrl_op1_sel : uops_0_ctrl_op1_sel; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1348 = 3'h2 == value_1 ? uops_2_ctrl_op1_sel : _GEN_1347; // @[util.scala 508:{19,19}]
  wire [1:0] _GEN_1349 = 3'h3 == value_1 ? uops_3_ctrl_op1_sel : _GEN_1348; // @[util.scala 508:{19,19}]
  wire [1:0] out_uop_ctrl_op1_sel = 3'h4 == value_1 ? uops_4_ctrl_op1_sel : _GEN_1349; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1352 = 3'h1 == value_1 ? uops_1_ctrl_br_type : uops_0_ctrl_br_type; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1353 = 3'h2 == value_1 ? uops_2_ctrl_br_type : _GEN_1352; // @[util.scala 508:{19,19}]
  wire [3:0] _GEN_1354 = 3'h3 == value_1 ? uops_3_ctrl_br_type : _GEN_1353; // @[util.scala 508:{19,19}]
  wire [3:0] out_uop_ctrl_br_type = 3'h4 == value_1 ? uops_4_ctrl_br_type : _GEN_1354; // @[util.scala 508:{19,19}]
  wire [9:0] _GEN_1357 = 3'h1 == value_1 ? uops_1_fu_code : uops_0_fu_code; // @[util.scala 508:{19,19}]
  wire [9:0] _GEN_1358 = 3'h2 == value_1 ? uops_2_fu_code : _GEN_1357; // @[util.scala 508:{19,19}]
  wire [9:0] _GEN_1359 = 3'h3 == value_1 ? uops_3_fu_code : _GEN_1358; // @[util.scala 508:{19,19}]
  wire [9:0] out_uop_fu_code = 3'h4 == value_1 ? uops_4_fu_code : _GEN_1359; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1362 = 3'h1 == value_1 ? uops_1_iq_type : uops_0_iq_type; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1363 = 3'h2 == value_1 ? uops_2_iq_type : _GEN_1362; // @[util.scala 508:{19,19}]
  wire [2:0] _GEN_1364 = 3'h3 == value_1 ? uops_3_iq_type : _GEN_1363; // @[util.scala 508:{19,19}]
  wire [2:0] out_uop_iq_type = 3'h4 == value_1 ? uops_4_iq_type : _GEN_1364; // @[util.scala 508:{19,19}]
  wire [39:0] _GEN_1367 = 3'h1 == value_1 ? uops_1_debug_pc : uops_0_debug_pc; // @[util.scala 508:{19,19}]
  wire [39:0] _GEN_1368 = 3'h2 == value_1 ? uops_2_debug_pc : _GEN_1367; // @[util.scala 508:{19,19}]
  wire [39:0] _GEN_1369 = 3'h3 == value_1 ? uops_3_debug_pc : _GEN_1368; // @[util.scala 508:{19,19}]
  wire [39:0] out_uop_debug_pc = 3'h4 == value_1 ? uops_4_debug_pc : _GEN_1369; // @[util.scala 508:{19,19}]
  wire  _GEN_1372 = 3'h1 == value_1 ? uops_1_is_rvc : uops_0_is_rvc; // @[util.scala 508:{19,19}]
  wire  _GEN_1373 = 3'h2 == value_1 ? uops_2_is_rvc : _GEN_1372; // @[util.scala 508:{19,19}]
  wire  _GEN_1374 = 3'h3 == value_1 ? uops_3_is_rvc : _GEN_1373; // @[util.scala 508:{19,19}]
  wire  out_uop_is_rvc = 3'h4 == value_1 ? uops_4_is_rvc : _GEN_1374; // @[util.scala 508:{19,19}]
  wire [31:0] _GEN_1377 = 3'h1 == value_1 ? uops_1_debug_inst : uops_0_debug_inst; // @[util.scala 508:{19,19}]
  wire [31:0] _GEN_1378 = 3'h2 == value_1 ? uops_2_debug_inst : _GEN_1377; // @[util.scala 508:{19,19}]
  wire [31:0] _GEN_1379 = 3'h3 == value_1 ? uops_3_debug_inst : _GEN_1378; // @[util.scala 508:{19,19}]
  wire [31:0] out_uop_debug_inst = 3'h4 == value_1 ? uops_4_debug_inst : _GEN_1379; // @[util.scala 508:{19,19}]
  wire [31:0] _GEN_1382 = 3'h1 == value_1 ? uops_1_inst : uops_0_inst; // @[util.scala 508:{19,19}]
  wire [31:0] _GEN_1383 = 3'h2 == value_1 ? uops_2_inst : _GEN_1382; // @[util.scala 508:{19,19}]
  wire [31:0] _GEN_1384 = 3'h3 == value_1 ? uops_3_inst : _GEN_1383; // @[util.scala 508:{19,19}]
  wire [31:0] out_uop_inst = 3'h4 == value_1 ? uops_4_inst : _GEN_1384; // @[util.scala 508:{19,19}]
  wire [6:0] _GEN_1387 = 3'h1 == value_1 ? uops_1_uopc : uops_0_uopc; // @[util.scala 508:{19,19}]
  wire [6:0] _GEN_1388 = 3'h2 == value_1 ? uops_2_uopc : _GEN_1387; // @[util.scala 508:{19,19}]
  wire [6:0] _GEN_1389 = 3'h3 == value_1 ? uops_3_uopc : _GEN_1388; // @[util.scala 508:{19,19}]
  wire [6:0] out_uop_uopc = 3'h4 == value_1 ? uops_4_uopc : _GEN_1389; // @[util.scala 508:{19,19}]
  wire [7:0] _T_67 = io_brupdate_b1_mispredict_mask & out_uop_br_mask; // @[util.scala 118:51]
  wire  _T_68 = _T_67 != 8'h0; // @[util.scala 118:59]
  wire [7:0] _T_75 = out_uop_br_mask & _T_15; // @[util.scala 85:25]
  wire [4:0] out_fflags_bits_flags = ram_fflags_bits_flags__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_debug_tsrc = ram_fflags_bits_uop_debug_tsrc__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_debug_fsrc = ram_fflags_bits_uop_debug_fsrc__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_bp_xcpt_if = ram_fflags_bits_uop_bp_xcpt_if__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_bp_debug_if = ram_fflags_bits_uop_bp_debug_if__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_xcpt_ma_if = ram_fflags_bits_uop_xcpt_ma_if__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_xcpt_ae_if = ram_fflags_bits_uop_xcpt_ae_if__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_xcpt_pf_if = ram_fflags_bits_uop_xcpt_pf_if__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_fp_single = ram_fflags_bits_uop_fp_single__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_fp_val = ram_fflags_bits_uop_fp_val__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_frs3_en = ram_fflags_bits_uop_frs3_en__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_lrs2_rtype = ram_fflags_bits_uop_lrs2_rtype__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_lrs1_rtype = ram_fflags_bits_uop_lrs1_rtype__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_dst_rtype = ram_fflags_bits_uop_dst_rtype__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_ldst_val = ram_fflags_bits_uop_ldst_val__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_lrs3 = ram_fflags_bits_uop_lrs3__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_lrs2 = ram_fflags_bits_uop_lrs2__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_lrs1 = ram_fflags_bits_uop_lrs1__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_ldst = ram_fflags_bits_uop_ldst__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_ldst_is_rs1 = ram_fflags_bits_uop_ldst_is_rs1__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_flush_on_commit = ram_fflags_bits_uop_flush_on_commit__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_unique = ram_fflags_bits_uop_is_unique__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_sys_pc2epc = ram_fflags_bits_uop_is_sys_pc2epc__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_uses_stq = ram_fflags_bits_uop_uses_stq__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_uses_ldq = ram_fflags_bits_uop_uses_ldq__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_amo = ram_fflags_bits_uop_is_amo__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_fencei = ram_fflags_bits_uop_is_fencei__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_fence = ram_fflags_bits_uop_is_fence__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_mem_signed = ram_fflags_bits_uop_mem_signed__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_mem_size = ram_fflags_bits_uop_mem_size__T_64_data; // @[util.scala 506:17 507:19]
  wire [4:0] out_fflags_bits_uop_mem_cmd = ram_fflags_bits_uop_mem_cmd__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_bypassable = ram_fflags_bits_uop_bypassable__T_64_data; // @[util.scala 506:17 507:19]
  wire [63:0] out_fflags_bits_uop_exc_cause = ram_fflags_bits_uop_exc_cause__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_exception = ram_fflags_bits_uop_exception__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_stale_pdst = ram_fflags_bits_uop_stale_pdst__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_ppred_busy = ram_fflags_bits_uop_ppred_busy__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_prs3_busy = ram_fflags_bits_uop_prs3_busy__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_prs2_busy = ram_fflags_bits_uop_prs2_busy__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_prs1_busy = ram_fflags_bits_uop_prs1_busy__T_64_data; // @[util.scala 506:17 507:19]
  wire [3:0] out_fflags_bits_uop_ppred = ram_fflags_bits_uop_ppred__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_prs3 = ram_fflags_bits_uop_prs3__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_prs2 = ram_fflags_bits_uop_prs2__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_prs1 = ram_fflags_bits_uop_prs1__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_pdst = ram_fflags_bits_uop_pdst__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_rxq_idx = ram_fflags_bits_uop_rxq_idx__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] out_fflags_bits_uop_stq_idx = ram_fflags_bits_uop_stq_idx__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] out_fflags_bits_uop_ldq_idx = ram_fflags_bits_uop_ldq_idx__T_64_data; // @[util.scala 506:17 507:19]
  wire [4:0] out_fflags_bits_uop_rob_idx = ram_fflags_bits_uop_rob_idx__T_64_data; // @[util.scala 506:17 507:19]
  wire [11:0] out_fflags_bits_uop_csr_addr = ram_fflags_bits_uop_csr_addr__T_64_data; // @[util.scala 506:17 507:19]
  wire [19:0] out_fflags_bits_uop_imm_packed = ram_fflags_bits_uop_imm_packed__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_taken = ram_fflags_bits_uop_taken__T_64_data; // @[util.scala 506:17 507:19]
  wire [5:0] out_fflags_bits_uop_pc_lob = ram_fflags_bits_uop_pc_lob__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_edge_inst = ram_fflags_bits_uop_edge_inst__T_64_data; // @[util.scala 506:17 507:19]
  wire [3:0] out_fflags_bits_uop_ftq_idx = ram_fflags_bits_uop_ftq_idx__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] out_fflags_bits_uop_br_tag = ram_fflags_bits_uop_br_tag__T_64_data; // @[util.scala 506:17 507:19]
  wire [7:0] out_fflags_bits_uop_br_mask = ram_fflags_bits_uop_br_mask__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_sfb = ram_fflags_bits_uop_is_sfb__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_jal = ram_fflags_bits_uop_is_jal__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_jalr = ram_fflags_bits_uop_is_jalr__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_br = ram_fflags_bits_uop_is_br__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_iw_p2_poisoned = ram_fflags_bits_uop_iw_p2_poisoned__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_iw_p1_poisoned = ram_fflags_bits_uop_iw_p1_poisoned__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_iw_state = ram_fflags_bits_uop_iw_state__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_ctrl_is_std = ram_fflags_bits_uop_ctrl_is_std__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_ctrl_is_sta = ram_fflags_bits_uop_ctrl_is_sta__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_ctrl_is_load = ram_fflags_bits_uop_ctrl_is_load__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] out_fflags_bits_uop_ctrl_csr_cmd = ram_fflags_bits_uop_ctrl_csr_cmd__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_ctrl_fcn_dw = ram_fflags_bits_uop_ctrl_fcn_dw__T_64_data; // @[util.scala 506:17 507:19]
  wire [3:0] out_fflags_bits_uop_ctrl_op_fcn = ram_fflags_bits_uop_ctrl_op_fcn__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] out_fflags_bits_uop_ctrl_imm_sel = ram_fflags_bits_uop_ctrl_imm_sel__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] out_fflags_bits_uop_ctrl_op2_sel = ram_fflags_bits_uop_ctrl_op2_sel__T_64_data; // @[util.scala 506:17 507:19]
  wire [1:0] out_fflags_bits_uop_ctrl_op1_sel = ram_fflags_bits_uop_ctrl_op1_sel__T_64_data; // @[util.scala 506:17 507:19]
  wire [3:0] out_fflags_bits_uop_ctrl_br_type = ram_fflags_bits_uop_ctrl_br_type__T_64_data; // @[util.scala 506:17 507:19]
  wire [9:0] out_fflags_bits_uop_fu_code = ram_fflags_bits_uop_fu_code__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] out_fflags_bits_uop_iq_type = ram_fflags_bits_uop_iq_type__T_64_data; // @[util.scala 506:17 507:19]
  wire [39:0] out_fflags_bits_uop_debug_pc = ram_fflags_bits_uop_debug_pc__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_bits_uop_is_rvc = ram_fflags_bits_uop_is_rvc__T_64_data; // @[util.scala 506:17 507:19]
  wire [31:0] out_fflags_bits_uop_debug_inst = ram_fflags_bits_uop_debug_inst__T_64_data; // @[util.scala 506:17 507:19]
  wire [31:0] out_fflags_bits_uop_inst = ram_fflags_bits_uop_inst__T_64_data; // @[util.scala 506:17 507:19]
  wire [6:0] out_fflags_bits_uop_uopc = ram_fflags_bits_uop_uopc__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_fflags_valid = ram_fflags_valid__T_64_data; // @[util.scala 506:17 507:19]
  wire  out_predicated = ram_predicated__T_64_data; // @[util.scala 506:17 507:19]
  wire [64:0] out_data = ram_data__T_64_data; // @[util.scala 506:17 507:19]
  wire [2:0] _T_79 = value - value_1; // @[util.scala 524:40]
  wire [2:0] _T_80 = maybe_full ? 3'h5 : 3'h0; // @[util.scala 530:24]
  wire [2:0] _T_83 = 3'h5 + _T_79; // @[util.scala 533:40]
  wire [2:0] _T_84 = value_1 > value ? _T_83 : _T_79; // @[util.scala 532:24]
  assign ram_data__T_64_en = 1'h1;
  assign ram_data__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_data__T_64_data = ram_data[ram_data__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_data__T_64_data = ram_data__T_64_addr >= 3'h5 ? _RAND_1[64:0] : ram_data[ram_data__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_data__T_53_data = io_enq_bits_data;
  assign ram_data__T_53_addr = value;
  assign ram_data__T_53_mask = 1'h1;
  assign ram_data__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_predicated__T_64_en = 1'h1;
  assign ram_predicated__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_predicated__T_64_data = ram_predicated[ram_predicated__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_predicated__T_64_data = ram_predicated__T_64_addr >= 3'h5 ? _RAND_3[0:0] :
    ram_predicated[ram_predicated__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_predicated__T_53_data = io_enq_bits_predicated;
  assign ram_predicated__T_53_addr = value;
  assign ram_predicated__T_53_mask = 1'h1;
  assign ram_predicated__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_valid__T_64_en = 1'h1;
  assign ram_fflags_valid__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_valid__T_64_data = ram_fflags_valid[ram_fflags_valid__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_valid__T_64_data = ram_fflags_valid__T_64_addr >= 3'h5 ? _RAND_5[0:0] :
    ram_fflags_valid[ram_fflags_valid__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_valid__T_53_data = io_enq_bits_fflags_valid;
  assign ram_fflags_valid__T_53_addr = value;
  assign ram_fflags_valid__T_53_mask = 1'h1;
  assign ram_fflags_valid__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_uopc__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_uopc__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_uopc__T_64_data = ram_fflags_bits_uop_uopc[ram_fflags_bits_uop_uopc__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_uopc__T_64_data = ram_fflags_bits_uop_uopc__T_64_addr >= 3'h5 ? _RAND_7[6:0] :
    ram_fflags_bits_uop_uopc[ram_fflags_bits_uop_uopc__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_uopc__T_53_data = io_enq_bits_fflags_bits_uop_uopc;
  assign ram_fflags_bits_uop_uopc__T_53_addr = value;
  assign ram_fflags_bits_uop_uopc__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_uopc__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_inst__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_inst__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_inst__T_64_data = ram_fflags_bits_uop_inst[ram_fflags_bits_uop_inst__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_inst__T_64_data = ram_fflags_bits_uop_inst__T_64_addr >= 3'h5 ? _RAND_9[31:0] :
    ram_fflags_bits_uop_inst[ram_fflags_bits_uop_inst__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_inst__T_53_data = io_enq_bits_fflags_bits_uop_inst;
  assign ram_fflags_bits_uop_inst__T_53_addr = value;
  assign ram_fflags_bits_uop_inst__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_inst__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_debug_inst__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_debug_inst__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_inst__T_64_data =
    ram_fflags_bits_uop_debug_inst[ram_fflags_bits_uop_debug_inst__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_debug_inst__T_64_data = ram_fflags_bits_uop_debug_inst__T_64_addr >= 3'h5 ? _RAND_11[31:0]
     : ram_fflags_bits_uop_debug_inst[ram_fflags_bits_uop_debug_inst__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_inst__T_53_data = io_enq_bits_fflags_bits_uop_debug_inst;
  assign ram_fflags_bits_uop_debug_inst__T_53_addr = value;
  assign ram_fflags_bits_uop_debug_inst__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_debug_inst__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_rvc__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_rvc__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_rvc__T_64_data = ram_fflags_bits_uop_is_rvc[ram_fflags_bits_uop_is_rvc__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_rvc__T_64_data = ram_fflags_bits_uop_is_rvc__T_64_addr >= 3'h5 ? _RAND_13[0:0] :
    ram_fflags_bits_uop_is_rvc[ram_fflags_bits_uop_is_rvc__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_rvc__T_53_data = io_enq_bits_fflags_bits_uop_is_rvc;
  assign ram_fflags_bits_uop_is_rvc__T_53_addr = value;
  assign ram_fflags_bits_uop_is_rvc__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_rvc__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_debug_pc__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_debug_pc__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_pc__T_64_data = ram_fflags_bits_uop_debug_pc[ram_fflags_bits_uop_debug_pc__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_debug_pc__T_64_data = ram_fflags_bits_uop_debug_pc__T_64_addr >= 3'h5 ? _RAND_15[39:0] :
    ram_fflags_bits_uop_debug_pc[ram_fflags_bits_uop_debug_pc__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_pc__T_53_data = io_enq_bits_fflags_bits_uop_debug_pc;
  assign ram_fflags_bits_uop_debug_pc__T_53_addr = value;
  assign ram_fflags_bits_uop_debug_pc__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_debug_pc__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_iq_type__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_iq_type__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iq_type__T_64_data = ram_fflags_bits_uop_iq_type[ram_fflags_bits_uop_iq_type__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_iq_type__T_64_data = ram_fflags_bits_uop_iq_type__T_64_addr >= 3'h5 ? _RAND_17[2:0] :
    ram_fflags_bits_uop_iq_type[ram_fflags_bits_uop_iq_type__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iq_type__T_53_data = io_enq_bits_fflags_bits_uop_iq_type;
  assign ram_fflags_bits_uop_iq_type__T_53_addr = value;
  assign ram_fflags_bits_uop_iq_type__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_iq_type__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_fu_code__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_fu_code__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_fu_code__T_64_data = ram_fflags_bits_uop_fu_code[ram_fflags_bits_uop_fu_code__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_fu_code__T_64_data = ram_fflags_bits_uop_fu_code__T_64_addr >= 3'h5 ? _RAND_19[9:0] :
    ram_fflags_bits_uop_fu_code[ram_fflags_bits_uop_fu_code__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_fu_code__T_53_data = io_enq_bits_fflags_bits_uop_fu_code;
  assign ram_fflags_bits_uop_fu_code__T_53_addr = value;
  assign ram_fflags_bits_uop_fu_code__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_fu_code__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_br_type__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_br_type__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_br_type__T_64_data =
    ram_fflags_bits_uop_ctrl_br_type[ram_fflags_bits_uop_ctrl_br_type__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_br_type__T_64_data = ram_fflags_bits_uop_ctrl_br_type__T_64_addr >= 3'h5 ? _RAND_21[3
    :0] : ram_fflags_bits_uop_ctrl_br_type[ram_fflags_bits_uop_ctrl_br_type__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_br_type__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_br_type;
  assign ram_fflags_bits_uop_ctrl_br_type__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_br_type__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_br_type__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_64_data =
    ram_fflags_bits_uop_ctrl_op1_sel[ram_fflags_bits_uop_ctrl_op1_sel__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_64_data = ram_fflags_bits_uop_ctrl_op1_sel__T_64_addr >= 3'h5 ? _RAND_23[1
    :0] : ram_fflags_bits_uop_ctrl_op1_sel[ram_fflags_bits_uop_ctrl_op1_sel__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_op1_sel;
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_op1_sel__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_64_data =
    ram_fflags_bits_uop_ctrl_op2_sel[ram_fflags_bits_uop_ctrl_op2_sel__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_64_data = ram_fflags_bits_uop_ctrl_op2_sel__T_64_addr >= 3'h5 ? _RAND_25[2
    :0] : ram_fflags_bits_uop_ctrl_op2_sel[ram_fflags_bits_uop_ctrl_op2_sel__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_op2_sel;
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_op2_sel__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_64_data =
    ram_fflags_bits_uop_ctrl_imm_sel[ram_fflags_bits_uop_ctrl_imm_sel__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_64_data = ram_fflags_bits_uop_ctrl_imm_sel__T_64_addr >= 3'h5 ? _RAND_27[2
    :0] : ram_fflags_bits_uop_ctrl_imm_sel[ram_fflags_bits_uop_ctrl_imm_sel__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_imm_sel;
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_imm_sel__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_64_data =
    ram_fflags_bits_uop_ctrl_op_fcn[ram_fflags_bits_uop_ctrl_op_fcn__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_64_data = ram_fflags_bits_uop_ctrl_op_fcn__T_64_addr >= 3'h5 ? _RAND_29[3:0]
     : ram_fflags_bits_uop_ctrl_op_fcn[ram_fflags_bits_uop_ctrl_op_fcn__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_op_fcn;
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_op_fcn__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_64_data =
    ram_fflags_bits_uop_ctrl_fcn_dw[ram_fflags_bits_uop_ctrl_fcn_dw__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_64_data = ram_fflags_bits_uop_ctrl_fcn_dw__T_64_addr >= 3'h5 ? _RAND_31[0:0]
     : ram_fflags_bits_uop_ctrl_fcn_dw[ram_fflags_bits_uop_ctrl_fcn_dw__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_fcn_dw;
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_fcn_dw__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_64_data =
    ram_fflags_bits_uop_ctrl_csr_cmd[ram_fflags_bits_uop_ctrl_csr_cmd__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_64_data = ram_fflags_bits_uop_ctrl_csr_cmd__T_64_addr >= 3'h5 ? _RAND_33[2
    :0] : ram_fflags_bits_uop_ctrl_csr_cmd[ram_fflags_bits_uop_ctrl_csr_cmd__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_csr_cmd;
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_csr_cmd__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_is_load__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_is_load__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_is_load__T_64_data =
    ram_fflags_bits_uop_ctrl_is_load[ram_fflags_bits_uop_ctrl_is_load__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_is_load__T_64_data = ram_fflags_bits_uop_ctrl_is_load__T_64_addr >= 3'h5 ? _RAND_35[0
    :0] : ram_fflags_bits_uop_ctrl_is_load[ram_fflags_bits_uop_ctrl_is_load__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_is_load__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_is_load;
  assign ram_fflags_bits_uop_ctrl_is_load__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_is_load__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_is_load__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_is_sta__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_is_sta__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_is_sta__T_64_data =
    ram_fflags_bits_uop_ctrl_is_sta[ram_fflags_bits_uop_ctrl_is_sta__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_is_sta__T_64_data = ram_fflags_bits_uop_ctrl_is_sta__T_64_addr >= 3'h5 ? _RAND_37[0:0]
     : ram_fflags_bits_uop_ctrl_is_sta[ram_fflags_bits_uop_ctrl_is_sta__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_is_sta__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_is_sta;
  assign ram_fflags_bits_uop_ctrl_is_sta__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_is_sta__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_is_sta__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ctrl_is_std__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ctrl_is_std__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_is_std__T_64_data =
    ram_fflags_bits_uop_ctrl_is_std[ram_fflags_bits_uop_ctrl_is_std__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ctrl_is_std__T_64_data = ram_fflags_bits_uop_ctrl_is_std__T_64_addr >= 3'h5 ? _RAND_39[0:0]
     : ram_fflags_bits_uop_ctrl_is_std[ram_fflags_bits_uop_ctrl_is_std__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ctrl_is_std__T_53_data = io_enq_bits_fflags_bits_uop_ctrl_is_std;
  assign ram_fflags_bits_uop_ctrl_is_std__T_53_addr = value;
  assign ram_fflags_bits_uop_ctrl_is_std__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ctrl_is_std__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_iw_state__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_iw_state__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iw_state__T_64_data = ram_fflags_bits_uop_iw_state[ram_fflags_bits_uop_iw_state__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_iw_state__T_64_data = ram_fflags_bits_uop_iw_state__T_64_addr >= 3'h5 ? _RAND_41[1:0] :
    ram_fflags_bits_uop_iw_state[ram_fflags_bits_uop_iw_state__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iw_state__T_53_data = io_enq_bits_fflags_bits_uop_iw_state;
  assign ram_fflags_bits_uop_iw_state__T_53_addr = value;
  assign ram_fflags_bits_uop_iw_state__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_iw_state__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_64_data =
    ram_fflags_bits_uop_iw_p1_poisoned[ram_fflags_bits_uop_iw_p1_poisoned__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_64_data = ram_fflags_bits_uop_iw_p1_poisoned__T_64_addr >= 3'h5 ?
    _RAND_43[0:0] : ram_fflags_bits_uop_iw_p1_poisoned[ram_fflags_bits_uop_iw_p1_poisoned__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_53_data = io_enq_bits_fflags_bits_uop_iw_p1_poisoned;
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_53_addr = value;
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_iw_p1_poisoned__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_64_data =
    ram_fflags_bits_uop_iw_p2_poisoned[ram_fflags_bits_uop_iw_p2_poisoned__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_64_data = ram_fflags_bits_uop_iw_p2_poisoned__T_64_addr >= 3'h5 ?
    _RAND_45[0:0] : ram_fflags_bits_uop_iw_p2_poisoned[ram_fflags_bits_uop_iw_p2_poisoned__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_53_data = io_enq_bits_fflags_bits_uop_iw_p2_poisoned;
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_53_addr = value;
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_iw_p2_poisoned__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_br__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_br__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_br__T_64_data = ram_fflags_bits_uop_is_br[ram_fflags_bits_uop_is_br__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_br__T_64_data = ram_fflags_bits_uop_is_br__T_64_addr >= 3'h5 ? _RAND_47[0:0] :
    ram_fflags_bits_uop_is_br[ram_fflags_bits_uop_is_br__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_br__T_53_data = io_enq_bits_fflags_bits_uop_is_br;
  assign ram_fflags_bits_uop_is_br__T_53_addr = value;
  assign ram_fflags_bits_uop_is_br__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_br__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_jalr__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_jalr__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_jalr__T_64_data = ram_fflags_bits_uop_is_jalr[ram_fflags_bits_uop_is_jalr__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_jalr__T_64_data = ram_fflags_bits_uop_is_jalr__T_64_addr >= 3'h5 ? _RAND_49[0:0] :
    ram_fflags_bits_uop_is_jalr[ram_fflags_bits_uop_is_jalr__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_jalr__T_53_data = io_enq_bits_fflags_bits_uop_is_jalr;
  assign ram_fflags_bits_uop_is_jalr__T_53_addr = value;
  assign ram_fflags_bits_uop_is_jalr__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_jalr__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_jal__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_jal__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_jal__T_64_data = ram_fflags_bits_uop_is_jal[ram_fflags_bits_uop_is_jal__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_jal__T_64_data = ram_fflags_bits_uop_is_jal__T_64_addr >= 3'h5 ? _RAND_51[0:0] :
    ram_fflags_bits_uop_is_jal[ram_fflags_bits_uop_is_jal__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_jal__T_53_data = io_enq_bits_fflags_bits_uop_is_jal;
  assign ram_fflags_bits_uop_is_jal__T_53_addr = value;
  assign ram_fflags_bits_uop_is_jal__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_jal__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_sfb__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_sfb__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_sfb__T_64_data = ram_fflags_bits_uop_is_sfb[ram_fflags_bits_uop_is_sfb__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_sfb__T_64_data = ram_fflags_bits_uop_is_sfb__T_64_addr >= 3'h5 ? _RAND_53[0:0] :
    ram_fflags_bits_uop_is_sfb[ram_fflags_bits_uop_is_sfb__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_sfb__T_53_data = io_enq_bits_fflags_bits_uop_is_sfb;
  assign ram_fflags_bits_uop_is_sfb__T_53_addr = value;
  assign ram_fflags_bits_uop_is_sfb__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_sfb__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_br_mask__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_br_mask__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_br_mask__T_64_data = ram_fflags_bits_uop_br_mask[ram_fflags_bits_uop_br_mask__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_br_mask__T_64_data = ram_fflags_bits_uop_br_mask__T_64_addr >= 3'h5 ? _RAND_55[7:0] :
    ram_fflags_bits_uop_br_mask[ram_fflags_bits_uop_br_mask__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_br_mask__T_53_data = io_enq_bits_fflags_bits_uop_br_mask;
  assign ram_fflags_bits_uop_br_mask__T_53_addr = value;
  assign ram_fflags_bits_uop_br_mask__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_br_mask__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_br_tag__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_br_tag__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_br_tag__T_64_data = ram_fflags_bits_uop_br_tag[ram_fflags_bits_uop_br_tag__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_br_tag__T_64_data = ram_fflags_bits_uop_br_tag__T_64_addr >= 3'h5 ? _RAND_57[2:0] :
    ram_fflags_bits_uop_br_tag[ram_fflags_bits_uop_br_tag__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_br_tag__T_53_data = io_enq_bits_fflags_bits_uop_br_tag;
  assign ram_fflags_bits_uop_br_tag__T_53_addr = value;
  assign ram_fflags_bits_uop_br_tag__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_br_tag__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ftq_idx__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ftq_idx__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ftq_idx__T_64_data = ram_fflags_bits_uop_ftq_idx[ram_fflags_bits_uop_ftq_idx__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ftq_idx__T_64_data = ram_fflags_bits_uop_ftq_idx__T_64_addr >= 3'h5 ? _RAND_59[3:0] :
    ram_fflags_bits_uop_ftq_idx[ram_fflags_bits_uop_ftq_idx__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ftq_idx__T_53_data = io_enq_bits_fflags_bits_uop_ftq_idx;
  assign ram_fflags_bits_uop_ftq_idx__T_53_addr = value;
  assign ram_fflags_bits_uop_ftq_idx__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ftq_idx__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_edge_inst__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_edge_inst__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_edge_inst__T_64_data =
    ram_fflags_bits_uop_edge_inst[ram_fflags_bits_uop_edge_inst__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_edge_inst__T_64_data = ram_fflags_bits_uop_edge_inst__T_64_addr >= 3'h5 ? _RAND_61[0:0] :
    ram_fflags_bits_uop_edge_inst[ram_fflags_bits_uop_edge_inst__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_edge_inst__T_53_data = io_enq_bits_fflags_bits_uop_edge_inst;
  assign ram_fflags_bits_uop_edge_inst__T_53_addr = value;
  assign ram_fflags_bits_uop_edge_inst__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_edge_inst__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_pc_lob__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_pc_lob__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_pc_lob__T_64_data = ram_fflags_bits_uop_pc_lob[ram_fflags_bits_uop_pc_lob__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_pc_lob__T_64_data = ram_fflags_bits_uop_pc_lob__T_64_addr >= 3'h5 ? _RAND_63[5:0] :
    ram_fflags_bits_uop_pc_lob[ram_fflags_bits_uop_pc_lob__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_pc_lob__T_53_data = io_enq_bits_fflags_bits_uop_pc_lob;
  assign ram_fflags_bits_uop_pc_lob__T_53_addr = value;
  assign ram_fflags_bits_uop_pc_lob__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_pc_lob__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_taken__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_taken__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_taken__T_64_data = ram_fflags_bits_uop_taken[ram_fflags_bits_uop_taken__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_taken__T_64_data = ram_fflags_bits_uop_taken__T_64_addr >= 3'h5 ? _RAND_65[0:0] :
    ram_fflags_bits_uop_taken[ram_fflags_bits_uop_taken__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_taken__T_53_data = io_enq_bits_fflags_bits_uop_taken;
  assign ram_fflags_bits_uop_taken__T_53_addr = value;
  assign ram_fflags_bits_uop_taken__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_taken__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_imm_packed__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_imm_packed__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_imm_packed__T_64_data =
    ram_fflags_bits_uop_imm_packed[ram_fflags_bits_uop_imm_packed__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_imm_packed__T_64_data = ram_fflags_bits_uop_imm_packed__T_64_addr >= 3'h5 ? _RAND_67[19:0]
     : ram_fflags_bits_uop_imm_packed[ram_fflags_bits_uop_imm_packed__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_imm_packed__T_53_data = io_enq_bits_fflags_bits_uop_imm_packed;
  assign ram_fflags_bits_uop_imm_packed__T_53_addr = value;
  assign ram_fflags_bits_uop_imm_packed__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_imm_packed__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_csr_addr__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_csr_addr__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_csr_addr__T_64_data = ram_fflags_bits_uop_csr_addr[ram_fflags_bits_uop_csr_addr__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_csr_addr__T_64_data = ram_fflags_bits_uop_csr_addr__T_64_addr >= 3'h5 ? _RAND_69[11:0] :
    ram_fflags_bits_uop_csr_addr[ram_fflags_bits_uop_csr_addr__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_csr_addr__T_53_data = io_enq_bits_fflags_bits_uop_csr_addr;
  assign ram_fflags_bits_uop_csr_addr__T_53_addr = value;
  assign ram_fflags_bits_uop_csr_addr__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_csr_addr__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_rob_idx__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_rob_idx__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_rob_idx__T_64_data = ram_fflags_bits_uop_rob_idx[ram_fflags_bits_uop_rob_idx__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_rob_idx__T_64_data = ram_fflags_bits_uop_rob_idx__T_64_addr >= 3'h5 ? _RAND_71[4:0] :
    ram_fflags_bits_uop_rob_idx[ram_fflags_bits_uop_rob_idx__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_rob_idx__T_53_data = io_enq_bits_fflags_bits_uop_rob_idx;
  assign ram_fflags_bits_uop_rob_idx__T_53_addr = value;
  assign ram_fflags_bits_uop_rob_idx__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_rob_idx__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ldq_idx__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ldq_idx__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldq_idx__T_64_data = ram_fflags_bits_uop_ldq_idx[ram_fflags_bits_uop_ldq_idx__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ldq_idx__T_64_data = ram_fflags_bits_uop_ldq_idx__T_64_addr >= 3'h5 ? _RAND_73[2:0] :
    ram_fflags_bits_uop_ldq_idx[ram_fflags_bits_uop_ldq_idx__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldq_idx__T_53_data = io_enq_bits_fflags_bits_uop_ldq_idx;
  assign ram_fflags_bits_uop_ldq_idx__T_53_addr = value;
  assign ram_fflags_bits_uop_ldq_idx__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ldq_idx__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_stq_idx__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_stq_idx__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_stq_idx__T_64_data = ram_fflags_bits_uop_stq_idx[ram_fflags_bits_uop_stq_idx__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_stq_idx__T_64_data = ram_fflags_bits_uop_stq_idx__T_64_addr >= 3'h5 ? _RAND_75[2:0] :
    ram_fflags_bits_uop_stq_idx[ram_fflags_bits_uop_stq_idx__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_stq_idx__T_53_data = io_enq_bits_fflags_bits_uop_stq_idx;
  assign ram_fflags_bits_uop_stq_idx__T_53_addr = value;
  assign ram_fflags_bits_uop_stq_idx__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_stq_idx__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_rxq_idx__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_rxq_idx__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_rxq_idx__T_64_data = ram_fflags_bits_uop_rxq_idx[ram_fflags_bits_uop_rxq_idx__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_rxq_idx__T_64_data = ram_fflags_bits_uop_rxq_idx__T_64_addr >= 3'h5 ? _RAND_77[1:0] :
    ram_fflags_bits_uop_rxq_idx[ram_fflags_bits_uop_rxq_idx__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_rxq_idx__T_53_data = io_enq_bits_fflags_bits_uop_rxq_idx;
  assign ram_fflags_bits_uop_rxq_idx__T_53_addr = value;
  assign ram_fflags_bits_uop_rxq_idx__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_rxq_idx__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_pdst__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_pdst__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_pdst__T_64_data = ram_fflags_bits_uop_pdst[ram_fflags_bits_uop_pdst__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_pdst__T_64_data = ram_fflags_bits_uop_pdst__T_64_addr >= 3'h5 ? _RAND_79[5:0] :
    ram_fflags_bits_uop_pdst[ram_fflags_bits_uop_pdst__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_pdst__T_53_data = io_enq_bits_fflags_bits_uop_pdst;
  assign ram_fflags_bits_uop_pdst__T_53_addr = value;
  assign ram_fflags_bits_uop_pdst__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_pdst__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_prs1__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_prs1__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs1__T_64_data = ram_fflags_bits_uop_prs1[ram_fflags_bits_uop_prs1__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_prs1__T_64_data = ram_fflags_bits_uop_prs1__T_64_addr >= 3'h5 ? _RAND_81[5:0] :
    ram_fflags_bits_uop_prs1[ram_fflags_bits_uop_prs1__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs1__T_53_data = io_enq_bits_fflags_bits_uop_prs1;
  assign ram_fflags_bits_uop_prs1__T_53_addr = value;
  assign ram_fflags_bits_uop_prs1__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_prs1__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_prs2__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_prs2__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs2__T_64_data = ram_fflags_bits_uop_prs2[ram_fflags_bits_uop_prs2__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_prs2__T_64_data = ram_fflags_bits_uop_prs2__T_64_addr >= 3'h5 ? _RAND_83[5:0] :
    ram_fflags_bits_uop_prs2[ram_fflags_bits_uop_prs2__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs2__T_53_data = io_enq_bits_fflags_bits_uop_prs2;
  assign ram_fflags_bits_uop_prs2__T_53_addr = value;
  assign ram_fflags_bits_uop_prs2__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_prs2__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_prs3__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_prs3__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs3__T_64_data = ram_fflags_bits_uop_prs3[ram_fflags_bits_uop_prs3__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_prs3__T_64_data = ram_fflags_bits_uop_prs3__T_64_addr >= 3'h5 ? _RAND_85[5:0] :
    ram_fflags_bits_uop_prs3[ram_fflags_bits_uop_prs3__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs3__T_53_data = io_enq_bits_fflags_bits_uop_prs3;
  assign ram_fflags_bits_uop_prs3__T_53_addr = value;
  assign ram_fflags_bits_uop_prs3__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_prs3__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ppred__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ppred__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ppred__T_64_data = ram_fflags_bits_uop_ppred[ram_fflags_bits_uop_ppred__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ppred__T_64_data = ram_fflags_bits_uop_ppred__T_64_addr >= 3'h5 ? _RAND_87[3:0] :
    ram_fflags_bits_uop_ppred[ram_fflags_bits_uop_ppred__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ppred__T_53_data = io_enq_bits_fflags_bits_uop_ppred;
  assign ram_fflags_bits_uop_ppred__T_53_addr = value;
  assign ram_fflags_bits_uop_ppred__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ppred__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_prs1_busy__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_prs1_busy__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs1_busy__T_64_data =
    ram_fflags_bits_uop_prs1_busy[ram_fflags_bits_uop_prs1_busy__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_prs1_busy__T_64_data = ram_fflags_bits_uop_prs1_busy__T_64_addr >= 3'h5 ? _RAND_89[0:0] :
    ram_fflags_bits_uop_prs1_busy[ram_fflags_bits_uop_prs1_busy__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs1_busy__T_53_data = io_enq_bits_fflags_bits_uop_prs1_busy;
  assign ram_fflags_bits_uop_prs1_busy__T_53_addr = value;
  assign ram_fflags_bits_uop_prs1_busy__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_prs1_busy__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_prs2_busy__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_prs2_busy__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs2_busy__T_64_data =
    ram_fflags_bits_uop_prs2_busy[ram_fflags_bits_uop_prs2_busy__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_prs2_busy__T_64_data = ram_fflags_bits_uop_prs2_busy__T_64_addr >= 3'h5 ? _RAND_91[0:0] :
    ram_fflags_bits_uop_prs2_busy[ram_fflags_bits_uop_prs2_busy__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs2_busy__T_53_data = io_enq_bits_fflags_bits_uop_prs2_busy;
  assign ram_fflags_bits_uop_prs2_busy__T_53_addr = value;
  assign ram_fflags_bits_uop_prs2_busy__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_prs2_busy__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_prs3_busy__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_prs3_busy__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs3_busy__T_64_data =
    ram_fflags_bits_uop_prs3_busy[ram_fflags_bits_uop_prs3_busy__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_prs3_busy__T_64_data = ram_fflags_bits_uop_prs3_busy__T_64_addr >= 3'h5 ? _RAND_93[0:0] :
    ram_fflags_bits_uop_prs3_busy[ram_fflags_bits_uop_prs3_busy__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_prs3_busy__T_53_data = io_enq_bits_fflags_bits_uop_prs3_busy;
  assign ram_fflags_bits_uop_prs3_busy__T_53_addr = value;
  assign ram_fflags_bits_uop_prs3_busy__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_prs3_busy__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ppred_busy__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ppred_busy__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ppred_busy__T_64_data =
    ram_fflags_bits_uop_ppred_busy[ram_fflags_bits_uop_ppred_busy__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ppred_busy__T_64_data = ram_fflags_bits_uop_ppred_busy__T_64_addr >= 3'h5 ? _RAND_95[0:0]
     : ram_fflags_bits_uop_ppred_busy[ram_fflags_bits_uop_ppred_busy__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ppred_busy__T_53_data = io_enq_bits_fflags_bits_uop_ppred_busy;
  assign ram_fflags_bits_uop_ppred_busy__T_53_addr = value;
  assign ram_fflags_bits_uop_ppred_busy__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ppred_busy__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_stale_pdst__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_stale_pdst__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_stale_pdst__T_64_data =
    ram_fflags_bits_uop_stale_pdst[ram_fflags_bits_uop_stale_pdst__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_stale_pdst__T_64_data = ram_fflags_bits_uop_stale_pdst__T_64_addr >= 3'h5 ? _RAND_97[5:0]
     : ram_fflags_bits_uop_stale_pdst[ram_fflags_bits_uop_stale_pdst__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_stale_pdst__T_53_data = io_enq_bits_fflags_bits_uop_stale_pdst;
  assign ram_fflags_bits_uop_stale_pdst__T_53_addr = value;
  assign ram_fflags_bits_uop_stale_pdst__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_stale_pdst__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_exception__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_exception__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_exception__T_64_data =
    ram_fflags_bits_uop_exception[ram_fflags_bits_uop_exception__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_exception__T_64_data = ram_fflags_bits_uop_exception__T_64_addr >= 3'h5 ? _RAND_99[0:0] :
    ram_fflags_bits_uop_exception[ram_fflags_bits_uop_exception__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_exception__T_53_data = io_enq_bits_fflags_bits_uop_exception;
  assign ram_fflags_bits_uop_exception__T_53_addr = value;
  assign ram_fflags_bits_uop_exception__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_exception__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_exc_cause__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_exc_cause__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_exc_cause__T_64_data =
    ram_fflags_bits_uop_exc_cause[ram_fflags_bits_uop_exc_cause__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_exc_cause__T_64_data = ram_fflags_bits_uop_exc_cause__T_64_addr >= 3'h5 ? _RAND_101[63:0]
     : ram_fflags_bits_uop_exc_cause[ram_fflags_bits_uop_exc_cause__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_exc_cause__T_53_data = io_enq_bits_fflags_bits_uop_exc_cause;
  assign ram_fflags_bits_uop_exc_cause__T_53_addr = value;
  assign ram_fflags_bits_uop_exc_cause__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_exc_cause__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_bypassable__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_bypassable__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_bypassable__T_64_data =
    ram_fflags_bits_uop_bypassable[ram_fflags_bits_uop_bypassable__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_bypassable__T_64_data = ram_fflags_bits_uop_bypassable__T_64_addr >= 3'h5 ? _RAND_103[0:0]
     : ram_fflags_bits_uop_bypassable[ram_fflags_bits_uop_bypassable__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_bypassable__T_53_data = io_enq_bits_fflags_bits_uop_bypassable;
  assign ram_fflags_bits_uop_bypassable__T_53_addr = value;
  assign ram_fflags_bits_uop_bypassable__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_bypassable__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_mem_cmd__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_mem_cmd__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_mem_cmd__T_64_data = ram_fflags_bits_uop_mem_cmd[ram_fflags_bits_uop_mem_cmd__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_mem_cmd__T_64_data = ram_fflags_bits_uop_mem_cmd__T_64_addr >= 3'h5 ? _RAND_105[4:0] :
    ram_fflags_bits_uop_mem_cmd[ram_fflags_bits_uop_mem_cmd__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_mem_cmd__T_53_data = io_enq_bits_fflags_bits_uop_mem_cmd;
  assign ram_fflags_bits_uop_mem_cmd__T_53_addr = value;
  assign ram_fflags_bits_uop_mem_cmd__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_mem_cmd__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_mem_size__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_mem_size__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_mem_size__T_64_data = ram_fflags_bits_uop_mem_size[ram_fflags_bits_uop_mem_size__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_mem_size__T_64_data = ram_fflags_bits_uop_mem_size__T_64_addr >= 3'h5 ? _RAND_107[1:0] :
    ram_fflags_bits_uop_mem_size[ram_fflags_bits_uop_mem_size__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_mem_size__T_53_data = io_enq_bits_fflags_bits_uop_mem_size;
  assign ram_fflags_bits_uop_mem_size__T_53_addr = value;
  assign ram_fflags_bits_uop_mem_size__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_mem_size__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_mem_signed__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_mem_signed__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_mem_signed__T_64_data =
    ram_fflags_bits_uop_mem_signed[ram_fflags_bits_uop_mem_signed__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_mem_signed__T_64_data = ram_fflags_bits_uop_mem_signed__T_64_addr >= 3'h5 ? _RAND_109[0:0]
     : ram_fflags_bits_uop_mem_signed[ram_fflags_bits_uop_mem_signed__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_mem_signed__T_53_data = io_enq_bits_fflags_bits_uop_mem_signed;
  assign ram_fflags_bits_uop_mem_signed__T_53_addr = value;
  assign ram_fflags_bits_uop_mem_signed__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_mem_signed__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_fence__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_fence__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_fence__T_64_data = ram_fflags_bits_uop_is_fence[ram_fflags_bits_uop_is_fence__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_fence__T_64_data = ram_fflags_bits_uop_is_fence__T_64_addr >= 3'h5 ? _RAND_111[0:0] :
    ram_fflags_bits_uop_is_fence[ram_fflags_bits_uop_is_fence__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_fence__T_53_data = io_enq_bits_fflags_bits_uop_is_fence;
  assign ram_fflags_bits_uop_is_fence__T_53_addr = value;
  assign ram_fflags_bits_uop_is_fence__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_fence__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_fencei__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_fencei__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_fencei__T_64_data =
    ram_fflags_bits_uop_is_fencei[ram_fflags_bits_uop_is_fencei__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_fencei__T_64_data = ram_fflags_bits_uop_is_fencei__T_64_addr >= 3'h5 ? _RAND_113[0:0] :
    ram_fflags_bits_uop_is_fencei[ram_fflags_bits_uop_is_fencei__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_fencei__T_53_data = io_enq_bits_fflags_bits_uop_is_fencei;
  assign ram_fflags_bits_uop_is_fencei__T_53_addr = value;
  assign ram_fflags_bits_uop_is_fencei__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_fencei__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_amo__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_amo__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_amo__T_64_data = ram_fflags_bits_uop_is_amo[ram_fflags_bits_uop_is_amo__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_amo__T_64_data = ram_fflags_bits_uop_is_amo__T_64_addr >= 3'h5 ? _RAND_115[0:0] :
    ram_fflags_bits_uop_is_amo[ram_fflags_bits_uop_is_amo__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_amo__T_53_data = io_enq_bits_fflags_bits_uop_is_amo;
  assign ram_fflags_bits_uop_is_amo__T_53_addr = value;
  assign ram_fflags_bits_uop_is_amo__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_amo__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_uses_ldq__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_uses_ldq__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_uses_ldq__T_64_data = ram_fflags_bits_uop_uses_ldq[ram_fflags_bits_uop_uses_ldq__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_uses_ldq__T_64_data = ram_fflags_bits_uop_uses_ldq__T_64_addr >= 3'h5 ? _RAND_117[0:0] :
    ram_fflags_bits_uop_uses_ldq[ram_fflags_bits_uop_uses_ldq__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_uses_ldq__T_53_data = io_enq_bits_fflags_bits_uop_uses_ldq;
  assign ram_fflags_bits_uop_uses_ldq__T_53_addr = value;
  assign ram_fflags_bits_uop_uses_ldq__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_uses_ldq__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_uses_stq__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_uses_stq__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_uses_stq__T_64_data = ram_fflags_bits_uop_uses_stq[ram_fflags_bits_uop_uses_stq__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_uses_stq__T_64_data = ram_fflags_bits_uop_uses_stq__T_64_addr >= 3'h5 ? _RAND_119[0:0] :
    ram_fflags_bits_uop_uses_stq[ram_fflags_bits_uop_uses_stq__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_uses_stq__T_53_data = io_enq_bits_fflags_bits_uop_uses_stq;
  assign ram_fflags_bits_uop_uses_stq__T_53_addr = value;
  assign ram_fflags_bits_uop_uses_stq__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_uses_stq__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_64_data =
    ram_fflags_bits_uop_is_sys_pc2epc[ram_fflags_bits_uop_is_sys_pc2epc__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_64_data = ram_fflags_bits_uop_is_sys_pc2epc__T_64_addr >= 3'h5 ? _RAND_121
    [0:0] : ram_fflags_bits_uop_is_sys_pc2epc[ram_fflags_bits_uop_is_sys_pc2epc__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_53_data = io_enq_bits_fflags_bits_uop_is_sys_pc2epc;
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_53_addr = value;
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_sys_pc2epc__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_is_unique__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_is_unique__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_unique__T_64_data =
    ram_fflags_bits_uop_is_unique[ram_fflags_bits_uop_is_unique__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_is_unique__T_64_data = ram_fflags_bits_uop_is_unique__T_64_addr >= 3'h5 ? _RAND_123[0:0] :
    ram_fflags_bits_uop_is_unique[ram_fflags_bits_uop_is_unique__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_is_unique__T_53_data = io_enq_bits_fflags_bits_uop_is_unique;
  assign ram_fflags_bits_uop_is_unique__T_53_addr = value;
  assign ram_fflags_bits_uop_is_unique__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_is_unique__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_flush_on_commit__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_flush_on_commit__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_flush_on_commit__T_64_data =
    ram_fflags_bits_uop_flush_on_commit[ram_fflags_bits_uop_flush_on_commit__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_flush_on_commit__T_64_data = ram_fflags_bits_uop_flush_on_commit__T_64_addr >= 3'h5 ?
    _RAND_125[0:0] : ram_fflags_bits_uop_flush_on_commit[ram_fflags_bits_uop_flush_on_commit__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_flush_on_commit__T_53_data = io_enq_bits_fflags_bits_uop_flush_on_commit;
  assign ram_fflags_bits_uop_flush_on_commit__T_53_addr = value;
  assign ram_fflags_bits_uop_flush_on_commit__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_flush_on_commit__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ldst_is_rs1__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ldst_is_rs1__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldst_is_rs1__T_64_data =
    ram_fflags_bits_uop_ldst_is_rs1[ram_fflags_bits_uop_ldst_is_rs1__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ldst_is_rs1__T_64_data = ram_fflags_bits_uop_ldst_is_rs1__T_64_addr >= 3'h5 ? _RAND_127[0
    :0] : ram_fflags_bits_uop_ldst_is_rs1[ram_fflags_bits_uop_ldst_is_rs1__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldst_is_rs1__T_53_data = io_enq_bits_fflags_bits_uop_ldst_is_rs1;
  assign ram_fflags_bits_uop_ldst_is_rs1__T_53_addr = value;
  assign ram_fflags_bits_uop_ldst_is_rs1__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ldst_is_rs1__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ldst__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ldst__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldst__T_64_data = ram_fflags_bits_uop_ldst[ram_fflags_bits_uop_ldst__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ldst__T_64_data = ram_fflags_bits_uop_ldst__T_64_addr >= 3'h5 ? _RAND_129[5:0] :
    ram_fflags_bits_uop_ldst[ram_fflags_bits_uop_ldst__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldst__T_53_data = io_enq_bits_fflags_bits_uop_ldst;
  assign ram_fflags_bits_uop_ldst__T_53_addr = value;
  assign ram_fflags_bits_uop_ldst__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ldst__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_lrs1__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_lrs1__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs1__T_64_data = ram_fflags_bits_uop_lrs1[ram_fflags_bits_uop_lrs1__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_lrs1__T_64_data = ram_fflags_bits_uop_lrs1__T_64_addr >= 3'h5 ? _RAND_131[5:0] :
    ram_fflags_bits_uop_lrs1[ram_fflags_bits_uop_lrs1__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs1__T_53_data = io_enq_bits_fflags_bits_uop_lrs1;
  assign ram_fflags_bits_uop_lrs1__T_53_addr = value;
  assign ram_fflags_bits_uop_lrs1__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_lrs1__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_lrs2__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_lrs2__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs2__T_64_data = ram_fflags_bits_uop_lrs2[ram_fflags_bits_uop_lrs2__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_lrs2__T_64_data = ram_fflags_bits_uop_lrs2__T_64_addr >= 3'h5 ? _RAND_133[5:0] :
    ram_fflags_bits_uop_lrs2[ram_fflags_bits_uop_lrs2__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs2__T_53_data = io_enq_bits_fflags_bits_uop_lrs2;
  assign ram_fflags_bits_uop_lrs2__T_53_addr = value;
  assign ram_fflags_bits_uop_lrs2__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_lrs2__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_lrs3__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_lrs3__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs3__T_64_data = ram_fflags_bits_uop_lrs3[ram_fflags_bits_uop_lrs3__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_lrs3__T_64_data = ram_fflags_bits_uop_lrs3__T_64_addr >= 3'h5 ? _RAND_135[5:0] :
    ram_fflags_bits_uop_lrs3[ram_fflags_bits_uop_lrs3__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs3__T_53_data = io_enq_bits_fflags_bits_uop_lrs3;
  assign ram_fflags_bits_uop_lrs3__T_53_addr = value;
  assign ram_fflags_bits_uop_lrs3__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_lrs3__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_ldst_val__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_ldst_val__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldst_val__T_64_data = ram_fflags_bits_uop_ldst_val[ram_fflags_bits_uop_ldst_val__T_64_addr]
    ; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_ldst_val__T_64_data = ram_fflags_bits_uop_ldst_val__T_64_addr >= 3'h5 ? _RAND_137[0:0] :
    ram_fflags_bits_uop_ldst_val[ram_fflags_bits_uop_ldst_val__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_ldst_val__T_53_data = io_enq_bits_fflags_bits_uop_ldst_val;
  assign ram_fflags_bits_uop_ldst_val__T_53_addr = value;
  assign ram_fflags_bits_uop_ldst_val__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_ldst_val__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_dst_rtype__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_dst_rtype__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_dst_rtype__T_64_data =
    ram_fflags_bits_uop_dst_rtype[ram_fflags_bits_uop_dst_rtype__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_dst_rtype__T_64_data = ram_fflags_bits_uop_dst_rtype__T_64_addr >= 3'h5 ? _RAND_139[1:0] :
    ram_fflags_bits_uop_dst_rtype[ram_fflags_bits_uop_dst_rtype__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_dst_rtype__T_53_data = io_enq_bits_fflags_bits_uop_dst_rtype;
  assign ram_fflags_bits_uop_dst_rtype__T_53_addr = value;
  assign ram_fflags_bits_uop_dst_rtype__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_dst_rtype__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_lrs1_rtype__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_lrs1_rtype__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs1_rtype__T_64_data =
    ram_fflags_bits_uop_lrs1_rtype[ram_fflags_bits_uop_lrs1_rtype__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_lrs1_rtype__T_64_data = ram_fflags_bits_uop_lrs1_rtype__T_64_addr >= 3'h5 ? _RAND_141[1:0]
     : ram_fflags_bits_uop_lrs1_rtype[ram_fflags_bits_uop_lrs1_rtype__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs1_rtype__T_53_data = io_enq_bits_fflags_bits_uop_lrs1_rtype;
  assign ram_fflags_bits_uop_lrs1_rtype__T_53_addr = value;
  assign ram_fflags_bits_uop_lrs1_rtype__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_lrs1_rtype__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_lrs2_rtype__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_lrs2_rtype__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs2_rtype__T_64_data =
    ram_fflags_bits_uop_lrs2_rtype[ram_fflags_bits_uop_lrs2_rtype__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_lrs2_rtype__T_64_data = ram_fflags_bits_uop_lrs2_rtype__T_64_addr >= 3'h5 ? _RAND_143[1:0]
     : ram_fflags_bits_uop_lrs2_rtype[ram_fflags_bits_uop_lrs2_rtype__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_lrs2_rtype__T_53_data = io_enq_bits_fflags_bits_uop_lrs2_rtype;
  assign ram_fflags_bits_uop_lrs2_rtype__T_53_addr = value;
  assign ram_fflags_bits_uop_lrs2_rtype__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_lrs2_rtype__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_frs3_en__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_frs3_en__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_frs3_en__T_64_data = ram_fflags_bits_uop_frs3_en[ram_fflags_bits_uop_frs3_en__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_frs3_en__T_64_data = ram_fflags_bits_uop_frs3_en__T_64_addr >= 3'h5 ? _RAND_145[0:0] :
    ram_fflags_bits_uop_frs3_en[ram_fflags_bits_uop_frs3_en__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_frs3_en__T_53_data = io_enq_bits_fflags_bits_uop_frs3_en;
  assign ram_fflags_bits_uop_frs3_en__T_53_addr = value;
  assign ram_fflags_bits_uop_frs3_en__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_frs3_en__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_fp_val__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_fp_val__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_fp_val__T_64_data = ram_fflags_bits_uop_fp_val[ram_fflags_bits_uop_fp_val__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_fp_val__T_64_data = ram_fflags_bits_uop_fp_val__T_64_addr >= 3'h5 ? _RAND_147[0:0] :
    ram_fflags_bits_uop_fp_val[ram_fflags_bits_uop_fp_val__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_fp_val__T_53_data = io_enq_bits_fflags_bits_uop_fp_val;
  assign ram_fflags_bits_uop_fp_val__T_53_addr = value;
  assign ram_fflags_bits_uop_fp_val__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_fp_val__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_fp_single__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_fp_single__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_fp_single__T_64_data =
    ram_fflags_bits_uop_fp_single[ram_fflags_bits_uop_fp_single__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_fp_single__T_64_data = ram_fflags_bits_uop_fp_single__T_64_addr >= 3'h5 ? _RAND_149[0:0] :
    ram_fflags_bits_uop_fp_single[ram_fflags_bits_uop_fp_single__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_fp_single__T_53_data = io_enq_bits_fflags_bits_uop_fp_single;
  assign ram_fflags_bits_uop_fp_single__T_53_addr = value;
  assign ram_fflags_bits_uop_fp_single__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_fp_single__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_xcpt_pf_if__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_xcpt_pf_if__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_xcpt_pf_if__T_64_data =
    ram_fflags_bits_uop_xcpt_pf_if[ram_fflags_bits_uop_xcpt_pf_if__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_xcpt_pf_if__T_64_data = ram_fflags_bits_uop_xcpt_pf_if__T_64_addr >= 3'h5 ? _RAND_151[0:0]
     : ram_fflags_bits_uop_xcpt_pf_if[ram_fflags_bits_uop_xcpt_pf_if__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_xcpt_pf_if__T_53_data = io_enq_bits_fflags_bits_uop_xcpt_pf_if;
  assign ram_fflags_bits_uop_xcpt_pf_if__T_53_addr = value;
  assign ram_fflags_bits_uop_xcpt_pf_if__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_xcpt_pf_if__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_xcpt_ae_if__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_xcpt_ae_if__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_xcpt_ae_if__T_64_data =
    ram_fflags_bits_uop_xcpt_ae_if[ram_fflags_bits_uop_xcpt_ae_if__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_xcpt_ae_if__T_64_data = ram_fflags_bits_uop_xcpt_ae_if__T_64_addr >= 3'h5 ? _RAND_153[0:0]
     : ram_fflags_bits_uop_xcpt_ae_if[ram_fflags_bits_uop_xcpt_ae_if__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_xcpt_ae_if__T_53_data = io_enq_bits_fflags_bits_uop_xcpt_ae_if;
  assign ram_fflags_bits_uop_xcpt_ae_if__T_53_addr = value;
  assign ram_fflags_bits_uop_xcpt_ae_if__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_xcpt_ae_if__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_xcpt_ma_if__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_xcpt_ma_if__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_xcpt_ma_if__T_64_data =
    ram_fflags_bits_uop_xcpt_ma_if[ram_fflags_bits_uop_xcpt_ma_if__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_xcpt_ma_if__T_64_data = ram_fflags_bits_uop_xcpt_ma_if__T_64_addr >= 3'h5 ? _RAND_155[0:0]
     : ram_fflags_bits_uop_xcpt_ma_if[ram_fflags_bits_uop_xcpt_ma_if__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_xcpt_ma_if__T_53_data = io_enq_bits_fflags_bits_uop_xcpt_ma_if;
  assign ram_fflags_bits_uop_xcpt_ma_if__T_53_addr = value;
  assign ram_fflags_bits_uop_xcpt_ma_if__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_xcpt_ma_if__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_bp_debug_if__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_bp_debug_if__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_bp_debug_if__T_64_data =
    ram_fflags_bits_uop_bp_debug_if[ram_fflags_bits_uop_bp_debug_if__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_bp_debug_if__T_64_data = ram_fflags_bits_uop_bp_debug_if__T_64_addr >= 3'h5 ? _RAND_157[0
    :0] : ram_fflags_bits_uop_bp_debug_if[ram_fflags_bits_uop_bp_debug_if__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_bp_debug_if__T_53_data = io_enq_bits_fflags_bits_uop_bp_debug_if;
  assign ram_fflags_bits_uop_bp_debug_if__T_53_addr = value;
  assign ram_fflags_bits_uop_bp_debug_if__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_bp_debug_if__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_bp_xcpt_if__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_bp_xcpt_if__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_bp_xcpt_if__T_64_data =
    ram_fflags_bits_uop_bp_xcpt_if[ram_fflags_bits_uop_bp_xcpt_if__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_bp_xcpt_if__T_64_data = ram_fflags_bits_uop_bp_xcpt_if__T_64_addr >= 3'h5 ? _RAND_159[0:0]
     : ram_fflags_bits_uop_bp_xcpt_if[ram_fflags_bits_uop_bp_xcpt_if__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_bp_xcpt_if__T_53_data = io_enq_bits_fflags_bits_uop_bp_xcpt_if;
  assign ram_fflags_bits_uop_bp_xcpt_if__T_53_addr = value;
  assign ram_fflags_bits_uop_bp_xcpt_if__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_bp_xcpt_if__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_debug_fsrc__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_debug_fsrc__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_fsrc__T_64_data =
    ram_fflags_bits_uop_debug_fsrc[ram_fflags_bits_uop_debug_fsrc__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_debug_fsrc__T_64_data = ram_fflags_bits_uop_debug_fsrc__T_64_addr >= 3'h5 ? _RAND_161[1:0]
     : ram_fflags_bits_uop_debug_fsrc[ram_fflags_bits_uop_debug_fsrc__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_fsrc__T_53_data = io_enq_bits_fflags_bits_uop_debug_fsrc;
  assign ram_fflags_bits_uop_debug_fsrc__T_53_addr = value;
  assign ram_fflags_bits_uop_debug_fsrc__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_debug_fsrc__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_uop_debug_tsrc__T_64_en = 1'h1;
  assign ram_fflags_bits_uop_debug_tsrc__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_tsrc__T_64_data =
    ram_fflags_bits_uop_debug_tsrc[ram_fflags_bits_uop_debug_tsrc__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_uop_debug_tsrc__T_64_data = ram_fflags_bits_uop_debug_tsrc__T_64_addr >= 3'h5 ? _RAND_163[1:0]
     : ram_fflags_bits_uop_debug_tsrc[ram_fflags_bits_uop_debug_tsrc__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_uop_debug_tsrc__T_53_data = io_enq_bits_fflags_bits_uop_debug_tsrc;
  assign ram_fflags_bits_uop_debug_tsrc__T_53_addr = value;
  assign ram_fflags_bits_uop_debug_tsrc__T_53_mask = 1'h1;
  assign ram_fflags_bits_uop_debug_tsrc__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign ram_fflags_bits_flags__T_64_en = 1'h1;
  assign ram_fflags_bits_flags__T_64_addr = value_1;
  `ifndef RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_flags__T_64_data = ram_fflags_bits_flags[ram_fflags_bits_flags__T_64_addr]; // @[util.scala 464:20]
  `else
  assign ram_fflags_bits_flags__T_64_data = ram_fflags_bits_flags__T_64_addr >= 3'h5 ? _RAND_165[4:0] :
    ram_fflags_bits_flags[ram_fflags_bits_flags__T_64_addr]; // @[util.scala 464:20]
  `endif // RANDOMIZE_GARBAGE_ASSIGN
  assign ram_fflags_bits_flags__T_53_data = io_enq_bits_fflags_bits_flags;
  assign ram_fflags_bits_flags__T_53_addr = value;
  assign ram_fflags_bits_flags__T_53_mask = 1'h1;
  assign ram_fflags_bits_flags__T_53_en = io_empty ? _GEN_1391 : _T_3;
  assign io_enq_ready = ~full; // @[util.scala 504:19]
  assign io_deq_valid = io_empty ? io_enq_valid : _T_6 & _GEN_4 & ~_T_68 & _T_13; // @[util.scala 515:21 516:20 509:27]
  assign io_deq_bits_uop_uopc = io_empty ? io_enq_bits_uop_uopc : out_uop_uopc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_inst = io_empty ? io_enq_bits_uop_inst : out_uop_inst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_debug_inst = io_empty ? io_enq_bits_uop_debug_inst : out_uop_debug_inst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_rvc = io_empty ? io_enq_bits_uop_is_rvc : out_uop_is_rvc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_debug_pc = io_empty ? io_enq_bits_uop_debug_pc : out_uop_debug_pc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_iq_type = io_empty ? io_enq_bits_uop_iq_type : out_uop_iq_type; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_fu_code = io_empty ? io_enq_bits_uop_fu_code : out_uop_fu_code; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_br_type = io_empty ? io_enq_bits_uop_ctrl_br_type : out_uop_ctrl_br_type; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_op1_sel = io_empty ? io_enq_bits_uop_ctrl_op1_sel : out_uop_ctrl_op1_sel; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_op2_sel = io_empty ? io_enq_bits_uop_ctrl_op2_sel : out_uop_ctrl_op2_sel; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_imm_sel = io_empty ? io_enq_bits_uop_ctrl_imm_sel : out_uop_ctrl_imm_sel; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_op_fcn = io_empty ? io_enq_bits_uop_ctrl_op_fcn : out_uop_ctrl_op_fcn; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_fcn_dw = io_empty ? io_enq_bits_uop_ctrl_fcn_dw : out_uop_ctrl_fcn_dw; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_csr_cmd = io_empty ? io_enq_bits_uop_ctrl_csr_cmd : out_uop_ctrl_csr_cmd; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_is_load = io_empty ? io_enq_bits_uop_ctrl_is_load : out_uop_ctrl_is_load; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_is_sta = io_empty ? io_enq_bits_uop_ctrl_is_sta : out_uop_ctrl_is_sta; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ctrl_is_std = io_empty ? io_enq_bits_uop_ctrl_is_std : out_uop_ctrl_is_std; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_iw_state = io_empty ? io_enq_bits_uop_iw_state : out_uop_iw_state; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_iw_p1_poisoned = io_empty ? io_enq_bits_uop_iw_p1_poisoned : out_uop_iw_p1_poisoned; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_iw_p2_poisoned = io_empty ? io_enq_bits_uop_iw_p2_poisoned : out_uop_iw_p2_poisoned; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_br = io_empty ? io_enq_bits_uop_is_br : out_uop_is_br; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_jalr = io_empty ? io_enq_bits_uop_is_jalr : out_uop_is_jalr; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_jal = io_empty ? io_enq_bits_uop_is_jal : out_uop_is_jal; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_sfb = io_empty ? io_enq_bits_uop_is_sfb : out_uop_is_sfb; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_br_mask = io_empty ? _T_55 : _T_75; // @[util.scala 515:21 511:27 518:31]
  assign io_deq_bits_uop_br_tag = io_empty ? io_enq_bits_uop_br_tag : out_uop_br_tag; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ftq_idx = io_empty ? io_enq_bits_uop_ftq_idx : out_uop_ftq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_edge_inst = io_empty ? io_enq_bits_uop_edge_inst : out_uop_edge_inst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_pc_lob = io_empty ? io_enq_bits_uop_pc_lob : out_uop_pc_lob; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_taken = io_empty ? io_enq_bits_uop_taken : out_uop_taken; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_imm_packed = io_empty ? io_enq_bits_uop_imm_packed : out_uop_imm_packed; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_csr_addr = io_empty ? io_enq_bits_uop_csr_addr : out_uop_csr_addr; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_rob_idx = io_empty ? io_enq_bits_uop_rob_idx : out_uop_rob_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ldq_idx = io_empty ? io_enq_bits_uop_ldq_idx : out_uop_ldq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_stq_idx = io_empty ? io_enq_bits_uop_stq_idx : out_uop_stq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_rxq_idx = io_empty ? io_enq_bits_uop_rxq_idx : out_uop_rxq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_pdst = io_empty ? io_enq_bits_uop_pdst : out_uop_pdst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_prs1 = io_empty ? io_enq_bits_uop_prs1 : out_uop_prs1; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_prs2 = io_empty ? io_enq_bits_uop_prs2 : out_uop_prs2; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_prs3 = io_empty ? io_enq_bits_uop_prs3 : out_uop_prs3; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ppred = io_empty ? io_enq_bits_uop_ppred : out_uop_ppred; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_prs1_busy = io_empty ? io_enq_bits_uop_prs1_busy : out_uop_prs1_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_prs2_busy = io_empty ? io_enq_bits_uop_prs2_busy : out_uop_prs2_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_prs3_busy = io_empty ? io_enq_bits_uop_prs3_busy : out_uop_prs3_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ppred_busy = io_empty ? io_enq_bits_uop_ppred_busy : out_uop_ppred_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_stale_pdst = io_empty ? io_enq_bits_uop_stale_pdst : out_uop_stale_pdst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_exception = io_empty ? io_enq_bits_uop_exception : out_uop_exception; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_exc_cause = io_empty ? io_enq_bits_uop_exc_cause : out_uop_exc_cause; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_bypassable = io_empty ? io_enq_bits_uop_bypassable : out_uop_bypassable; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_mem_cmd = io_empty ? io_enq_bits_uop_mem_cmd : out_uop_mem_cmd; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_mem_size = io_empty ? io_enq_bits_uop_mem_size : out_uop_mem_size; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_mem_signed = io_empty ? io_enq_bits_uop_mem_signed : out_uop_mem_signed; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_fence = io_empty ? io_enq_bits_uop_is_fence : out_uop_is_fence; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_fencei = io_empty ? io_enq_bits_uop_is_fencei : out_uop_is_fencei; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_amo = io_empty ? io_enq_bits_uop_is_amo : out_uop_is_amo; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_uses_ldq = io_empty ? io_enq_bits_uop_uses_ldq : out_uop_uses_ldq; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_uses_stq = io_empty ? io_enq_bits_uop_uses_stq : out_uop_uses_stq; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_sys_pc2epc = io_empty ? io_enq_bits_uop_is_sys_pc2epc : out_uop_is_sys_pc2epc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_is_unique = io_empty ? io_enq_bits_uop_is_unique : out_uop_is_unique; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_flush_on_commit = io_empty ? io_enq_bits_uop_flush_on_commit : out_uop_flush_on_commit; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ldst_is_rs1 = io_empty ? io_enq_bits_uop_ldst_is_rs1 : out_uop_ldst_is_rs1; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ldst = io_empty ? io_enq_bits_uop_ldst : out_uop_ldst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_lrs1 = io_empty ? io_enq_bits_uop_lrs1 : out_uop_lrs1; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_lrs2 = io_empty ? io_enq_bits_uop_lrs2 : out_uop_lrs2; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_lrs3 = io_empty ? io_enq_bits_uop_lrs3 : out_uop_lrs3; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_ldst_val = io_empty ? io_enq_bits_uop_ldst_val : out_uop_ldst_val; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_dst_rtype = io_empty ? io_enq_bits_uop_dst_rtype : out_uop_dst_rtype; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_lrs1_rtype = io_empty ? io_enq_bits_uop_lrs1_rtype : out_uop_lrs1_rtype; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_lrs2_rtype = io_empty ? io_enq_bits_uop_lrs2_rtype : out_uop_lrs2_rtype; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_frs3_en = io_empty ? io_enq_bits_uop_frs3_en : out_uop_frs3_en; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_fp_val = io_empty ? io_enq_bits_uop_fp_val : out_uop_fp_val; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_fp_single = io_empty ? io_enq_bits_uop_fp_single : out_uop_fp_single; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_xcpt_pf_if = io_empty ? io_enq_bits_uop_xcpt_pf_if : out_uop_xcpt_pf_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_xcpt_ae_if = io_empty ? io_enq_bits_uop_xcpt_ae_if : out_uop_xcpt_ae_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_xcpt_ma_if = io_empty ? io_enq_bits_uop_xcpt_ma_if : out_uop_xcpt_ma_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_bp_debug_if = io_empty ? io_enq_bits_uop_bp_debug_if : out_uop_bp_debug_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_bp_xcpt_if = io_empty ? io_enq_bits_uop_bp_xcpt_if : out_uop_bp_xcpt_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_debug_fsrc = io_empty ? io_enq_bits_uop_debug_fsrc : out_uop_debug_fsrc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_uop_debug_tsrc = io_empty ? io_enq_bits_uop_debug_tsrc : out_uop_debug_tsrc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_data = io_empty ? io_enq_bits_data : out_data; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_predicated = io_empty ? io_enq_bits_predicated : out_predicated; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_valid = io_empty ? io_enq_bits_fflags_valid : out_fflags_valid; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_uopc = io_empty ? io_enq_bits_fflags_bits_uop_uopc : out_fflags_bits_uop_uopc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_inst = io_empty ? io_enq_bits_fflags_bits_uop_inst : out_fflags_bits_uop_inst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_debug_inst = io_empty ? io_enq_bits_fflags_bits_uop_debug_inst :
    out_fflags_bits_uop_debug_inst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_rvc = io_empty ? io_enq_bits_fflags_bits_uop_is_rvc : out_fflags_bits_uop_is_rvc
    ; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_debug_pc = io_empty ? io_enq_bits_fflags_bits_uop_debug_pc :
    out_fflags_bits_uop_debug_pc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_iq_type = io_empty ? io_enq_bits_fflags_bits_uop_iq_type :
    out_fflags_bits_uop_iq_type; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_fu_code = io_empty ? io_enq_bits_fflags_bits_uop_fu_code :
    out_fflags_bits_uop_fu_code; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_br_type = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_br_type :
    out_fflags_bits_uop_ctrl_br_type; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_op1_sel = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_op1_sel :
    out_fflags_bits_uop_ctrl_op1_sel; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_op2_sel = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_op2_sel :
    out_fflags_bits_uop_ctrl_op2_sel; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_imm_sel = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_imm_sel :
    out_fflags_bits_uop_ctrl_imm_sel; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_op_fcn = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_op_fcn :
    out_fflags_bits_uop_ctrl_op_fcn; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_fcn_dw = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_fcn_dw :
    out_fflags_bits_uop_ctrl_fcn_dw; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_csr_cmd = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_csr_cmd :
    out_fflags_bits_uop_ctrl_csr_cmd; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_is_load = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_is_load :
    out_fflags_bits_uop_ctrl_is_load; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_is_sta = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_is_sta :
    out_fflags_bits_uop_ctrl_is_sta; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ctrl_is_std = io_empty ? io_enq_bits_fflags_bits_uop_ctrl_is_std :
    out_fflags_bits_uop_ctrl_is_std; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_iw_state = io_empty ? io_enq_bits_fflags_bits_uop_iw_state :
    out_fflags_bits_uop_iw_state; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_iw_p1_poisoned = io_empty ? io_enq_bits_fflags_bits_uop_iw_p1_poisoned :
    out_fflags_bits_uop_iw_p1_poisoned; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_iw_p2_poisoned = io_empty ? io_enq_bits_fflags_bits_uop_iw_p2_poisoned :
    out_fflags_bits_uop_iw_p2_poisoned; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_br = io_empty ? io_enq_bits_fflags_bits_uop_is_br : out_fflags_bits_uop_is_br; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_jalr = io_empty ? io_enq_bits_fflags_bits_uop_is_jalr :
    out_fflags_bits_uop_is_jalr; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_jal = io_empty ? io_enq_bits_fflags_bits_uop_is_jal : out_fflags_bits_uop_is_jal
    ; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_sfb = io_empty ? io_enq_bits_fflags_bits_uop_is_sfb : out_fflags_bits_uop_is_sfb
    ; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_br_mask = io_empty ? io_enq_bits_fflags_bits_uop_br_mask :
    out_fflags_bits_uop_br_mask; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_br_tag = io_empty ? io_enq_bits_fflags_bits_uop_br_tag : out_fflags_bits_uop_br_tag
    ; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ftq_idx = io_empty ? io_enq_bits_fflags_bits_uop_ftq_idx :
    out_fflags_bits_uop_ftq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_edge_inst = io_empty ? io_enq_bits_fflags_bits_uop_edge_inst :
    out_fflags_bits_uop_edge_inst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_pc_lob = io_empty ? io_enq_bits_fflags_bits_uop_pc_lob : out_fflags_bits_uop_pc_lob
    ; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_taken = io_empty ? io_enq_bits_fflags_bits_uop_taken : out_fflags_bits_uop_taken; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_imm_packed = io_empty ? io_enq_bits_fflags_bits_uop_imm_packed :
    out_fflags_bits_uop_imm_packed; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_csr_addr = io_empty ? io_enq_bits_fflags_bits_uop_csr_addr :
    out_fflags_bits_uop_csr_addr; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_rob_idx = io_empty ? io_enq_bits_fflags_bits_uop_rob_idx :
    out_fflags_bits_uop_rob_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ldq_idx = io_empty ? io_enq_bits_fflags_bits_uop_ldq_idx :
    out_fflags_bits_uop_ldq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_stq_idx = io_empty ? io_enq_bits_fflags_bits_uop_stq_idx :
    out_fflags_bits_uop_stq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_rxq_idx = io_empty ? io_enq_bits_fflags_bits_uop_rxq_idx :
    out_fflags_bits_uop_rxq_idx; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_pdst = io_empty ? io_enq_bits_fflags_bits_uop_pdst : out_fflags_bits_uop_pdst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_prs1 = io_empty ? io_enq_bits_fflags_bits_uop_prs1 : out_fflags_bits_uop_prs1; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_prs2 = io_empty ? io_enq_bits_fflags_bits_uop_prs2 : out_fflags_bits_uop_prs2; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_prs3 = io_empty ? io_enq_bits_fflags_bits_uop_prs3 : out_fflags_bits_uop_prs3; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ppred = io_empty ? io_enq_bits_fflags_bits_uop_ppred : out_fflags_bits_uop_ppred; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_prs1_busy = io_empty ? io_enq_bits_fflags_bits_uop_prs1_busy :
    out_fflags_bits_uop_prs1_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_prs2_busy = io_empty ? io_enq_bits_fflags_bits_uop_prs2_busy :
    out_fflags_bits_uop_prs2_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_prs3_busy = io_empty ? io_enq_bits_fflags_bits_uop_prs3_busy :
    out_fflags_bits_uop_prs3_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ppred_busy = io_empty ? io_enq_bits_fflags_bits_uop_ppred_busy :
    out_fflags_bits_uop_ppred_busy; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_stale_pdst = io_empty ? io_enq_bits_fflags_bits_uop_stale_pdst :
    out_fflags_bits_uop_stale_pdst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_exception = io_empty ? io_enq_bits_fflags_bits_uop_exception :
    out_fflags_bits_uop_exception; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_exc_cause = io_empty ? io_enq_bits_fflags_bits_uop_exc_cause :
    out_fflags_bits_uop_exc_cause; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_bypassable = io_empty ? io_enq_bits_fflags_bits_uop_bypassable :
    out_fflags_bits_uop_bypassable; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_mem_cmd = io_empty ? io_enq_bits_fflags_bits_uop_mem_cmd :
    out_fflags_bits_uop_mem_cmd; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_mem_size = io_empty ? io_enq_bits_fflags_bits_uop_mem_size :
    out_fflags_bits_uop_mem_size; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_mem_signed = io_empty ? io_enq_bits_fflags_bits_uop_mem_signed :
    out_fflags_bits_uop_mem_signed; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_fence = io_empty ? io_enq_bits_fflags_bits_uop_is_fence :
    out_fflags_bits_uop_is_fence; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_fencei = io_empty ? io_enq_bits_fflags_bits_uop_is_fencei :
    out_fflags_bits_uop_is_fencei; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_amo = io_empty ? io_enq_bits_fflags_bits_uop_is_amo : out_fflags_bits_uop_is_amo
    ; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_uses_ldq = io_empty ? io_enq_bits_fflags_bits_uop_uses_ldq :
    out_fflags_bits_uop_uses_ldq; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_uses_stq = io_empty ? io_enq_bits_fflags_bits_uop_uses_stq :
    out_fflags_bits_uop_uses_stq; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_sys_pc2epc = io_empty ? io_enq_bits_fflags_bits_uop_is_sys_pc2epc :
    out_fflags_bits_uop_is_sys_pc2epc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_is_unique = io_empty ? io_enq_bits_fflags_bits_uop_is_unique :
    out_fflags_bits_uop_is_unique; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_flush_on_commit = io_empty ? io_enq_bits_fflags_bits_uop_flush_on_commit :
    out_fflags_bits_uop_flush_on_commit; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ldst_is_rs1 = io_empty ? io_enq_bits_fflags_bits_uop_ldst_is_rs1 :
    out_fflags_bits_uop_ldst_is_rs1; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ldst = io_empty ? io_enq_bits_fflags_bits_uop_ldst : out_fflags_bits_uop_ldst; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_lrs1 = io_empty ? io_enq_bits_fflags_bits_uop_lrs1 : out_fflags_bits_uop_lrs1; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_lrs2 = io_empty ? io_enq_bits_fflags_bits_uop_lrs2 : out_fflags_bits_uop_lrs2; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_lrs3 = io_empty ? io_enq_bits_fflags_bits_uop_lrs3 : out_fflags_bits_uop_lrs3; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_ldst_val = io_empty ? io_enq_bits_fflags_bits_uop_ldst_val :
    out_fflags_bits_uop_ldst_val; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_dst_rtype = io_empty ? io_enq_bits_fflags_bits_uop_dst_rtype :
    out_fflags_bits_uop_dst_rtype; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_lrs1_rtype = io_empty ? io_enq_bits_fflags_bits_uop_lrs1_rtype :
    out_fflags_bits_uop_lrs1_rtype; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_lrs2_rtype = io_empty ? io_enq_bits_fflags_bits_uop_lrs2_rtype :
    out_fflags_bits_uop_lrs2_rtype; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_frs3_en = io_empty ? io_enq_bits_fflags_bits_uop_frs3_en :
    out_fflags_bits_uop_frs3_en; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_fp_val = io_empty ? io_enq_bits_fflags_bits_uop_fp_val : out_fflags_bits_uop_fp_val
    ; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_fp_single = io_empty ? io_enq_bits_fflags_bits_uop_fp_single :
    out_fflags_bits_uop_fp_single; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_xcpt_pf_if = io_empty ? io_enq_bits_fflags_bits_uop_xcpt_pf_if :
    out_fflags_bits_uop_xcpt_pf_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_xcpt_ae_if = io_empty ? io_enq_bits_fflags_bits_uop_xcpt_ae_if :
    out_fflags_bits_uop_xcpt_ae_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_xcpt_ma_if = io_empty ? io_enq_bits_fflags_bits_uop_xcpt_ma_if :
    out_fflags_bits_uop_xcpt_ma_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_bp_debug_if = io_empty ? io_enq_bits_fflags_bits_uop_bp_debug_if :
    out_fflags_bits_uop_bp_debug_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_bp_xcpt_if = io_empty ? io_enq_bits_fflags_bits_uop_bp_xcpt_if :
    out_fflags_bits_uop_bp_xcpt_if; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_debug_fsrc = io_empty ? io_enq_bits_fflags_bits_uop_debug_fsrc :
    out_fflags_bits_uop_debug_fsrc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_uop_debug_tsrc = io_empty ? io_enq_bits_fflags_bits_uop_debug_tsrc :
    out_fflags_bits_uop_debug_tsrc; // @[util.scala 515:21 517:19 510:27]
  assign io_deq_bits_fflags_bits_flags = io_empty ? io_enq_bits_fflags_bits_flags : out_fflags_bits_flags; // @[util.scala 515:21 517:19 510:27]
  assign io_empty = ptr_match & ~maybe_full; // @[util.scala 473:25]
  assign io_count = ptr_match ? _T_80 : _T_84; // @[util.scala 529:20]
  always @(posedge clock) begin
    if (ram_data__T_53_en & ram_data__T_53_mask) begin
      ram_data[ram_data__T_53_addr] <= ram_data__T_53_data; // @[util.scala 464:20]
    end
    if (ram_predicated__T_53_en & ram_predicated__T_53_mask) begin
      ram_predicated[ram_predicated__T_53_addr] <= ram_predicated__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_valid__T_53_en & ram_fflags_valid__T_53_mask) begin
      ram_fflags_valid[ram_fflags_valid__T_53_addr] <= ram_fflags_valid__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_uopc__T_53_en & ram_fflags_bits_uop_uopc__T_53_mask) begin
      ram_fflags_bits_uop_uopc[ram_fflags_bits_uop_uopc__T_53_addr] <= ram_fflags_bits_uop_uopc__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_inst__T_53_en & ram_fflags_bits_uop_inst__T_53_mask) begin
      ram_fflags_bits_uop_inst[ram_fflags_bits_uop_inst__T_53_addr] <= ram_fflags_bits_uop_inst__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_debug_inst__T_53_en & ram_fflags_bits_uop_debug_inst__T_53_mask) begin
      ram_fflags_bits_uop_debug_inst[ram_fflags_bits_uop_debug_inst__T_53_addr] <=
        ram_fflags_bits_uop_debug_inst__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_rvc__T_53_en & ram_fflags_bits_uop_is_rvc__T_53_mask) begin
      ram_fflags_bits_uop_is_rvc[ram_fflags_bits_uop_is_rvc__T_53_addr] <= ram_fflags_bits_uop_is_rvc__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_debug_pc__T_53_en & ram_fflags_bits_uop_debug_pc__T_53_mask) begin
      ram_fflags_bits_uop_debug_pc[ram_fflags_bits_uop_debug_pc__T_53_addr] <= ram_fflags_bits_uop_debug_pc__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_iq_type__T_53_en & ram_fflags_bits_uop_iq_type__T_53_mask) begin
      ram_fflags_bits_uop_iq_type[ram_fflags_bits_uop_iq_type__T_53_addr] <= ram_fflags_bits_uop_iq_type__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_fu_code__T_53_en & ram_fflags_bits_uop_fu_code__T_53_mask) begin
      ram_fflags_bits_uop_fu_code[ram_fflags_bits_uop_fu_code__T_53_addr] <= ram_fflags_bits_uop_fu_code__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_br_type__T_53_en & ram_fflags_bits_uop_ctrl_br_type__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_br_type[ram_fflags_bits_uop_ctrl_br_type__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_br_type__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_op1_sel__T_53_en & ram_fflags_bits_uop_ctrl_op1_sel__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_op1_sel[ram_fflags_bits_uop_ctrl_op1_sel__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_op1_sel__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_op2_sel__T_53_en & ram_fflags_bits_uop_ctrl_op2_sel__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_op2_sel[ram_fflags_bits_uop_ctrl_op2_sel__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_op2_sel__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_imm_sel__T_53_en & ram_fflags_bits_uop_ctrl_imm_sel__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_imm_sel[ram_fflags_bits_uop_ctrl_imm_sel__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_imm_sel__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_op_fcn__T_53_en & ram_fflags_bits_uop_ctrl_op_fcn__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_op_fcn[ram_fflags_bits_uop_ctrl_op_fcn__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_op_fcn__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_fcn_dw__T_53_en & ram_fflags_bits_uop_ctrl_fcn_dw__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_fcn_dw[ram_fflags_bits_uop_ctrl_fcn_dw__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_fcn_dw__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_csr_cmd__T_53_en & ram_fflags_bits_uop_ctrl_csr_cmd__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_csr_cmd[ram_fflags_bits_uop_ctrl_csr_cmd__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_csr_cmd__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_is_load__T_53_en & ram_fflags_bits_uop_ctrl_is_load__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_is_load[ram_fflags_bits_uop_ctrl_is_load__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_is_load__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_is_sta__T_53_en & ram_fflags_bits_uop_ctrl_is_sta__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_is_sta[ram_fflags_bits_uop_ctrl_is_sta__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_is_sta__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ctrl_is_std__T_53_en & ram_fflags_bits_uop_ctrl_is_std__T_53_mask) begin
      ram_fflags_bits_uop_ctrl_is_std[ram_fflags_bits_uop_ctrl_is_std__T_53_addr] <=
        ram_fflags_bits_uop_ctrl_is_std__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_iw_state__T_53_en & ram_fflags_bits_uop_iw_state__T_53_mask) begin
      ram_fflags_bits_uop_iw_state[ram_fflags_bits_uop_iw_state__T_53_addr] <= ram_fflags_bits_uop_iw_state__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_iw_p1_poisoned__T_53_en & ram_fflags_bits_uop_iw_p1_poisoned__T_53_mask) begin
      ram_fflags_bits_uop_iw_p1_poisoned[ram_fflags_bits_uop_iw_p1_poisoned__T_53_addr] <=
        ram_fflags_bits_uop_iw_p1_poisoned__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_iw_p2_poisoned__T_53_en & ram_fflags_bits_uop_iw_p2_poisoned__T_53_mask) begin
      ram_fflags_bits_uop_iw_p2_poisoned[ram_fflags_bits_uop_iw_p2_poisoned__T_53_addr] <=
        ram_fflags_bits_uop_iw_p2_poisoned__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_br__T_53_en & ram_fflags_bits_uop_is_br__T_53_mask) begin
      ram_fflags_bits_uop_is_br[ram_fflags_bits_uop_is_br__T_53_addr] <= ram_fflags_bits_uop_is_br__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_jalr__T_53_en & ram_fflags_bits_uop_is_jalr__T_53_mask) begin
      ram_fflags_bits_uop_is_jalr[ram_fflags_bits_uop_is_jalr__T_53_addr] <= ram_fflags_bits_uop_is_jalr__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_jal__T_53_en & ram_fflags_bits_uop_is_jal__T_53_mask) begin
      ram_fflags_bits_uop_is_jal[ram_fflags_bits_uop_is_jal__T_53_addr] <= ram_fflags_bits_uop_is_jal__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_sfb__T_53_en & ram_fflags_bits_uop_is_sfb__T_53_mask) begin
      ram_fflags_bits_uop_is_sfb[ram_fflags_bits_uop_is_sfb__T_53_addr] <= ram_fflags_bits_uop_is_sfb__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_br_mask__T_53_en & ram_fflags_bits_uop_br_mask__T_53_mask) begin
      ram_fflags_bits_uop_br_mask[ram_fflags_bits_uop_br_mask__T_53_addr] <= ram_fflags_bits_uop_br_mask__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_br_tag__T_53_en & ram_fflags_bits_uop_br_tag__T_53_mask) begin
      ram_fflags_bits_uop_br_tag[ram_fflags_bits_uop_br_tag__T_53_addr] <= ram_fflags_bits_uop_br_tag__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ftq_idx__T_53_en & ram_fflags_bits_uop_ftq_idx__T_53_mask) begin
      ram_fflags_bits_uop_ftq_idx[ram_fflags_bits_uop_ftq_idx__T_53_addr] <= ram_fflags_bits_uop_ftq_idx__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_edge_inst__T_53_en & ram_fflags_bits_uop_edge_inst__T_53_mask) begin
      ram_fflags_bits_uop_edge_inst[ram_fflags_bits_uop_edge_inst__T_53_addr] <=
        ram_fflags_bits_uop_edge_inst__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_pc_lob__T_53_en & ram_fflags_bits_uop_pc_lob__T_53_mask) begin
      ram_fflags_bits_uop_pc_lob[ram_fflags_bits_uop_pc_lob__T_53_addr] <= ram_fflags_bits_uop_pc_lob__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_taken__T_53_en & ram_fflags_bits_uop_taken__T_53_mask) begin
      ram_fflags_bits_uop_taken[ram_fflags_bits_uop_taken__T_53_addr] <= ram_fflags_bits_uop_taken__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_imm_packed__T_53_en & ram_fflags_bits_uop_imm_packed__T_53_mask) begin
      ram_fflags_bits_uop_imm_packed[ram_fflags_bits_uop_imm_packed__T_53_addr] <=
        ram_fflags_bits_uop_imm_packed__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_csr_addr__T_53_en & ram_fflags_bits_uop_csr_addr__T_53_mask) begin
      ram_fflags_bits_uop_csr_addr[ram_fflags_bits_uop_csr_addr__T_53_addr] <= ram_fflags_bits_uop_csr_addr__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_rob_idx__T_53_en & ram_fflags_bits_uop_rob_idx__T_53_mask) begin
      ram_fflags_bits_uop_rob_idx[ram_fflags_bits_uop_rob_idx__T_53_addr] <= ram_fflags_bits_uop_rob_idx__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ldq_idx__T_53_en & ram_fflags_bits_uop_ldq_idx__T_53_mask) begin
      ram_fflags_bits_uop_ldq_idx[ram_fflags_bits_uop_ldq_idx__T_53_addr] <= ram_fflags_bits_uop_ldq_idx__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_stq_idx__T_53_en & ram_fflags_bits_uop_stq_idx__T_53_mask) begin
      ram_fflags_bits_uop_stq_idx[ram_fflags_bits_uop_stq_idx__T_53_addr] <= ram_fflags_bits_uop_stq_idx__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_rxq_idx__T_53_en & ram_fflags_bits_uop_rxq_idx__T_53_mask) begin
      ram_fflags_bits_uop_rxq_idx[ram_fflags_bits_uop_rxq_idx__T_53_addr] <= ram_fflags_bits_uop_rxq_idx__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_pdst__T_53_en & ram_fflags_bits_uop_pdst__T_53_mask) begin
      ram_fflags_bits_uop_pdst[ram_fflags_bits_uop_pdst__T_53_addr] <= ram_fflags_bits_uop_pdst__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_prs1__T_53_en & ram_fflags_bits_uop_prs1__T_53_mask) begin
      ram_fflags_bits_uop_prs1[ram_fflags_bits_uop_prs1__T_53_addr] <= ram_fflags_bits_uop_prs1__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_prs2__T_53_en & ram_fflags_bits_uop_prs2__T_53_mask) begin
      ram_fflags_bits_uop_prs2[ram_fflags_bits_uop_prs2__T_53_addr] <= ram_fflags_bits_uop_prs2__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_prs3__T_53_en & ram_fflags_bits_uop_prs3__T_53_mask) begin
      ram_fflags_bits_uop_prs3[ram_fflags_bits_uop_prs3__T_53_addr] <= ram_fflags_bits_uop_prs3__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ppred__T_53_en & ram_fflags_bits_uop_ppred__T_53_mask) begin
      ram_fflags_bits_uop_ppred[ram_fflags_bits_uop_ppred__T_53_addr] <= ram_fflags_bits_uop_ppred__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_prs1_busy__T_53_en & ram_fflags_bits_uop_prs1_busy__T_53_mask) begin
      ram_fflags_bits_uop_prs1_busy[ram_fflags_bits_uop_prs1_busy__T_53_addr] <=
        ram_fflags_bits_uop_prs1_busy__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_prs2_busy__T_53_en & ram_fflags_bits_uop_prs2_busy__T_53_mask) begin
      ram_fflags_bits_uop_prs2_busy[ram_fflags_bits_uop_prs2_busy__T_53_addr] <=
        ram_fflags_bits_uop_prs2_busy__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_prs3_busy__T_53_en & ram_fflags_bits_uop_prs3_busy__T_53_mask) begin
      ram_fflags_bits_uop_prs3_busy[ram_fflags_bits_uop_prs3_busy__T_53_addr] <=
        ram_fflags_bits_uop_prs3_busy__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ppred_busy__T_53_en & ram_fflags_bits_uop_ppred_busy__T_53_mask) begin
      ram_fflags_bits_uop_ppred_busy[ram_fflags_bits_uop_ppred_busy__T_53_addr] <=
        ram_fflags_bits_uop_ppred_busy__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_stale_pdst__T_53_en & ram_fflags_bits_uop_stale_pdst__T_53_mask) begin
      ram_fflags_bits_uop_stale_pdst[ram_fflags_bits_uop_stale_pdst__T_53_addr] <=
        ram_fflags_bits_uop_stale_pdst__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_exception__T_53_en & ram_fflags_bits_uop_exception__T_53_mask) begin
      ram_fflags_bits_uop_exception[ram_fflags_bits_uop_exception__T_53_addr] <=
        ram_fflags_bits_uop_exception__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_exc_cause__T_53_en & ram_fflags_bits_uop_exc_cause__T_53_mask) begin
      ram_fflags_bits_uop_exc_cause[ram_fflags_bits_uop_exc_cause__T_53_addr] <=
        ram_fflags_bits_uop_exc_cause__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_bypassable__T_53_en & ram_fflags_bits_uop_bypassable__T_53_mask) begin
      ram_fflags_bits_uop_bypassable[ram_fflags_bits_uop_bypassable__T_53_addr] <=
        ram_fflags_bits_uop_bypassable__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_mem_cmd__T_53_en & ram_fflags_bits_uop_mem_cmd__T_53_mask) begin
      ram_fflags_bits_uop_mem_cmd[ram_fflags_bits_uop_mem_cmd__T_53_addr] <= ram_fflags_bits_uop_mem_cmd__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_mem_size__T_53_en & ram_fflags_bits_uop_mem_size__T_53_mask) begin
      ram_fflags_bits_uop_mem_size[ram_fflags_bits_uop_mem_size__T_53_addr] <= ram_fflags_bits_uop_mem_size__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_mem_signed__T_53_en & ram_fflags_bits_uop_mem_signed__T_53_mask) begin
      ram_fflags_bits_uop_mem_signed[ram_fflags_bits_uop_mem_signed__T_53_addr] <=
        ram_fflags_bits_uop_mem_signed__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_fence__T_53_en & ram_fflags_bits_uop_is_fence__T_53_mask) begin
      ram_fflags_bits_uop_is_fence[ram_fflags_bits_uop_is_fence__T_53_addr] <= ram_fflags_bits_uop_is_fence__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_fencei__T_53_en & ram_fflags_bits_uop_is_fencei__T_53_mask) begin
      ram_fflags_bits_uop_is_fencei[ram_fflags_bits_uop_is_fencei__T_53_addr] <=
        ram_fflags_bits_uop_is_fencei__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_amo__T_53_en & ram_fflags_bits_uop_is_amo__T_53_mask) begin
      ram_fflags_bits_uop_is_amo[ram_fflags_bits_uop_is_amo__T_53_addr] <= ram_fflags_bits_uop_is_amo__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_uses_ldq__T_53_en & ram_fflags_bits_uop_uses_ldq__T_53_mask) begin
      ram_fflags_bits_uop_uses_ldq[ram_fflags_bits_uop_uses_ldq__T_53_addr] <= ram_fflags_bits_uop_uses_ldq__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_uses_stq__T_53_en & ram_fflags_bits_uop_uses_stq__T_53_mask) begin
      ram_fflags_bits_uop_uses_stq[ram_fflags_bits_uop_uses_stq__T_53_addr] <= ram_fflags_bits_uop_uses_stq__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_sys_pc2epc__T_53_en & ram_fflags_bits_uop_is_sys_pc2epc__T_53_mask) begin
      ram_fflags_bits_uop_is_sys_pc2epc[ram_fflags_bits_uop_is_sys_pc2epc__T_53_addr] <=
        ram_fflags_bits_uop_is_sys_pc2epc__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_is_unique__T_53_en & ram_fflags_bits_uop_is_unique__T_53_mask) begin
      ram_fflags_bits_uop_is_unique[ram_fflags_bits_uop_is_unique__T_53_addr] <=
        ram_fflags_bits_uop_is_unique__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_flush_on_commit__T_53_en & ram_fflags_bits_uop_flush_on_commit__T_53_mask) begin
      ram_fflags_bits_uop_flush_on_commit[ram_fflags_bits_uop_flush_on_commit__T_53_addr] <=
        ram_fflags_bits_uop_flush_on_commit__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ldst_is_rs1__T_53_en & ram_fflags_bits_uop_ldst_is_rs1__T_53_mask) begin
      ram_fflags_bits_uop_ldst_is_rs1[ram_fflags_bits_uop_ldst_is_rs1__T_53_addr] <=
        ram_fflags_bits_uop_ldst_is_rs1__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ldst__T_53_en & ram_fflags_bits_uop_ldst__T_53_mask) begin
      ram_fflags_bits_uop_ldst[ram_fflags_bits_uop_ldst__T_53_addr] <= ram_fflags_bits_uop_ldst__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_lrs1__T_53_en & ram_fflags_bits_uop_lrs1__T_53_mask) begin
      ram_fflags_bits_uop_lrs1[ram_fflags_bits_uop_lrs1__T_53_addr] <= ram_fflags_bits_uop_lrs1__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_lrs2__T_53_en & ram_fflags_bits_uop_lrs2__T_53_mask) begin
      ram_fflags_bits_uop_lrs2[ram_fflags_bits_uop_lrs2__T_53_addr] <= ram_fflags_bits_uop_lrs2__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_lrs3__T_53_en & ram_fflags_bits_uop_lrs3__T_53_mask) begin
      ram_fflags_bits_uop_lrs3[ram_fflags_bits_uop_lrs3__T_53_addr] <= ram_fflags_bits_uop_lrs3__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_ldst_val__T_53_en & ram_fflags_bits_uop_ldst_val__T_53_mask) begin
      ram_fflags_bits_uop_ldst_val[ram_fflags_bits_uop_ldst_val__T_53_addr] <= ram_fflags_bits_uop_ldst_val__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_dst_rtype__T_53_en & ram_fflags_bits_uop_dst_rtype__T_53_mask) begin
      ram_fflags_bits_uop_dst_rtype[ram_fflags_bits_uop_dst_rtype__T_53_addr] <=
        ram_fflags_bits_uop_dst_rtype__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_lrs1_rtype__T_53_en & ram_fflags_bits_uop_lrs1_rtype__T_53_mask) begin
      ram_fflags_bits_uop_lrs1_rtype[ram_fflags_bits_uop_lrs1_rtype__T_53_addr] <=
        ram_fflags_bits_uop_lrs1_rtype__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_lrs2_rtype__T_53_en & ram_fflags_bits_uop_lrs2_rtype__T_53_mask) begin
      ram_fflags_bits_uop_lrs2_rtype[ram_fflags_bits_uop_lrs2_rtype__T_53_addr] <=
        ram_fflags_bits_uop_lrs2_rtype__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_frs3_en__T_53_en & ram_fflags_bits_uop_frs3_en__T_53_mask) begin
      ram_fflags_bits_uop_frs3_en[ram_fflags_bits_uop_frs3_en__T_53_addr] <= ram_fflags_bits_uop_frs3_en__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_fp_val__T_53_en & ram_fflags_bits_uop_fp_val__T_53_mask) begin
      ram_fflags_bits_uop_fp_val[ram_fflags_bits_uop_fp_val__T_53_addr] <= ram_fflags_bits_uop_fp_val__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_fp_single__T_53_en & ram_fflags_bits_uop_fp_single__T_53_mask) begin
      ram_fflags_bits_uop_fp_single[ram_fflags_bits_uop_fp_single__T_53_addr] <=
        ram_fflags_bits_uop_fp_single__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_xcpt_pf_if__T_53_en & ram_fflags_bits_uop_xcpt_pf_if__T_53_mask) begin
      ram_fflags_bits_uop_xcpt_pf_if[ram_fflags_bits_uop_xcpt_pf_if__T_53_addr] <=
        ram_fflags_bits_uop_xcpt_pf_if__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_xcpt_ae_if__T_53_en & ram_fflags_bits_uop_xcpt_ae_if__T_53_mask) begin
      ram_fflags_bits_uop_xcpt_ae_if[ram_fflags_bits_uop_xcpt_ae_if__T_53_addr] <=
        ram_fflags_bits_uop_xcpt_ae_if__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_xcpt_ma_if__T_53_en & ram_fflags_bits_uop_xcpt_ma_if__T_53_mask) begin
      ram_fflags_bits_uop_xcpt_ma_if[ram_fflags_bits_uop_xcpt_ma_if__T_53_addr] <=
        ram_fflags_bits_uop_xcpt_ma_if__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_bp_debug_if__T_53_en & ram_fflags_bits_uop_bp_debug_if__T_53_mask) begin
      ram_fflags_bits_uop_bp_debug_if[ram_fflags_bits_uop_bp_debug_if__T_53_addr] <=
        ram_fflags_bits_uop_bp_debug_if__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_bp_xcpt_if__T_53_en & ram_fflags_bits_uop_bp_xcpt_if__T_53_mask) begin
      ram_fflags_bits_uop_bp_xcpt_if[ram_fflags_bits_uop_bp_xcpt_if__T_53_addr] <=
        ram_fflags_bits_uop_bp_xcpt_if__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_debug_fsrc__T_53_en & ram_fflags_bits_uop_debug_fsrc__T_53_mask) begin
      ram_fflags_bits_uop_debug_fsrc[ram_fflags_bits_uop_debug_fsrc__T_53_addr] <=
        ram_fflags_bits_uop_debug_fsrc__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_uop_debug_tsrc__T_53_en & ram_fflags_bits_uop_debug_tsrc__T_53_mask) begin
      ram_fflags_bits_uop_debug_tsrc[ram_fflags_bits_uop_debug_tsrc__T_53_addr] <=
        ram_fflags_bits_uop_debug_tsrc__T_53_data; // @[util.scala 464:20]
    end
    if (ram_fflags_bits_flags__T_53_en & ram_fflags_bits_flags__T_53_mask) begin
      ram_fflags_bits_flags[ram_fflags_bits_flags__T_53_addr] <= ram_fflags_bits_flags__T_53_data; // @[util.scala 464:20]
    end
    if (reset) begin // @[util.scala 465:24]
      valids_0 <= 1'h0; // @[util.scala 465:24]
    end else if (do_deq) begin // @[util.scala 495:17]
      if (3'h0 == value_1) begin // @[util.scala 496:27]
        valids_0 <= 1'h0; // @[util.scala 496:27]
      end else begin
        valids_0 <= _GEN_582;
      end
    end else begin
      valids_0 <= _GEN_582;
    end
    if (reset) begin // @[util.scala 465:24]
      valids_1 <= 1'h0; // @[util.scala 465:24]
    end else if (do_deq) begin // @[util.scala 495:17]
      if (3'h1 == value_1) begin // @[util.scala 496:27]
        valids_1 <= 1'h0; // @[util.scala 496:27]
      end else begin
        valids_1 <= _GEN_583;
      end
    end else begin
      valids_1 <= _GEN_583;
    end
    if (reset) begin // @[util.scala 465:24]
      valids_2 <= 1'h0; // @[util.scala 465:24]
    end else if (do_deq) begin // @[util.scala 495:17]
      if (3'h2 == value_1) begin // @[util.scala 496:27]
        valids_2 <= 1'h0; // @[util.scala 496:27]
      end else begin
        valids_2 <= _GEN_584;
      end
    end else begin
      valids_2 <= _GEN_584;
    end
    if (reset) begin // @[util.scala 465:24]
      valids_3 <= 1'h0; // @[util.scala 465:24]
    end else if (do_deq) begin // @[util.scala 495:17]
      if (3'h3 == value_1) begin // @[util.scala 496:27]
        valids_3 <= 1'h0; // @[util.scala 496:27]
      end else begin
        valids_3 <= _GEN_585;
      end
    end else begin
      valids_3 <= _GEN_585;
    end
    if (reset) begin // @[util.scala 465:24]
      valids_4 <= 1'h0; // @[util.scala 465:24]
    end else if (do_deq) begin // @[util.scala 495:17]
      if (3'h4 == value_1) begin // @[util.scala 496:27]
        valids_4 <= 1'h0; // @[util.scala 496:27]
      end else begin
        valids_4 <= _GEN_586;
      end
    end else begin
      valids_4 <= _GEN_586;
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_uopc <= io_enq_bits_uop_uopc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_inst <= io_enq_bits_uop_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_debug_inst <= io_enq_bits_uop_debug_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_rvc <= io_enq_bits_uop_is_rvc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_debug_pc <= io_enq_bits_uop_debug_pc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_iq_type <= io_enq_bits_uop_iq_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_fu_code <= io_enq_bits_uop_fu_code; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_br_type <= io_enq_bits_uop_ctrl_br_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_op1_sel <= io_enq_bits_uop_ctrl_op1_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_op2_sel <= io_enq_bits_uop_ctrl_op2_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_imm_sel <= io_enq_bits_uop_ctrl_imm_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_op_fcn <= io_enq_bits_uop_ctrl_op_fcn; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_fcn_dw <= io_enq_bits_uop_ctrl_fcn_dw; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_csr_cmd <= io_enq_bits_uop_ctrl_csr_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_is_load <= io_enq_bits_uop_ctrl_is_load; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_is_sta <= io_enq_bits_uop_ctrl_is_sta; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ctrl_is_std <= io_enq_bits_uop_ctrl_is_std; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_iw_state <= io_enq_bits_uop_iw_state; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_iw_p1_poisoned <= io_enq_bits_uop_iw_p1_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_iw_p2_poisoned <= io_enq_bits_uop_iw_p2_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_br <= io_enq_bits_uop_is_br; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_jalr <= io_enq_bits_uop_is_jalr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_jal <= io_enq_bits_uop_is_jal; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_sfb <= io_enq_bits_uop_is_sfb; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 491:33]
        uops_0_br_mask <= _T_55; // @[util.scala 491:33]
      end else if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_br_mask <= io_enq_bits_uop_br_mask; // @[util.scala 490:33]
      end else begin
        uops_0_br_mask <= _GEN_5;
      end
    end else begin
      uops_0_br_mask <= _GEN_5;
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_br_tag <= io_enq_bits_uop_br_tag; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ftq_idx <= io_enq_bits_uop_ftq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_edge_inst <= io_enq_bits_uop_edge_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_pc_lob <= io_enq_bits_uop_pc_lob; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_taken <= io_enq_bits_uop_taken; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_imm_packed <= io_enq_bits_uop_imm_packed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_csr_addr <= io_enq_bits_uop_csr_addr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_rob_idx <= io_enq_bits_uop_rob_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ldq_idx <= io_enq_bits_uop_ldq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_stq_idx <= io_enq_bits_uop_stq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_rxq_idx <= io_enq_bits_uop_rxq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_pdst <= io_enq_bits_uop_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_prs1 <= io_enq_bits_uop_prs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_prs2 <= io_enq_bits_uop_prs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_prs3 <= io_enq_bits_uop_prs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ppred <= io_enq_bits_uop_ppred; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_prs1_busy <= io_enq_bits_uop_prs1_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_prs2_busy <= io_enq_bits_uop_prs2_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_prs3_busy <= io_enq_bits_uop_prs3_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ppred_busy <= io_enq_bits_uop_ppred_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_stale_pdst <= io_enq_bits_uop_stale_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_exception <= io_enq_bits_uop_exception; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_exc_cause <= io_enq_bits_uop_exc_cause; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_bypassable <= io_enq_bits_uop_bypassable; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_mem_cmd <= io_enq_bits_uop_mem_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_mem_size <= io_enq_bits_uop_mem_size; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_mem_signed <= io_enq_bits_uop_mem_signed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_fence <= io_enq_bits_uop_is_fence; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_fencei <= io_enq_bits_uop_is_fencei; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_amo <= io_enq_bits_uop_is_amo; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_uses_ldq <= io_enq_bits_uop_uses_ldq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_uses_stq <= io_enq_bits_uop_uses_stq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_sys_pc2epc <= io_enq_bits_uop_is_sys_pc2epc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_is_unique <= io_enq_bits_uop_is_unique; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_flush_on_commit <= io_enq_bits_uop_flush_on_commit; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ldst_is_rs1 <= io_enq_bits_uop_ldst_is_rs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ldst <= io_enq_bits_uop_ldst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_lrs1 <= io_enq_bits_uop_lrs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_lrs2 <= io_enq_bits_uop_lrs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_lrs3 <= io_enq_bits_uop_lrs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_ldst_val <= io_enq_bits_uop_ldst_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_dst_rtype <= io_enq_bits_uop_dst_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_lrs1_rtype <= io_enq_bits_uop_lrs1_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_lrs2_rtype <= io_enq_bits_uop_lrs2_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_frs3_en <= io_enq_bits_uop_frs3_en; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_fp_val <= io_enq_bits_uop_fp_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_fp_single <= io_enq_bits_uop_fp_single; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_xcpt_pf_if <= io_enq_bits_uop_xcpt_pf_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_xcpt_ae_if <= io_enq_bits_uop_xcpt_ae_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_xcpt_ma_if <= io_enq_bits_uop_xcpt_ma_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_bp_debug_if <= io_enq_bits_uop_bp_debug_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_bp_xcpt_if <= io_enq_bits_uop_bp_xcpt_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_debug_fsrc <= io_enq_bits_uop_debug_fsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h0 == value) begin // @[util.scala 490:33]
        uops_0_debug_tsrc <= io_enq_bits_uop_debug_tsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_uopc <= io_enq_bits_uop_uopc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_inst <= io_enq_bits_uop_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_debug_inst <= io_enq_bits_uop_debug_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_rvc <= io_enq_bits_uop_is_rvc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_debug_pc <= io_enq_bits_uop_debug_pc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_iq_type <= io_enq_bits_uop_iq_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_fu_code <= io_enq_bits_uop_fu_code; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_br_type <= io_enq_bits_uop_ctrl_br_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_op1_sel <= io_enq_bits_uop_ctrl_op1_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_op2_sel <= io_enq_bits_uop_ctrl_op2_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_imm_sel <= io_enq_bits_uop_ctrl_imm_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_op_fcn <= io_enq_bits_uop_ctrl_op_fcn; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_fcn_dw <= io_enq_bits_uop_ctrl_fcn_dw; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_csr_cmd <= io_enq_bits_uop_ctrl_csr_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_is_load <= io_enq_bits_uop_ctrl_is_load; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_is_sta <= io_enq_bits_uop_ctrl_is_sta; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ctrl_is_std <= io_enq_bits_uop_ctrl_is_std; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_iw_state <= io_enq_bits_uop_iw_state; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_iw_p1_poisoned <= io_enq_bits_uop_iw_p1_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_iw_p2_poisoned <= io_enq_bits_uop_iw_p2_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_br <= io_enq_bits_uop_is_br; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_jalr <= io_enq_bits_uop_is_jalr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_jal <= io_enq_bits_uop_is_jal; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_sfb <= io_enq_bits_uop_is_sfb; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 491:33]
        uops_1_br_mask <= _T_55; // @[util.scala 491:33]
      end else if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_br_mask <= io_enq_bits_uop_br_mask; // @[util.scala 490:33]
      end else begin
        uops_1_br_mask <= _GEN_6;
      end
    end else begin
      uops_1_br_mask <= _GEN_6;
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_br_tag <= io_enq_bits_uop_br_tag; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ftq_idx <= io_enq_bits_uop_ftq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_edge_inst <= io_enq_bits_uop_edge_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_pc_lob <= io_enq_bits_uop_pc_lob; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_taken <= io_enq_bits_uop_taken; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_imm_packed <= io_enq_bits_uop_imm_packed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_csr_addr <= io_enq_bits_uop_csr_addr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_rob_idx <= io_enq_bits_uop_rob_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ldq_idx <= io_enq_bits_uop_ldq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_stq_idx <= io_enq_bits_uop_stq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_rxq_idx <= io_enq_bits_uop_rxq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_pdst <= io_enq_bits_uop_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_prs1 <= io_enq_bits_uop_prs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_prs2 <= io_enq_bits_uop_prs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_prs3 <= io_enq_bits_uop_prs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ppred <= io_enq_bits_uop_ppred; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_prs1_busy <= io_enq_bits_uop_prs1_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_prs2_busy <= io_enq_bits_uop_prs2_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_prs3_busy <= io_enq_bits_uop_prs3_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ppred_busy <= io_enq_bits_uop_ppred_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_stale_pdst <= io_enq_bits_uop_stale_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_exception <= io_enq_bits_uop_exception; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_exc_cause <= io_enq_bits_uop_exc_cause; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_bypassable <= io_enq_bits_uop_bypassable; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_mem_cmd <= io_enq_bits_uop_mem_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_mem_size <= io_enq_bits_uop_mem_size; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_mem_signed <= io_enq_bits_uop_mem_signed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_fence <= io_enq_bits_uop_is_fence; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_fencei <= io_enq_bits_uop_is_fencei; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_amo <= io_enq_bits_uop_is_amo; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_uses_ldq <= io_enq_bits_uop_uses_ldq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_uses_stq <= io_enq_bits_uop_uses_stq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_sys_pc2epc <= io_enq_bits_uop_is_sys_pc2epc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_is_unique <= io_enq_bits_uop_is_unique; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_flush_on_commit <= io_enq_bits_uop_flush_on_commit; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ldst_is_rs1 <= io_enq_bits_uop_ldst_is_rs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ldst <= io_enq_bits_uop_ldst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_lrs1 <= io_enq_bits_uop_lrs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_lrs2 <= io_enq_bits_uop_lrs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_lrs3 <= io_enq_bits_uop_lrs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_ldst_val <= io_enq_bits_uop_ldst_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_dst_rtype <= io_enq_bits_uop_dst_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_lrs1_rtype <= io_enq_bits_uop_lrs1_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_lrs2_rtype <= io_enq_bits_uop_lrs2_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_frs3_en <= io_enq_bits_uop_frs3_en; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_fp_val <= io_enq_bits_uop_fp_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_fp_single <= io_enq_bits_uop_fp_single; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_xcpt_pf_if <= io_enq_bits_uop_xcpt_pf_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_xcpt_ae_if <= io_enq_bits_uop_xcpt_ae_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_xcpt_ma_if <= io_enq_bits_uop_xcpt_ma_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_bp_debug_if <= io_enq_bits_uop_bp_debug_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_bp_xcpt_if <= io_enq_bits_uop_bp_xcpt_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_debug_fsrc <= io_enq_bits_uop_debug_fsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h1 == value) begin // @[util.scala 490:33]
        uops_1_debug_tsrc <= io_enq_bits_uop_debug_tsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_uopc <= io_enq_bits_uop_uopc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_inst <= io_enq_bits_uop_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_debug_inst <= io_enq_bits_uop_debug_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_rvc <= io_enq_bits_uop_is_rvc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_debug_pc <= io_enq_bits_uop_debug_pc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_iq_type <= io_enq_bits_uop_iq_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_fu_code <= io_enq_bits_uop_fu_code; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_br_type <= io_enq_bits_uop_ctrl_br_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_op1_sel <= io_enq_bits_uop_ctrl_op1_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_op2_sel <= io_enq_bits_uop_ctrl_op2_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_imm_sel <= io_enq_bits_uop_ctrl_imm_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_op_fcn <= io_enq_bits_uop_ctrl_op_fcn; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_fcn_dw <= io_enq_bits_uop_ctrl_fcn_dw; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_csr_cmd <= io_enq_bits_uop_ctrl_csr_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_is_load <= io_enq_bits_uop_ctrl_is_load; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_is_sta <= io_enq_bits_uop_ctrl_is_sta; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ctrl_is_std <= io_enq_bits_uop_ctrl_is_std; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_iw_state <= io_enq_bits_uop_iw_state; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_iw_p1_poisoned <= io_enq_bits_uop_iw_p1_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_iw_p2_poisoned <= io_enq_bits_uop_iw_p2_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_br <= io_enq_bits_uop_is_br; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_jalr <= io_enq_bits_uop_is_jalr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_jal <= io_enq_bits_uop_is_jal; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_sfb <= io_enq_bits_uop_is_sfb; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 491:33]
        uops_2_br_mask <= _T_55; // @[util.scala 491:33]
      end else if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_br_mask <= io_enq_bits_uop_br_mask; // @[util.scala 490:33]
      end else begin
        uops_2_br_mask <= _GEN_7;
      end
    end else begin
      uops_2_br_mask <= _GEN_7;
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_br_tag <= io_enq_bits_uop_br_tag; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ftq_idx <= io_enq_bits_uop_ftq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_edge_inst <= io_enq_bits_uop_edge_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_pc_lob <= io_enq_bits_uop_pc_lob; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_taken <= io_enq_bits_uop_taken; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_imm_packed <= io_enq_bits_uop_imm_packed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_csr_addr <= io_enq_bits_uop_csr_addr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_rob_idx <= io_enq_bits_uop_rob_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ldq_idx <= io_enq_bits_uop_ldq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_stq_idx <= io_enq_bits_uop_stq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_rxq_idx <= io_enq_bits_uop_rxq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_pdst <= io_enq_bits_uop_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_prs1 <= io_enq_bits_uop_prs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_prs2 <= io_enq_bits_uop_prs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_prs3 <= io_enq_bits_uop_prs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ppred <= io_enq_bits_uop_ppred; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_prs1_busy <= io_enq_bits_uop_prs1_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_prs2_busy <= io_enq_bits_uop_prs2_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_prs3_busy <= io_enq_bits_uop_prs3_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ppred_busy <= io_enq_bits_uop_ppred_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_stale_pdst <= io_enq_bits_uop_stale_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_exception <= io_enq_bits_uop_exception; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_exc_cause <= io_enq_bits_uop_exc_cause; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_bypassable <= io_enq_bits_uop_bypassable; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_mem_cmd <= io_enq_bits_uop_mem_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_mem_size <= io_enq_bits_uop_mem_size; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_mem_signed <= io_enq_bits_uop_mem_signed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_fence <= io_enq_bits_uop_is_fence; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_fencei <= io_enq_bits_uop_is_fencei; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_amo <= io_enq_bits_uop_is_amo; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_uses_ldq <= io_enq_bits_uop_uses_ldq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_uses_stq <= io_enq_bits_uop_uses_stq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_sys_pc2epc <= io_enq_bits_uop_is_sys_pc2epc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_is_unique <= io_enq_bits_uop_is_unique; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_flush_on_commit <= io_enq_bits_uop_flush_on_commit; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ldst_is_rs1 <= io_enq_bits_uop_ldst_is_rs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ldst <= io_enq_bits_uop_ldst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_lrs1 <= io_enq_bits_uop_lrs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_lrs2 <= io_enq_bits_uop_lrs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_lrs3 <= io_enq_bits_uop_lrs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_ldst_val <= io_enq_bits_uop_ldst_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_dst_rtype <= io_enq_bits_uop_dst_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_lrs1_rtype <= io_enq_bits_uop_lrs1_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_lrs2_rtype <= io_enq_bits_uop_lrs2_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_frs3_en <= io_enq_bits_uop_frs3_en; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_fp_val <= io_enq_bits_uop_fp_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_fp_single <= io_enq_bits_uop_fp_single; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_xcpt_pf_if <= io_enq_bits_uop_xcpt_pf_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_xcpt_ae_if <= io_enq_bits_uop_xcpt_ae_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_xcpt_ma_if <= io_enq_bits_uop_xcpt_ma_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_bp_debug_if <= io_enq_bits_uop_bp_debug_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_bp_xcpt_if <= io_enq_bits_uop_bp_xcpt_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_debug_fsrc <= io_enq_bits_uop_debug_fsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h2 == value) begin // @[util.scala 490:33]
        uops_2_debug_tsrc <= io_enq_bits_uop_debug_tsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_uopc <= io_enq_bits_uop_uopc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_inst <= io_enq_bits_uop_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_debug_inst <= io_enq_bits_uop_debug_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_rvc <= io_enq_bits_uop_is_rvc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_debug_pc <= io_enq_bits_uop_debug_pc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_iq_type <= io_enq_bits_uop_iq_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_fu_code <= io_enq_bits_uop_fu_code; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_br_type <= io_enq_bits_uop_ctrl_br_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_op1_sel <= io_enq_bits_uop_ctrl_op1_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_op2_sel <= io_enq_bits_uop_ctrl_op2_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_imm_sel <= io_enq_bits_uop_ctrl_imm_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_op_fcn <= io_enq_bits_uop_ctrl_op_fcn; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_fcn_dw <= io_enq_bits_uop_ctrl_fcn_dw; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_csr_cmd <= io_enq_bits_uop_ctrl_csr_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_is_load <= io_enq_bits_uop_ctrl_is_load; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_is_sta <= io_enq_bits_uop_ctrl_is_sta; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ctrl_is_std <= io_enq_bits_uop_ctrl_is_std; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_iw_state <= io_enq_bits_uop_iw_state; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_iw_p1_poisoned <= io_enq_bits_uop_iw_p1_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_iw_p2_poisoned <= io_enq_bits_uop_iw_p2_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_br <= io_enq_bits_uop_is_br; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_jalr <= io_enq_bits_uop_is_jalr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_jal <= io_enq_bits_uop_is_jal; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_sfb <= io_enq_bits_uop_is_sfb; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 491:33]
        uops_3_br_mask <= _T_55; // @[util.scala 491:33]
      end else if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_br_mask <= io_enq_bits_uop_br_mask; // @[util.scala 490:33]
      end else begin
        uops_3_br_mask <= _GEN_8;
      end
    end else begin
      uops_3_br_mask <= _GEN_8;
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_br_tag <= io_enq_bits_uop_br_tag; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ftq_idx <= io_enq_bits_uop_ftq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_edge_inst <= io_enq_bits_uop_edge_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_pc_lob <= io_enq_bits_uop_pc_lob; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_taken <= io_enq_bits_uop_taken; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_imm_packed <= io_enq_bits_uop_imm_packed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_csr_addr <= io_enq_bits_uop_csr_addr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_rob_idx <= io_enq_bits_uop_rob_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ldq_idx <= io_enq_bits_uop_ldq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_stq_idx <= io_enq_bits_uop_stq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_rxq_idx <= io_enq_bits_uop_rxq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_pdst <= io_enq_bits_uop_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_prs1 <= io_enq_bits_uop_prs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_prs2 <= io_enq_bits_uop_prs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_prs3 <= io_enq_bits_uop_prs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ppred <= io_enq_bits_uop_ppred; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_prs1_busy <= io_enq_bits_uop_prs1_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_prs2_busy <= io_enq_bits_uop_prs2_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_prs3_busy <= io_enq_bits_uop_prs3_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ppred_busy <= io_enq_bits_uop_ppred_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_stale_pdst <= io_enq_bits_uop_stale_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_exception <= io_enq_bits_uop_exception; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_exc_cause <= io_enq_bits_uop_exc_cause; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_bypassable <= io_enq_bits_uop_bypassable; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_mem_cmd <= io_enq_bits_uop_mem_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_mem_size <= io_enq_bits_uop_mem_size; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_mem_signed <= io_enq_bits_uop_mem_signed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_fence <= io_enq_bits_uop_is_fence; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_fencei <= io_enq_bits_uop_is_fencei; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_amo <= io_enq_bits_uop_is_amo; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_uses_ldq <= io_enq_bits_uop_uses_ldq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_uses_stq <= io_enq_bits_uop_uses_stq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_sys_pc2epc <= io_enq_bits_uop_is_sys_pc2epc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_is_unique <= io_enq_bits_uop_is_unique; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_flush_on_commit <= io_enq_bits_uop_flush_on_commit; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ldst_is_rs1 <= io_enq_bits_uop_ldst_is_rs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ldst <= io_enq_bits_uop_ldst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_lrs1 <= io_enq_bits_uop_lrs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_lrs2 <= io_enq_bits_uop_lrs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_lrs3 <= io_enq_bits_uop_lrs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_ldst_val <= io_enq_bits_uop_ldst_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_dst_rtype <= io_enq_bits_uop_dst_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_lrs1_rtype <= io_enq_bits_uop_lrs1_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_lrs2_rtype <= io_enq_bits_uop_lrs2_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_frs3_en <= io_enq_bits_uop_frs3_en; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_fp_val <= io_enq_bits_uop_fp_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_fp_single <= io_enq_bits_uop_fp_single; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_xcpt_pf_if <= io_enq_bits_uop_xcpt_pf_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_xcpt_ae_if <= io_enq_bits_uop_xcpt_ae_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_xcpt_ma_if <= io_enq_bits_uop_xcpt_ma_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_bp_debug_if <= io_enq_bits_uop_bp_debug_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_bp_xcpt_if <= io_enq_bits_uop_bp_xcpt_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_debug_fsrc <= io_enq_bits_uop_debug_fsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h3 == value) begin // @[util.scala 490:33]
        uops_3_debug_tsrc <= io_enq_bits_uop_debug_tsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_uopc <= io_enq_bits_uop_uopc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_inst <= io_enq_bits_uop_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_debug_inst <= io_enq_bits_uop_debug_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_rvc <= io_enq_bits_uop_is_rvc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_debug_pc <= io_enq_bits_uop_debug_pc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_iq_type <= io_enq_bits_uop_iq_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_fu_code <= io_enq_bits_uop_fu_code; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_br_type <= io_enq_bits_uop_ctrl_br_type; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_op1_sel <= io_enq_bits_uop_ctrl_op1_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_op2_sel <= io_enq_bits_uop_ctrl_op2_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_imm_sel <= io_enq_bits_uop_ctrl_imm_sel; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_op_fcn <= io_enq_bits_uop_ctrl_op_fcn; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_fcn_dw <= io_enq_bits_uop_ctrl_fcn_dw; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_csr_cmd <= io_enq_bits_uop_ctrl_csr_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_is_load <= io_enq_bits_uop_ctrl_is_load; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_is_sta <= io_enq_bits_uop_ctrl_is_sta; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ctrl_is_std <= io_enq_bits_uop_ctrl_is_std; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_iw_state <= io_enq_bits_uop_iw_state; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_iw_p1_poisoned <= io_enq_bits_uop_iw_p1_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_iw_p2_poisoned <= io_enq_bits_uop_iw_p2_poisoned; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_br <= io_enq_bits_uop_is_br; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_jalr <= io_enq_bits_uop_is_jalr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_jal <= io_enq_bits_uop_is_jal; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_sfb <= io_enq_bits_uop_is_sfb; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 491:33]
        uops_4_br_mask <= _T_55; // @[util.scala 491:33]
      end else if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_br_mask <= io_enq_bits_uop_br_mask; // @[util.scala 490:33]
      end else begin
        uops_4_br_mask <= _GEN_9;
      end
    end else begin
      uops_4_br_mask <= _GEN_9;
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_br_tag <= io_enq_bits_uop_br_tag; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ftq_idx <= io_enq_bits_uop_ftq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_edge_inst <= io_enq_bits_uop_edge_inst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_pc_lob <= io_enq_bits_uop_pc_lob; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_taken <= io_enq_bits_uop_taken; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_imm_packed <= io_enq_bits_uop_imm_packed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_csr_addr <= io_enq_bits_uop_csr_addr; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_rob_idx <= io_enq_bits_uop_rob_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ldq_idx <= io_enq_bits_uop_ldq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_stq_idx <= io_enq_bits_uop_stq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_rxq_idx <= io_enq_bits_uop_rxq_idx; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_pdst <= io_enq_bits_uop_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_prs1 <= io_enq_bits_uop_prs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_prs2 <= io_enq_bits_uop_prs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_prs3 <= io_enq_bits_uop_prs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ppred <= io_enq_bits_uop_ppred; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_prs1_busy <= io_enq_bits_uop_prs1_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_prs2_busy <= io_enq_bits_uop_prs2_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_prs3_busy <= io_enq_bits_uop_prs3_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ppred_busy <= io_enq_bits_uop_ppred_busy; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_stale_pdst <= io_enq_bits_uop_stale_pdst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_exception <= io_enq_bits_uop_exception; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_exc_cause <= io_enq_bits_uop_exc_cause; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_bypassable <= io_enq_bits_uop_bypassable; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_mem_cmd <= io_enq_bits_uop_mem_cmd; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_mem_size <= io_enq_bits_uop_mem_size; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_mem_signed <= io_enq_bits_uop_mem_signed; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_fence <= io_enq_bits_uop_is_fence; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_fencei <= io_enq_bits_uop_is_fencei; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_amo <= io_enq_bits_uop_is_amo; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_uses_ldq <= io_enq_bits_uop_uses_ldq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_uses_stq <= io_enq_bits_uop_uses_stq; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_sys_pc2epc <= io_enq_bits_uop_is_sys_pc2epc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_is_unique <= io_enq_bits_uop_is_unique; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_flush_on_commit <= io_enq_bits_uop_flush_on_commit; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ldst_is_rs1 <= io_enq_bits_uop_ldst_is_rs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ldst <= io_enq_bits_uop_ldst; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_lrs1 <= io_enq_bits_uop_lrs1; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_lrs2 <= io_enq_bits_uop_lrs2; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_lrs3 <= io_enq_bits_uop_lrs3; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_ldst_val <= io_enq_bits_uop_ldst_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_dst_rtype <= io_enq_bits_uop_dst_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_lrs1_rtype <= io_enq_bits_uop_lrs1_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_lrs2_rtype <= io_enq_bits_uop_lrs2_rtype; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_frs3_en <= io_enq_bits_uop_frs3_en; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_fp_val <= io_enq_bits_uop_fp_val; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_fp_single <= io_enq_bits_uop_fp_single; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_xcpt_pf_if <= io_enq_bits_uop_xcpt_pf_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_xcpt_ae_if <= io_enq_bits_uop_xcpt_ae_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_xcpt_ma_if <= io_enq_bits_uop_xcpt_ma_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_bp_debug_if <= io_enq_bits_uop_bp_debug_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_bp_xcpt_if <= io_enq_bits_uop_bp_xcpt_if; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_debug_fsrc <= io_enq_bits_uop_debug_fsrc; // @[util.scala 490:33]
      end
    end
    if (do_enq) begin // @[util.scala 487:17]
      if (3'h4 == value) begin // @[util.scala 490:33]
        uops_4_debug_tsrc <= io_enq_bits_uop_debug_tsrc; // @[util.scala 490:33]
      end
    end
    if (reset) begin // @[Counter.scala 29:33]
      value <= 3'h0; // @[Counter.scala 29:33]
    end else if (do_enq) begin // @[util.scala 487:17]
      if (_T_56) begin // @[Counter.scala 41:21]
        value <= 3'h0; // @[Counter.scala 41:29]
      end else begin
        value <= _T_58; // @[Counter.scala 39:13]
      end
    end
    if (reset) begin // @[Counter.scala 29:33]
      value_1 <= 3'h0; // @[Counter.scala 29:33]
    end else if (do_deq) begin // @[util.scala 495:17]
      if (_T_59) begin // @[Counter.scala 41:21]
        value_1 <= 3'h0; // @[Counter.scala 41:29]
      end else begin
        value_1 <= _T_61; // @[Counter.scala 39:13]
      end
    end
    if (reset) begin // @[util.scala 470:27]
      maybe_full <= 1'h0; // @[util.scala 470:27]
    end else if (do_enq != do_deq) begin // @[util.scala 500:28]
      if (io_empty) begin // @[util.scala 515:21]
        if (io_deq_ready) begin // @[util.scala 521:27]
          maybe_full <= 1'h0; // @[util.scala 521:36]
        end else begin
          maybe_full <= _T_3;
        end
      end else begin
        maybe_full <= _T_3;
      end
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_GARBAGE_ASSIGN
  _RAND_1 = {3{`RANDOM}};
  _RAND_3 = {1{`RANDOM}};
  _RAND_5 = {1{`RANDOM}};
  _RAND_7 = {1{`RANDOM}};
  _RAND_9 = {1{`RANDOM}};
  _RAND_11 = {1{`RANDOM}};
  _RAND_13 = {1{`RANDOM}};
  _RAND_15 = {2{`RANDOM}};
  _RAND_17 = {1{`RANDOM}};
  _RAND_19 = {1{`RANDOM}};
  _RAND_21 = {1{`RANDOM}};
  _RAND_23 = {1{`RANDOM}};
  _RAND_25 = {1{`RANDOM}};
  _RAND_27 = {1{`RANDOM}};
  _RAND_29 = {1{`RANDOM}};
  _RAND_31 = {1{`RANDOM}};
  _RAND_33 = {1{`RANDOM}};
  _RAND_35 = {1{`RANDOM}};
  _RAND_37 = {1{`RANDOM}};
  _RAND_39 = {1{`RANDOM}};
  _RAND_41 = {1{`RANDOM}};
  _RAND_43 = {1{`RANDOM}};
  _RAND_45 = {1{`RANDOM}};
  _RAND_47 = {1{`RANDOM}};
  _RAND_49 = {1{`RANDOM}};
  _RAND_51 = {1{`RANDOM}};
  _RAND_53 = {1{`RANDOM}};
  _RAND_55 = {1{`RANDOM}};
  _RAND_57 = {1{`RANDOM}};
  _RAND_59 = {1{`RANDOM}};
  _RAND_61 = {1{`RANDOM}};
  _RAND_63 = {1{`RANDOM}};
  _RAND_65 = {1{`RANDOM}};
  _RAND_67 = {1{`RANDOM}};
  _RAND_69 = {1{`RANDOM}};
  _RAND_71 = {1{`RANDOM}};
  _RAND_73 = {1{`RANDOM}};
  _RAND_75 = {1{`RANDOM}};
  _RAND_77 = {1{`RANDOM}};
  _RAND_79 = {1{`RANDOM}};
  _RAND_81 = {1{`RANDOM}};
  _RAND_83 = {1{`RANDOM}};
  _RAND_85 = {1{`RANDOM}};
  _RAND_87 = {1{`RANDOM}};
  _RAND_89 = {1{`RANDOM}};
  _RAND_91 = {1{`RANDOM}};
  _RAND_93 = {1{`RANDOM}};
  _RAND_95 = {1{`RANDOM}};
  _RAND_97 = {1{`RANDOM}};
  _RAND_99 = {1{`RANDOM}};
  _RAND_101 = {2{`RANDOM}};
  _RAND_103 = {1{`RANDOM}};
  _RAND_105 = {1{`RANDOM}};
  _RAND_107 = {1{`RANDOM}};
  _RAND_109 = {1{`RANDOM}};
  _RAND_111 = {1{`RANDOM}};
  _RAND_113 = {1{`RANDOM}};
  _RAND_115 = {1{`RANDOM}};
  _RAND_117 = {1{`RANDOM}};
  _RAND_119 = {1{`RANDOM}};
  _RAND_121 = {1{`RANDOM}};
  _RAND_123 = {1{`RANDOM}};
  _RAND_125 = {1{`RANDOM}};
  _RAND_127 = {1{`RANDOM}};
  _RAND_129 = {1{`RANDOM}};
  _RAND_131 = {1{`RANDOM}};
  _RAND_133 = {1{`RANDOM}};
  _RAND_135 = {1{`RANDOM}};
  _RAND_137 = {1{`RANDOM}};
  _RAND_139 = {1{`RANDOM}};
  _RAND_141 = {1{`RANDOM}};
  _RAND_143 = {1{`RANDOM}};
  _RAND_145 = {1{`RANDOM}};
  _RAND_147 = {1{`RANDOM}};
  _RAND_149 = {1{`RANDOM}};
  _RAND_151 = {1{`RANDOM}};
  _RAND_153 = {1{`RANDOM}};
  _RAND_155 = {1{`RANDOM}};
  _RAND_157 = {1{`RANDOM}};
  _RAND_159 = {1{`RANDOM}};
  _RAND_161 = {1{`RANDOM}};
  _RAND_163 = {1{`RANDOM}};
  _RAND_165 = {1{`RANDOM}};
`endif // RANDOMIZE_GARBAGE_ASSIGN
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {3{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_data[initvar] = _RAND_0[64:0];
  _RAND_2 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_predicated[initvar] = _RAND_2[0:0];
  _RAND_4 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_valid[initvar] = _RAND_4[0:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_uopc[initvar] = _RAND_6[6:0];
  _RAND_8 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_inst[initvar] = _RAND_8[31:0];
  _RAND_10 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_debug_inst[initvar] = _RAND_10[31:0];
  _RAND_12 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_rvc[initvar] = _RAND_12[0:0];
  _RAND_14 = {2{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_debug_pc[initvar] = _RAND_14[39:0];
  _RAND_16 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_iq_type[initvar] = _RAND_16[2:0];
  _RAND_18 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_fu_code[initvar] = _RAND_18[9:0];
  _RAND_20 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_br_type[initvar] = _RAND_20[3:0];
  _RAND_22 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_op1_sel[initvar] = _RAND_22[1:0];
  _RAND_24 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_op2_sel[initvar] = _RAND_24[2:0];
  _RAND_26 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_imm_sel[initvar] = _RAND_26[2:0];
  _RAND_28 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_op_fcn[initvar] = _RAND_28[3:0];
  _RAND_30 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_fcn_dw[initvar] = _RAND_30[0:0];
  _RAND_32 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_csr_cmd[initvar] = _RAND_32[2:0];
  _RAND_34 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_is_load[initvar] = _RAND_34[0:0];
  _RAND_36 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_is_sta[initvar] = _RAND_36[0:0];
  _RAND_38 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ctrl_is_std[initvar] = _RAND_38[0:0];
  _RAND_40 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_iw_state[initvar] = _RAND_40[1:0];
  _RAND_42 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_iw_p1_poisoned[initvar] = _RAND_42[0:0];
  _RAND_44 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_iw_p2_poisoned[initvar] = _RAND_44[0:0];
  _RAND_46 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_br[initvar] = _RAND_46[0:0];
  _RAND_48 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_jalr[initvar] = _RAND_48[0:0];
  _RAND_50 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_jal[initvar] = _RAND_50[0:0];
  _RAND_52 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_sfb[initvar] = _RAND_52[0:0];
  _RAND_54 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_br_mask[initvar] = _RAND_54[7:0];
  _RAND_56 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_br_tag[initvar] = _RAND_56[2:0];
  _RAND_58 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ftq_idx[initvar] = _RAND_58[3:0];
  _RAND_60 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_edge_inst[initvar] = _RAND_60[0:0];
  _RAND_62 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_pc_lob[initvar] = _RAND_62[5:0];
  _RAND_64 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_taken[initvar] = _RAND_64[0:0];
  _RAND_66 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_imm_packed[initvar] = _RAND_66[19:0];
  _RAND_68 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_csr_addr[initvar] = _RAND_68[11:0];
  _RAND_70 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_rob_idx[initvar] = _RAND_70[4:0];
  _RAND_72 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ldq_idx[initvar] = _RAND_72[2:0];
  _RAND_74 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_stq_idx[initvar] = _RAND_74[2:0];
  _RAND_76 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_rxq_idx[initvar] = _RAND_76[1:0];
  _RAND_78 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_pdst[initvar] = _RAND_78[5:0];
  _RAND_80 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_prs1[initvar] = _RAND_80[5:0];
  _RAND_82 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_prs2[initvar] = _RAND_82[5:0];
  _RAND_84 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_prs3[initvar] = _RAND_84[5:0];
  _RAND_86 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ppred[initvar] = _RAND_86[3:0];
  _RAND_88 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_prs1_busy[initvar] = _RAND_88[0:0];
  _RAND_90 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_prs2_busy[initvar] = _RAND_90[0:0];
  _RAND_92 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_prs3_busy[initvar] = _RAND_92[0:0];
  _RAND_94 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ppred_busy[initvar] = _RAND_94[0:0];
  _RAND_96 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_stale_pdst[initvar] = _RAND_96[5:0];
  _RAND_98 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_exception[initvar] = _RAND_98[0:0];
  _RAND_100 = {2{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_exc_cause[initvar] = _RAND_100[63:0];
  _RAND_102 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_bypassable[initvar] = _RAND_102[0:0];
  _RAND_104 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_mem_cmd[initvar] = _RAND_104[4:0];
  _RAND_106 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_mem_size[initvar] = _RAND_106[1:0];
  _RAND_108 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_mem_signed[initvar] = _RAND_108[0:0];
  _RAND_110 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_fence[initvar] = _RAND_110[0:0];
  _RAND_112 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_fencei[initvar] = _RAND_112[0:0];
  _RAND_114 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_amo[initvar] = _RAND_114[0:0];
  _RAND_116 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_uses_ldq[initvar] = _RAND_116[0:0];
  _RAND_118 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_uses_stq[initvar] = _RAND_118[0:0];
  _RAND_120 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_sys_pc2epc[initvar] = _RAND_120[0:0];
  _RAND_122 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_is_unique[initvar] = _RAND_122[0:0];
  _RAND_124 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_flush_on_commit[initvar] = _RAND_124[0:0];
  _RAND_126 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ldst_is_rs1[initvar] = _RAND_126[0:0];
  _RAND_128 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ldst[initvar] = _RAND_128[5:0];
  _RAND_130 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_lrs1[initvar] = _RAND_130[5:0];
  _RAND_132 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_lrs2[initvar] = _RAND_132[5:0];
  _RAND_134 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_lrs3[initvar] = _RAND_134[5:0];
  _RAND_136 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_ldst_val[initvar] = _RAND_136[0:0];
  _RAND_138 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_dst_rtype[initvar] = _RAND_138[1:0];
  _RAND_140 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_lrs1_rtype[initvar] = _RAND_140[1:0];
  _RAND_142 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_lrs2_rtype[initvar] = _RAND_142[1:0];
  _RAND_144 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_frs3_en[initvar] = _RAND_144[0:0];
  _RAND_146 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_fp_val[initvar] = _RAND_146[0:0];
  _RAND_148 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_fp_single[initvar] = _RAND_148[0:0];
  _RAND_150 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_xcpt_pf_if[initvar] = _RAND_150[0:0];
  _RAND_152 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_xcpt_ae_if[initvar] = _RAND_152[0:0];
  _RAND_154 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_xcpt_ma_if[initvar] = _RAND_154[0:0];
  _RAND_156 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_bp_debug_if[initvar] = _RAND_156[0:0];
  _RAND_158 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_bp_xcpt_if[initvar] = _RAND_158[0:0];
  _RAND_160 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_debug_fsrc[initvar] = _RAND_160[1:0];
  _RAND_162 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_uop_debug_tsrc[initvar] = _RAND_162[1:0];
  _RAND_164 = {1{`RANDOM}};
  for (initvar = 0; initvar < 5; initvar = initvar+1)
    ram_fflags_bits_flags[initvar] = _RAND_164[4:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_166 = {1{`RANDOM}};
  valids_0 = _RAND_166[0:0];
  _RAND_167 = {1{`RANDOM}};
  valids_1 = _RAND_167[0:0];
  _RAND_168 = {1{`RANDOM}};
  valids_2 = _RAND_168[0:0];
  _RAND_169 = {1{`RANDOM}};
  valids_3 = _RAND_169[0:0];
  _RAND_170 = {1{`RANDOM}};
  valids_4 = _RAND_170[0:0];
  _RAND_171 = {1{`RANDOM}};
  uops_0_uopc = _RAND_171[6:0];
  _RAND_172 = {1{`RANDOM}};
  uops_0_inst = _RAND_172[31:0];
  _RAND_173 = {1{`RANDOM}};
  uops_0_debug_inst = _RAND_173[31:0];
  _RAND_174 = {1{`RANDOM}};
  uops_0_is_rvc = _RAND_174[0:0];
  _RAND_175 = {2{`RANDOM}};
  uops_0_debug_pc = _RAND_175[39:0];
  _RAND_176 = {1{`RANDOM}};
  uops_0_iq_type = _RAND_176[2:0];
  _RAND_177 = {1{`RANDOM}};
  uops_0_fu_code = _RAND_177[9:0];
  _RAND_178 = {1{`RANDOM}};
  uops_0_ctrl_br_type = _RAND_178[3:0];
  _RAND_179 = {1{`RANDOM}};
  uops_0_ctrl_op1_sel = _RAND_179[1:0];
  _RAND_180 = {1{`RANDOM}};
  uops_0_ctrl_op2_sel = _RAND_180[2:0];
  _RAND_181 = {1{`RANDOM}};
  uops_0_ctrl_imm_sel = _RAND_181[2:0];
  _RAND_182 = {1{`RANDOM}};
  uops_0_ctrl_op_fcn = _RAND_182[3:0];
  _RAND_183 = {1{`RANDOM}};
  uops_0_ctrl_fcn_dw = _RAND_183[0:0];
  _RAND_184 = {1{`RANDOM}};
  uops_0_ctrl_csr_cmd = _RAND_184[2:0];
  _RAND_185 = {1{`RANDOM}};
  uops_0_ctrl_is_load = _RAND_185[0:0];
  _RAND_186 = {1{`RANDOM}};
  uops_0_ctrl_is_sta = _RAND_186[0:0];
  _RAND_187 = {1{`RANDOM}};
  uops_0_ctrl_is_std = _RAND_187[0:0];
  _RAND_188 = {1{`RANDOM}};
  uops_0_iw_state = _RAND_188[1:0];
  _RAND_189 = {1{`RANDOM}};
  uops_0_iw_p1_poisoned = _RAND_189[0:0];
  _RAND_190 = {1{`RANDOM}};
  uops_0_iw_p2_poisoned = _RAND_190[0:0];
  _RAND_191 = {1{`RANDOM}};
  uops_0_is_br = _RAND_191[0:0];
  _RAND_192 = {1{`RANDOM}};
  uops_0_is_jalr = _RAND_192[0:0];
  _RAND_193 = {1{`RANDOM}};
  uops_0_is_jal = _RAND_193[0:0];
  _RAND_194 = {1{`RANDOM}};
  uops_0_is_sfb = _RAND_194[0:0];
  _RAND_195 = {1{`RANDOM}};
  uops_0_br_mask = _RAND_195[7:0];
  _RAND_196 = {1{`RANDOM}};
  uops_0_br_tag = _RAND_196[2:0];
  _RAND_197 = {1{`RANDOM}};
  uops_0_ftq_idx = _RAND_197[3:0];
  _RAND_198 = {1{`RANDOM}};
  uops_0_edge_inst = _RAND_198[0:0];
  _RAND_199 = {1{`RANDOM}};
  uops_0_pc_lob = _RAND_199[5:0];
  _RAND_200 = {1{`RANDOM}};
  uops_0_taken = _RAND_200[0:0];
  _RAND_201 = {1{`RANDOM}};
  uops_0_imm_packed = _RAND_201[19:0];
  _RAND_202 = {1{`RANDOM}};
  uops_0_csr_addr = _RAND_202[11:0];
  _RAND_203 = {1{`RANDOM}};
  uops_0_rob_idx = _RAND_203[4:0];
  _RAND_204 = {1{`RANDOM}};
  uops_0_ldq_idx = _RAND_204[2:0];
  _RAND_205 = {1{`RANDOM}};
  uops_0_stq_idx = _RAND_205[2:0];
  _RAND_206 = {1{`RANDOM}};
  uops_0_rxq_idx = _RAND_206[1:0];
  _RAND_207 = {1{`RANDOM}};
  uops_0_pdst = _RAND_207[5:0];
  _RAND_208 = {1{`RANDOM}};
  uops_0_prs1 = _RAND_208[5:0];
  _RAND_209 = {1{`RANDOM}};
  uops_0_prs2 = _RAND_209[5:0];
  _RAND_210 = {1{`RANDOM}};
  uops_0_prs3 = _RAND_210[5:0];
  _RAND_211 = {1{`RANDOM}};
  uops_0_ppred = _RAND_211[3:0];
  _RAND_212 = {1{`RANDOM}};
  uops_0_prs1_busy = _RAND_212[0:0];
  _RAND_213 = {1{`RANDOM}};
  uops_0_prs2_busy = _RAND_213[0:0];
  _RAND_214 = {1{`RANDOM}};
  uops_0_prs3_busy = _RAND_214[0:0];
  _RAND_215 = {1{`RANDOM}};
  uops_0_ppred_busy = _RAND_215[0:0];
  _RAND_216 = {1{`RANDOM}};
  uops_0_stale_pdst = _RAND_216[5:0];
  _RAND_217 = {1{`RANDOM}};
  uops_0_exception = _RAND_217[0:0];
  _RAND_218 = {2{`RANDOM}};
  uops_0_exc_cause = _RAND_218[63:0];
  _RAND_219 = {1{`RANDOM}};
  uops_0_bypassable = _RAND_219[0:0];
  _RAND_220 = {1{`RANDOM}};
  uops_0_mem_cmd = _RAND_220[4:0];
  _RAND_221 = {1{`RANDOM}};
  uops_0_mem_size = _RAND_221[1:0];
  _RAND_222 = {1{`RANDOM}};
  uops_0_mem_signed = _RAND_222[0:0];
  _RAND_223 = {1{`RANDOM}};
  uops_0_is_fence = _RAND_223[0:0];
  _RAND_224 = {1{`RANDOM}};
  uops_0_is_fencei = _RAND_224[0:0];
  _RAND_225 = {1{`RANDOM}};
  uops_0_is_amo = _RAND_225[0:0];
  _RAND_226 = {1{`RANDOM}};
  uops_0_uses_ldq = _RAND_226[0:0];
  _RAND_227 = {1{`RANDOM}};
  uops_0_uses_stq = _RAND_227[0:0];
  _RAND_228 = {1{`RANDOM}};
  uops_0_is_sys_pc2epc = _RAND_228[0:0];
  _RAND_229 = {1{`RANDOM}};
  uops_0_is_unique = _RAND_229[0:0];
  _RAND_230 = {1{`RANDOM}};
  uops_0_flush_on_commit = _RAND_230[0:0];
  _RAND_231 = {1{`RANDOM}};
  uops_0_ldst_is_rs1 = _RAND_231[0:0];
  _RAND_232 = {1{`RANDOM}};
  uops_0_ldst = _RAND_232[5:0];
  _RAND_233 = {1{`RANDOM}};
  uops_0_lrs1 = _RAND_233[5:0];
  _RAND_234 = {1{`RANDOM}};
  uops_0_lrs2 = _RAND_234[5:0];
  _RAND_235 = {1{`RANDOM}};
  uops_0_lrs3 = _RAND_235[5:0];
  _RAND_236 = {1{`RANDOM}};
  uops_0_ldst_val = _RAND_236[0:0];
  _RAND_237 = {1{`RANDOM}};
  uops_0_dst_rtype = _RAND_237[1:0];
  _RAND_238 = {1{`RANDOM}};
  uops_0_lrs1_rtype = _RAND_238[1:0];
  _RAND_239 = {1{`RANDOM}};
  uops_0_lrs2_rtype = _RAND_239[1:0];
  _RAND_240 = {1{`RANDOM}};
  uops_0_frs3_en = _RAND_240[0:0];
  _RAND_241 = {1{`RANDOM}};
  uops_0_fp_val = _RAND_241[0:0];
  _RAND_242 = {1{`RANDOM}};
  uops_0_fp_single = _RAND_242[0:0];
  _RAND_243 = {1{`RANDOM}};
  uops_0_xcpt_pf_if = _RAND_243[0:0];
  _RAND_244 = {1{`RANDOM}};
  uops_0_xcpt_ae_if = _RAND_244[0:0];
  _RAND_245 = {1{`RANDOM}};
  uops_0_xcpt_ma_if = _RAND_245[0:0];
  _RAND_246 = {1{`RANDOM}};
  uops_0_bp_debug_if = _RAND_246[0:0];
  _RAND_247 = {1{`RANDOM}};
  uops_0_bp_xcpt_if = _RAND_247[0:0];
  _RAND_248 = {1{`RANDOM}};
  uops_0_debug_fsrc = _RAND_248[1:0];
  _RAND_249 = {1{`RANDOM}};
  uops_0_debug_tsrc = _RAND_249[1:0];
  _RAND_250 = {1{`RANDOM}};
  uops_1_uopc = _RAND_250[6:0];
  _RAND_251 = {1{`RANDOM}};
  uops_1_inst = _RAND_251[31:0];
  _RAND_252 = {1{`RANDOM}};
  uops_1_debug_inst = _RAND_252[31:0];
  _RAND_253 = {1{`RANDOM}};
  uops_1_is_rvc = _RAND_253[0:0];
  _RAND_254 = {2{`RANDOM}};
  uops_1_debug_pc = _RAND_254[39:0];
  _RAND_255 = {1{`RANDOM}};
  uops_1_iq_type = _RAND_255[2:0];
  _RAND_256 = {1{`RANDOM}};
  uops_1_fu_code = _RAND_256[9:0];
  _RAND_257 = {1{`RANDOM}};
  uops_1_ctrl_br_type = _RAND_257[3:0];
  _RAND_258 = {1{`RANDOM}};
  uops_1_ctrl_op1_sel = _RAND_258[1:0];
  _RAND_259 = {1{`RANDOM}};
  uops_1_ctrl_op2_sel = _RAND_259[2:0];
  _RAND_260 = {1{`RANDOM}};
  uops_1_ctrl_imm_sel = _RAND_260[2:0];
  _RAND_261 = {1{`RANDOM}};
  uops_1_ctrl_op_fcn = _RAND_261[3:0];
  _RAND_262 = {1{`RANDOM}};
  uops_1_ctrl_fcn_dw = _RAND_262[0:0];
  _RAND_263 = {1{`RANDOM}};
  uops_1_ctrl_csr_cmd = _RAND_263[2:0];
  _RAND_264 = {1{`RANDOM}};
  uops_1_ctrl_is_load = _RAND_264[0:0];
  _RAND_265 = {1{`RANDOM}};
  uops_1_ctrl_is_sta = _RAND_265[0:0];
  _RAND_266 = {1{`RANDOM}};
  uops_1_ctrl_is_std = _RAND_266[0:0];
  _RAND_267 = {1{`RANDOM}};
  uops_1_iw_state = _RAND_267[1:0];
  _RAND_268 = {1{`RANDOM}};
  uops_1_iw_p1_poisoned = _RAND_268[0:0];
  _RAND_269 = {1{`RANDOM}};
  uops_1_iw_p2_poisoned = _RAND_269[0:0];
  _RAND_270 = {1{`RANDOM}};
  uops_1_is_br = _RAND_270[0:0];
  _RAND_271 = {1{`RANDOM}};
  uops_1_is_jalr = _RAND_271[0:0];
  _RAND_272 = {1{`RANDOM}};
  uops_1_is_jal = _RAND_272[0:0];
  _RAND_273 = {1{`RANDOM}};
  uops_1_is_sfb = _RAND_273[0:0];
  _RAND_274 = {1{`RANDOM}};
  uops_1_br_mask = _RAND_274[7:0];
  _RAND_275 = {1{`RANDOM}};
  uops_1_br_tag = _RAND_275[2:0];
  _RAND_276 = {1{`RANDOM}};
  uops_1_ftq_idx = _RAND_276[3:0];
  _RAND_277 = {1{`RANDOM}};
  uops_1_edge_inst = _RAND_277[0:0];
  _RAND_278 = {1{`RANDOM}};
  uops_1_pc_lob = _RAND_278[5:0];
  _RAND_279 = {1{`RANDOM}};
  uops_1_taken = _RAND_279[0:0];
  _RAND_280 = {1{`RANDOM}};
  uops_1_imm_packed = _RAND_280[19:0];
  _RAND_281 = {1{`RANDOM}};
  uops_1_csr_addr = _RAND_281[11:0];
  _RAND_282 = {1{`RANDOM}};
  uops_1_rob_idx = _RAND_282[4:0];
  _RAND_283 = {1{`RANDOM}};
  uops_1_ldq_idx = _RAND_283[2:0];
  _RAND_284 = {1{`RANDOM}};
  uops_1_stq_idx = _RAND_284[2:0];
  _RAND_285 = {1{`RANDOM}};
  uops_1_rxq_idx = _RAND_285[1:0];
  _RAND_286 = {1{`RANDOM}};
  uops_1_pdst = _RAND_286[5:0];
  _RAND_287 = {1{`RANDOM}};
  uops_1_prs1 = _RAND_287[5:0];
  _RAND_288 = {1{`RANDOM}};
  uops_1_prs2 = _RAND_288[5:0];
  _RAND_289 = {1{`RANDOM}};
  uops_1_prs3 = _RAND_289[5:0];
  _RAND_290 = {1{`RANDOM}};
  uops_1_ppred = _RAND_290[3:0];
  _RAND_291 = {1{`RANDOM}};
  uops_1_prs1_busy = _RAND_291[0:0];
  _RAND_292 = {1{`RANDOM}};
  uops_1_prs2_busy = _RAND_292[0:0];
  _RAND_293 = {1{`RANDOM}};
  uops_1_prs3_busy = _RAND_293[0:0];
  _RAND_294 = {1{`RANDOM}};
  uops_1_ppred_busy = _RAND_294[0:0];
  _RAND_295 = {1{`RANDOM}};
  uops_1_stale_pdst = _RAND_295[5:0];
  _RAND_296 = {1{`RANDOM}};
  uops_1_exception = _RAND_296[0:0];
  _RAND_297 = {2{`RANDOM}};
  uops_1_exc_cause = _RAND_297[63:0];
  _RAND_298 = {1{`RANDOM}};
  uops_1_bypassable = _RAND_298[0:0];
  _RAND_299 = {1{`RANDOM}};
  uops_1_mem_cmd = _RAND_299[4:0];
  _RAND_300 = {1{`RANDOM}};
  uops_1_mem_size = _RAND_300[1:0];
  _RAND_301 = {1{`RANDOM}};
  uops_1_mem_signed = _RAND_301[0:0];
  _RAND_302 = {1{`RANDOM}};
  uops_1_is_fence = _RAND_302[0:0];
  _RAND_303 = {1{`RANDOM}};
  uops_1_is_fencei = _RAND_303[0:0];
  _RAND_304 = {1{`RANDOM}};
  uops_1_is_amo = _RAND_304[0:0];
  _RAND_305 = {1{`RANDOM}};
  uops_1_uses_ldq = _RAND_305[0:0];
  _RAND_306 = {1{`RANDOM}};
  uops_1_uses_stq = _RAND_306[0:0];
  _RAND_307 = {1{`RANDOM}};
  uops_1_is_sys_pc2epc = _RAND_307[0:0];
  _RAND_308 = {1{`RANDOM}};
  uops_1_is_unique = _RAND_308[0:0];
  _RAND_309 = {1{`RANDOM}};
  uops_1_flush_on_commit = _RAND_309[0:0];
  _RAND_310 = {1{`RANDOM}};
  uops_1_ldst_is_rs1 = _RAND_310[0:0];
  _RAND_311 = {1{`RANDOM}};
  uops_1_ldst = _RAND_311[5:0];
  _RAND_312 = {1{`RANDOM}};
  uops_1_lrs1 = _RAND_312[5:0];
  _RAND_313 = {1{`RANDOM}};
  uops_1_lrs2 = _RAND_313[5:0];
  _RAND_314 = {1{`RANDOM}};
  uops_1_lrs3 = _RAND_314[5:0];
  _RAND_315 = {1{`RANDOM}};
  uops_1_ldst_val = _RAND_315[0:0];
  _RAND_316 = {1{`RANDOM}};
  uops_1_dst_rtype = _RAND_316[1:0];
  _RAND_317 = {1{`RANDOM}};
  uops_1_lrs1_rtype = _RAND_317[1:0];
  _RAND_318 = {1{`RANDOM}};
  uops_1_lrs2_rtype = _RAND_318[1:0];
  _RAND_319 = {1{`RANDOM}};
  uops_1_frs3_en = _RAND_319[0:0];
  _RAND_320 = {1{`RANDOM}};
  uops_1_fp_val = _RAND_320[0:0];
  _RAND_321 = {1{`RANDOM}};
  uops_1_fp_single = _RAND_321[0:0];
  _RAND_322 = {1{`RANDOM}};
  uops_1_xcpt_pf_if = _RAND_322[0:0];
  _RAND_323 = {1{`RANDOM}};
  uops_1_xcpt_ae_if = _RAND_323[0:0];
  _RAND_324 = {1{`RANDOM}};
  uops_1_xcpt_ma_if = _RAND_324[0:0];
  _RAND_325 = {1{`RANDOM}};
  uops_1_bp_debug_if = _RAND_325[0:0];
  _RAND_326 = {1{`RANDOM}};
  uops_1_bp_xcpt_if = _RAND_326[0:0];
  _RAND_327 = {1{`RANDOM}};
  uops_1_debug_fsrc = _RAND_327[1:0];
  _RAND_328 = {1{`RANDOM}};
  uops_1_debug_tsrc = _RAND_328[1:0];
  _RAND_329 = {1{`RANDOM}};
  uops_2_uopc = _RAND_329[6:0];
  _RAND_330 = {1{`RANDOM}};
  uops_2_inst = _RAND_330[31:0];
  _RAND_331 = {1{`RANDOM}};
  uops_2_debug_inst = _RAND_331[31:0];
  _RAND_332 = {1{`RANDOM}};
  uops_2_is_rvc = _RAND_332[0:0];
  _RAND_333 = {2{`RANDOM}};
  uops_2_debug_pc = _RAND_333[39:0];
  _RAND_334 = {1{`RANDOM}};
  uops_2_iq_type = _RAND_334[2:0];
  _RAND_335 = {1{`RANDOM}};
  uops_2_fu_code = _RAND_335[9:0];
  _RAND_336 = {1{`RANDOM}};
  uops_2_ctrl_br_type = _RAND_336[3:0];
  _RAND_337 = {1{`RANDOM}};
  uops_2_ctrl_op1_sel = _RAND_337[1:0];
  _RAND_338 = {1{`RANDOM}};
  uops_2_ctrl_op2_sel = _RAND_338[2:0];
  _RAND_339 = {1{`RANDOM}};
  uops_2_ctrl_imm_sel = _RAND_339[2:0];
  _RAND_340 = {1{`RANDOM}};
  uops_2_ctrl_op_fcn = _RAND_340[3:0];
  _RAND_341 = {1{`RANDOM}};
  uops_2_ctrl_fcn_dw = _RAND_341[0:0];
  _RAND_342 = {1{`RANDOM}};
  uops_2_ctrl_csr_cmd = _RAND_342[2:0];
  _RAND_343 = {1{`RANDOM}};
  uops_2_ctrl_is_load = _RAND_343[0:0];
  _RAND_344 = {1{`RANDOM}};
  uops_2_ctrl_is_sta = _RAND_344[0:0];
  _RAND_345 = {1{`RANDOM}};
  uops_2_ctrl_is_std = _RAND_345[0:0];
  _RAND_346 = {1{`RANDOM}};
  uops_2_iw_state = _RAND_346[1:0];
  _RAND_347 = {1{`RANDOM}};
  uops_2_iw_p1_poisoned = _RAND_347[0:0];
  _RAND_348 = {1{`RANDOM}};
  uops_2_iw_p2_poisoned = _RAND_348[0:0];
  _RAND_349 = {1{`RANDOM}};
  uops_2_is_br = _RAND_349[0:0];
  _RAND_350 = {1{`RANDOM}};
  uops_2_is_jalr = _RAND_350[0:0];
  _RAND_351 = {1{`RANDOM}};
  uops_2_is_jal = _RAND_351[0:0];
  _RAND_352 = {1{`RANDOM}};
  uops_2_is_sfb = _RAND_352[0:0];
  _RAND_353 = {1{`RANDOM}};
  uops_2_br_mask = _RAND_353[7:0];
  _RAND_354 = {1{`RANDOM}};
  uops_2_br_tag = _RAND_354[2:0];
  _RAND_355 = {1{`RANDOM}};
  uops_2_ftq_idx = _RAND_355[3:0];
  _RAND_356 = {1{`RANDOM}};
  uops_2_edge_inst = _RAND_356[0:0];
  _RAND_357 = {1{`RANDOM}};
  uops_2_pc_lob = _RAND_357[5:0];
  _RAND_358 = {1{`RANDOM}};
  uops_2_taken = _RAND_358[0:0];
  _RAND_359 = {1{`RANDOM}};
  uops_2_imm_packed = _RAND_359[19:0];
  _RAND_360 = {1{`RANDOM}};
  uops_2_csr_addr = _RAND_360[11:0];
  _RAND_361 = {1{`RANDOM}};
  uops_2_rob_idx = _RAND_361[4:0];
  _RAND_362 = {1{`RANDOM}};
  uops_2_ldq_idx = _RAND_362[2:0];
  _RAND_363 = {1{`RANDOM}};
  uops_2_stq_idx = _RAND_363[2:0];
  _RAND_364 = {1{`RANDOM}};
  uops_2_rxq_idx = _RAND_364[1:0];
  _RAND_365 = {1{`RANDOM}};
  uops_2_pdst = _RAND_365[5:0];
  _RAND_366 = {1{`RANDOM}};
  uops_2_prs1 = _RAND_366[5:0];
  _RAND_367 = {1{`RANDOM}};
  uops_2_prs2 = _RAND_367[5:0];
  _RAND_368 = {1{`RANDOM}};
  uops_2_prs3 = _RAND_368[5:0];
  _RAND_369 = {1{`RANDOM}};
  uops_2_ppred = _RAND_369[3:0];
  _RAND_370 = {1{`RANDOM}};
  uops_2_prs1_busy = _RAND_370[0:0];
  _RAND_371 = {1{`RANDOM}};
  uops_2_prs2_busy = _RAND_371[0:0];
  _RAND_372 = {1{`RANDOM}};
  uops_2_prs3_busy = _RAND_372[0:0];
  _RAND_373 = {1{`RANDOM}};
  uops_2_ppred_busy = _RAND_373[0:0];
  _RAND_374 = {1{`RANDOM}};
  uops_2_stale_pdst = _RAND_374[5:0];
  _RAND_375 = {1{`RANDOM}};
  uops_2_exception = _RAND_375[0:0];
  _RAND_376 = {2{`RANDOM}};
  uops_2_exc_cause = _RAND_376[63:0];
  _RAND_377 = {1{`RANDOM}};
  uops_2_bypassable = _RAND_377[0:0];
  _RAND_378 = {1{`RANDOM}};
  uops_2_mem_cmd = _RAND_378[4:0];
  _RAND_379 = {1{`RANDOM}};
  uops_2_mem_size = _RAND_379[1:0];
  _RAND_380 = {1{`RANDOM}};
  uops_2_mem_signed = _RAND_380[0:0];
  _RAND_381 = {1{`RANDOM}};
  uops_2_is_fence = _RAND_381[0:0];
  _RAND_382 = {1{`RANDOM}};
  uops_2_is_fencei = _RAND_382[0:0];
  _RAND_383 = {1{`RANDOM}};
  uops_2_is_amo = _RAND_383[0:0];
  _RAND_384 = {1{`RANDOM}};
  uops_2_uses_ldq = _RAND_384[0:0];
  _RAND_385 = {1{`RANDOM}};
  uops_2_uses_stq = _RAND_385[0:0];
  _RAND_386 = {1{`RANDOM}};
  uops_2_is_sys_pc2epc = _RAND_386[0:0];
  _RAND_387 = {1{`RANDOM}};
  uops_2_is_unique = _RAND_387[0:0];
  _RAND_388 = {1{`RANDOM}};
  uops_2_flush_on_commit = _RAND_388[0:0];
  _RAND_389 = {1{`RANDOM}};
  uops_2_ldst_is_rs1 = _RAND_389[0:0];
  _RAND_390 = {1{`RANDOM}};
  uops_2_ldst = _RAND_390[5:0];
  _RAND_391 = {1{`RANDOM}};
  uops_2_lrs1 = _RAND_391[5:0];
  _RAND_392 = {1{`RANDOM}};
  uops_2_lrs2 = _RAND_392[5:0];
  _RAND_393 = {1{`RANDOM}};
  uops_2_lrs3 = _RAND_393[5:0];
  _RAND_394 = {1{`RANDOM}};
  uops_2_ldst_val = _RAND_394[0:0];
  _RAND_395 = {1{`RANDOM}};
  uops_2_dst_rtype = _RAND_395[1:0];
  _RAND_396 = {1{`RANDOM}};
  uops_2_lrs1_rtype = _RAND_396[1:0];
  _RAND_397 = {1{`RANDOM}};
  uops_2_lrs2_rtype = _RAND_397[1:0];
  _RAND_398 = {1{`RANDOM}};
  uops_2_frs3_en = _RAND_398[0:0];
  _RAND_399 = {1{`RANDOM}};
  uops_2_fp_val = _RAND_399[0:0];
  _RAND_400 = {1{`RANDOM}};
  uops_2_fp_single = _RAND_400[0:0];
  _RAND_401 = {1{`RANDOM}};
  uops_2_xcpt_pf_if = _RAND_401[0:0];
  _RAND_402 = {1{`RANDOM}};
  uops_2_xcpt_ae_if = _RAND_402[0:0];
  _RAND_403 = {1{`RANDOM}};
  uops_2_xcpt_ma_if = _RAND_403[0:0];
  _RAND_404 = {1{`RANDOM}};
  uops_2_bp_debug_if = _RAND_404[0:0];
  _RAND_405 = {1{`RANDOM}};
  uops_2_bp_xcpt_if = _RAND_405[0:0];
  _RAND_406 = {1{`RANDOM}};
  uops_2_debug_fsrc = _RAND_406[1:0];
  _RAND_407 = {1{`RANDOM}};
  uops_2_debug_tsrc = _RAND_407[1:0];
  _RAND_408 = {1{`RANDOM}};
  uops_3_uopc = _RAND_408[6:0];
  _RAND_409 = {1{`RANDOM}};
  uops_3_inst = _RAND_409[31:0];
  _RAND_410 = {1{`RANDOM}};
  uops_3_debug_inst = _RAND_410[31:0];
  _RAND_411 = {1{`RANDOM}};
  uops_3_is_rvc = _RAND_411[0:0];
  _RAND_412 = {2{`RANDOM}};
  uops_3_debug_pc = _RAND_412[39:0];
  _RAND_413 = {1{`RANDOM}};
  uops_3_iq_type = _RAND_413[2:0];
  _RAND_414 = {1{`RANDOM}};
  uops_3_fu_code = _RAND_414[9:0];
  _RAND_415 = {1{`RANDOM}};
  uops_3_ctrl_br_type = _RAND_415[3:0];
  _RAND_416 = {1{`RANDOM}};
  uops_3_ctrl_op1_sel = _RAND_416[1:0];
  _RAND_417 = {1{`RANDOM}};
  uops_3_ctrl_op2_sel = _RAND_417[2:0];
  _RAND_418 = {1{`RANDOM}};
  uops_3_ctrl_imm_sel = _RAND_418[2:0];
  _RAND_419 = {1{`RANDOM}};
  uops_3_ctrl_op_fcn = _RAND_419[3:0];
  _RAND_420 = {1{`RANDOM}};
  uops_3_ctrl_fcn_dw = _RAND_420[0:0];
  _RAND_421 = {1{`RANDOM}};
  uops_3_ctrl_csr_cmd = _RAND_421[2:0];
  _RAND_422 = {1{`RANDOM}};
  uops_3_ctrl_is_load = _RAND_422[0:0];
  _RAND_423 = {1{`RANDOM}};
  uops_3_ctrl_is_sta = _RAND_423[0:0];
  _RAND_424 = {1{`RANDOM}};
  uops_3_ctrl_is_std = _RAND_424[0:0];
  _RAND_425 = {1{`RANDOM}};
  uops_3_iw_state = _RAND_425[1:0];
  _RAND_426 = {1{`RANDOM}};
  uops_3_iw_p1_poisoned = _RAND_426[0:0];
  _RAND_427 = {1{`RANDOM}};
  uops_3_iw_p2_poisoned = _RAND_427[0:0];
  _RAND_428 = {1{`RANDOM}};
  uops_3_is_br = _RAND_428[0:0];
  _RAND_429 = {1{`RANDOM}};
  uops_3_is_jalr = _RAND_429[0:0];
  _RAND_430 = {1{`RANDOM}};
  uops_3_is_jal = _RAND_430[0:0];
  _RAND_431 = {1{`RANDOM}};
  uops_3_is_sfb = _RAND_431[0:0];
  _RAND_432 = {1{`RANDOM}};
  uops_3_br_mask = _RAND_432[7:0];
  _RAND_433 = {1{`RANDOM}};
  uops_3_br_tag = _RAND_433[2:0];
  _RAND_434 = {1{`RANDOM}};
  uops_3_ftq_idx = _RAND_434[3:0];
  _RAND_435 = {1{`RANDOM}};
  uops_3_edge_inst = _RAND_435[0:0];
  _RAND_436 = {1{`RANDOM}};
  uops_3_pc_lob = _RAND_436[5:0];
  _RAND_437 = {1{`RANDOM}};
  uops_3_taken = _RAND_437[0:0];
  _RAND_438 = {1{`RANDOM}};
  uops_3_imm_packed = _RAND_438[19:0];
  _RAND_439 = {1{`RANDOM}};
  uops_3_csr_addr = _RAND_439[11:0];
  _RAND_440 = {1{`RANDOM}};
  uops_3_rob_idx = _RAND_440[4:0];
  _RAND_441 = {1{`RANDOM}};
  uops_3_ldq_idx = _RAND_441[2:0];
  _RAND_442 = {1{`RANDOM}};
  uops_3_stq_idx = _RAND_442[2:0];
  _RAND_443 = {1{`RANDOM}};
  uops_3_rxq_idx = _RAND_443[1:0];
  _RAND_444 = {1{`RANDOM}};
  uops_3_pdst = _RAND_444[5:0];
  _RAND_445 = {1{`RANDOM}};
  uops_3_prs1 = _RAND_445[5:0];
  _RAND_446 = {1{`RANDOM}};
  uops_3_prs2 = _RAND_446[5:0];
  _RAND_447 = {1{`RANDOM}};
  uops_3_prs3 = _RAND_447[5:0];
  _RAND_448 = {1{`RANDOM}};
  uops_3_ppred = _RAND_448[3:0];
  _RAND_449 = {1{`RANDOM}};
  uops_3_prs1_busy = _RAND_449[0:0];
  _RAND_450 = {1{`RANDOM}};
  uops_3_prs2_busy = _RAND_450[0:0];
  _RAND_451 = {1{`RANDOM}};
  uops_3_prs3_busy = _RAND_451[0:0];
  _RAND_452 = {1{`RANDOM}};
  uops_3_ppred_busy = _RAND_452[0:0];
  _RAND_453 = {1{`RANDOM}};
  uops_3_stale_pdst = _RAND_453[5:0];
  _RAND_454 = {1{`RANDOM}};
  uops_3_exception = _RAND_454[0:0];
  _RAND_455 = {2{`RANDOM}};
  uops_3_exc_cause = _RAND_455[63:0];
  _RAND_456 = {1{`RANDOM}};
  uops_3_bypassable = _RAND_456[0:0];
  _RAND_457 = {1{`RANDOM}};
  uops_3_mem_cmd = _RAND_457[4:0];
  _RAND_458 = {1{`RANDOM}};
  uops_3_mem_size = _RAND_458[1:0];
  _RAND_459 = {1{`RANDOM}};
  uops_3_mem_signed = _RAND_459[0:0];
  _RAND_460 = {1{`RANDOM}};
  uops_3_is_fence = _RAND_460[0:0];
  _RAND_461 = {1{`RANDOM}};
  uops_3_is_fencei = _RAND_461[0:0];
  _RAND_462 = {1{`RANDOM}};
  uops_3_is_amo = _RAND_462[0:0];
  _RAND_463 = {1{`RANDOM}};
  uops_3_uses_ldq = _RAND_463[0:0];
  _RAND_464 = {1{`RANDOM}};
  uops_3_uses_stq = _RAND_464[0:0];
  _RAND_465 = {1{`RANDOM}};
  uops_3_is_sys_pc2epc = _RAND_465[0:0];
  _RAND_466 = {1{`RANDOM}};
  uops_3_is_unique = _RAND_466[0:0];
  _RAND_467 = {1{`RANDOM}};
  uops_3_flush_on_commit = _RAND_467[0:0];
  _RAND_468 = {1{`RANDOM}};
  uops_3_ldst_is_rs1 = _RAND_468[0:0];
  _RAND_469 = {1{`RANDOM}};
  uops_3_ldst = _RAND_469[5:0];
  _RAND_470 = {1{`RANDOM}};
  uops_3_lrs1 = _RAND_470[5:0];
  _RAND_471 = {1{`RANDOM}};
  uops_3_lrs2 = _RAND_471[5:0];
  _RAND_472 = {1{`RANDOM}};
  uops_3_lrs3 = _RAND_472[5:0];
  _RAND_473 = {1{`RANDOM}};
  uops_3_ldst_val = _RAND_473[0:0];
  _RAND_474 = {1{`RANDOM}};
  uops_3_dst_rtype = _RAND_474[1:0];
  _RAND_475 = {1{`RANDOM}};
  uops_3_lrs1_rtype = _RAND_475[1:0];
  _RAND_476 = {1{`RANDOM}};
  uops_3_lrs2_rtype = _RAND_476[1:0];
  _RAND_477 = {1{`RANDOM}};
  uops_3_frs3_en = _RAND_477[0:0];
  _RAND_478 = {1{`RANDOM}};
  uops_3_fp_val = _RAND_478[0:0];
  _RAND_479 = {1{`RANDOM}};
  uops_3_fp_single = _RAND_479[0:0];
  _RAND_480 = {1{`RANDOM}};
  uops_3_xcpt_pf_if = _RAND_480[0:0];
  _RAND_481 = {1{`RANDOM}};
  uops_3_xcpt_ae_if = _RAND_481[0:0];
  _RAND_482 = {1{`RANDOM}};
  uops_3_xcpt_ma_if = _RAND_482[0:0];
  _RAND_483 = {1{`RANDOM}};
  uops_3_bp_debug_if = _RAND_483[0:0];
  _RAND_484 = {1{`RANDOM}};
  uops_3_bp_xcpt_if = _RAND_484[0:0];
  _RAND_485 = {1{`RANDOM}};
  uops_3_debug_fsrc = _RAND_485[1:0];
  _RAND_486 = {1{`RANDOM}};
  uops_3_debug_tsrc = _RAND_486[1:0];
  _RAND_487 = {1{`RANDOM}};
  uops_4_uopc = _RAND_487[6:0];
  _RAND_488 = {1{`RANDOM}};
  uops_4_inst = _RAND_488[31:0];
  _RAND_489 = {1{`RANDOM}};
  uops_4_debug_inst = _RAND_489[31:0];
  _RAND_490 = {1{`RANDOM}};
  uops_4_is_rvc = _RAND_490[0:0];
  _RAND_491 = {2{`RANDOM}};
  uops_4_debug_pc = _RAND_491[39:0];
  _RAND_492 = {1{`RANDOM}};
  uops_4_iq_type = _RAND_492[2:0];
  _RAND_493 = {1{`RANDOM}};
  uops_4_fu_code = _RAND_493[9:0];
  _RAND_494 = {1{`RANDOM}};
  uops_4_ctrl_br_type = _RAND_494[3:0];
  _RAND_495 = {1{`RANDOM}};
  uops_4_ctrl_op1_sel = _RAND_495[1:0];
  _RAND_496 = {1{`RANDOM}};
  uops_4_ctrl_op2_sel = _RAND_496[2:0];
  _RAND_497 = {1{`RANDOM}};
  uops_4_ctrl_imm_sel = _RAND_497[2:0];
  _RAND_498 = {1{`RANDOM}};
  uops_4_ctrl_op_fcn = _RAND_498[3:0];
  _RAND_499 = {1{`RANDOM}};
  uops_4_ctrl_fcn_dw = _RAND_499[0:0];
  _RAND_500 = {1{`RANDOM}};
  uops_4_ctrl_csr_cmd = _RAND_500[2:0];
  _RAND_501 = {1{`RANDOM}};
  uops_4_ctrl_is_load = _RAND_501[0:0];
  _RAND_502 = {1{`RANDOM}};
  uops_4_ctrl_is_sta = _RAND_502[0:0];
  _RAND_503 = {1{`RANDOM}};
  uops_4_ctrl_is_std = _RAND_503[0:0];
  _RAND_504 = {1{`RANDOM}};
  uops_4_iw_state = _RAND_504[1:0];
  _RAND_505 = {1{`RANDOM}};
  uops_4_iw_p1_poisoned = _RAND_505[0:0];
  _RAND_506 = {1{`RANDOM}};
  uops_4_iw_p2_poisoned = _RAND_506[0:0];
  _RAND_507 = {1{`RANDOM}};
  uops_4_is_br = _RAND_507[0:0];
  _RAND_508 = {1{`RANDOM}};
  uops_4_is_jalr = _RAND_508[0:0];
  _RAND_509 = {1{`RANDOM}};
  uops_4_is_jal = _RAND_509[0:0];
  _RAND_510 = {1{`RANDOM}};
  uops_4_is_sfb = _RAND_510[0:0];
  _RAND_511 = {1{`RANDOM}};
  uops_4_br_mask = _RAND_511[7:0];
  _RAND_512 = {1{`RANDOM}};
  uops_4_br_tag = _RAND_512[2:0];
  _RAND_513 = {1{`RANDOM}};
  uops_4_ftq_idx = _RAND_513[3:0];
  _RAND_514 = {1{`RANDOM}};
  uops_4_edge_inst = _RAND_514[0:0];
  _RAND_515 = {1{`RANDOM}};
  uops_4_pc_lob = _RAND_515[5:0];
  _RAND_516 = {1{`RANDOM}};
  uops_4_taken = _RAND_516[0:0];
  _RAND_517 = {1{`RANDOM}};
  uops_4_imm_packed = _RAND_517[19:0];
  _RAND_518 = {1{`RANDOM}};
  uops_4_csr_addr = _RAND_518[11:0];
  _RAND_519 = {1{`RANDOM}};
  uops_4_rob_idx = _RAND_519[4:0];
  _RAND_520 = {1{`RANDOM}};
  uops_4_ldq_idx = _RAND_520[2:0];
  _RAND_521 = {1{`RANDOM}};
  uops_4_stq_idx = _RAND_521[2:0];
  _RAND_522 = {1{`RANDOM}};
  uops_4_rxq_idx = _RAND_522[1:0];
  _RAND_523 = {1{`RANDOM}};
  uops_4_pdst = _RAND_523[5:0];
  _RAND_524 = {1{`RANDOM}};
  uops_4_prs1 = _RAND_524[5:0];
  _RAND_525 = {1{`RANDOM}};
  uops_4_prs2 = _RAND_525[5:0];
  _RAND_526 = {1{`RANDOM}};
  uops_4_prs3 = _RAND_526[5:0];
  _RAND_527 = {1{`RANDOM}};
  uops_4_ppred = _RAND_527[3:0];
  _RAND_528 = {1{`RANDOM}};
  uops_4_prs1_busy = _RAND_528[0:0];
  _RAND_529 = {1{`RANDOM}};
  uops_4_prs2_busy = _RAND_529[0:0];
  _RAND_530 = {1{`RANDOM}};
  uops_4_prs3_busy = _RAND_530[0:0];
  _RAND_531 = {1{`RANDOM}};
  uops_4_ppred_busy = _RAND_531[0:0];
  _RAND_532 = {1{`RANDOM}};
  uops_4_stale_pdst = _RAND_532[5:0];
  _RAND_533 = {1{`RANDOM}};
  uops_4_exception = _RAND_533[0:0];
  _RAND_534 = {2{`RANDOM}};
  uops_4_exc_cause = _RAND_534[63:0];
  _RAND_535 = {1{`RANDOM}};
  uops_4_bypassable = _RAND_535[0:0];
  _RAND_536 = {1{`RANDOM}};
  uops_4_mem_cmd = _RAND_536[4:0];
  _RAND_537 = {1{`RANDOM}};
  uops_4_mem_size = _RAND_537[1:0];
  _RAND_538 = {1{`RANDOM}};
  uops_4_mem_signed = _RAND_538[0:0];
  _RAND_539 = {1{`RANDOM}};
  uops_4_is_fence = _RAND_539[0:0];
  _RAND_540 = {1{`RANDOM}};
  uops_4_is_fencei = _RAND_540[0:0];
  _RAND_541 = {1{`RANDOM}};
  uops_4_is_amo = _RAND_541[0:0];
  _RAND_542 = {1{`RANDOM}};
  uops_4_uses_ldq = _RAND_542[0:0];
  _RAND_543 = {1{`RANDOM}};
  uops_4_uses_stq = _RAND_543[0:0];
  _RAND_544 = {1{`RANDOM}};
  uops_4_is_sys_pc2epc = _RAND_544[0:0];
  _RAND_545 = {1{`RANDOM}};
  uops_4_is_unique = _RAND_545[0:0];
  _RAND_546 = {1{`RANDOM}};
  uops_4_flush_on_commit = _RAND_546[0:0];
  _RAND_547 = {1{`RANDOM}};
  uops_4_ldst_is_rs1 = _RAND_547[0:0];
  _RAND_548 = {1{`RANDOM}};
  uops_4_ldst = _RAND_548[5:0];
  _RAND_549 = {1{`RANDOM}};
  uops_4_lrs1 = _RAND_549[5:0];
  _RAND_550 = {1{`RANDOM}};
  uops_4_lrs2 = _RAND_550[5:0];
  _RAND_551 = {1{`RANDOM}};
  uops_4_lrs3 = _RAND_551[5:0];
  _RAND_552 = {1{`RANDOM}};
  uops_4_ldst_val = _RAND_552[0:0];
  _RAND_553 = {1{`RANDOM}};
  uops_4_dst_rtype = _RAND_553[1:0];
  _RAND_554 = {1{`RANDOM}};
  uops_4_lrs1_rtype = _RAND_554[1:0];
  _RAND_555 = {1{`RANDOM}};
  uops_4_lrs2_rtype = _RAND_555[1:0];
  _RAND_556 = {1{`RANDOM}};
  uops_4_frs3_en = _RAND_556[0:0];
  _RAND_557 = {1{`RANDOM}};
  uops_4_fp_val = _RAND_557[0:0];
  _RAND_558 = {1{`RANDOM}};
  uops_4_fp_single = _RAND_558[0:0];
  _RAND_559 = {1{`RANDOM}};
  uops_4_xcpt_pf_if = _RAND_559[0:0];
  _RAND_560 = {1{`RANDOM}};
  uops_4_xcpt_ae_if = _RAND_560[0:0];
  _RAND_561 = {1{`RANDOM}};
  uops_4_xcpt_ma_if = _RAND_561[0:0];
  _RAND_562 = {1{`RANDOM}};
  uops_4_bp_debug_if = _RAND_562[0:0];
  _RAND_563 = {1{`RANDOM}};
  uops_4_bp_xcpt_if = _RAND_563[0:0];
  _RAND_564 = {1{`RANDOM}};
  uops_4_debug_fsrc = _RAND_564[1:0];
  _RAND_565 = {1{`RANDOM}};
  uops_4_debug_tsrc = _RAND_565[1:0];
  _RAND_566 = {1{`RANDOM}};
  value = _RAND_566[2:0];
  _RAND_567 = {1{`RANDOM}};
  value_1 = _RAND_567[2:0];
  _RAND_568 = {1{`RANDOM}};
  maybe_full = _RAND_568[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
