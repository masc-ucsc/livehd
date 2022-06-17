module BasicDispatcher(
  input         clock,
  input         reset,
  output        io_ren_uops_0_ready,
  input         io_ren_uops_0_valid,
  input  [6:0]  io_ren_uops_0_bits_uopc,
  input  [31:0] io_ren_uops_0_bits_inst,
  input  [31:0] io_ren_uops_0_bits_debug_inst,
  input         io_ren_uops_0_bits_is_rvc,
  input  [39:0] io_ren_uops_0_bits_debug_pc,
  input  [2:0]  io_ren_uops_0_bits_iq_type,
  input  [9:0]  io_ren_uops_0_bits_fu_code,
  input  [3:0]  io_ren_uops_0_bits_ctrl_br_type,
  input  [1:0]  io_ren_uops_0_bits_ctrl_op1_sel,
  input  [2:0]  io_ren_uops_0_bits_ctrl_op2_sel,
  input  [2:0]  io_ren_uops_0_bits_ctrl_imm_sel,
  input  [3:0]  io_ren_uops_0_bits_ctrl_op_fcn,
  input         io_ren_uops_0_bits_ctrl_fcn_dw,
  input  [2:0]  io_ren_uops_0_bits_ctrl_csr_cmd,
  input         io_ren_uops_0_bits_ctrl_is_load,
  input         io_ren_uops_0_bits_ctrl_is_sta,
  input         io_ren_uops_0_bits_ctrl_is_std,
  input  [1:0]  io_ren_uops_0_bits_iw_state,
  input         io_ren_uops_0_bits_iw_p1_poisoned,
  input         io_ren_uops_0_bits_iw_p2_poisoned,
  input         io_ren_uops_0_bits_is_br,
  input         io_ren_uops_0_bits_is_jalr,
  input         io_ren_uops_0_bits_is_jal,
  input         io_ren_uops_0_bits_is_sfb,
  input  [7:0]  io_ren_uops_0_bits_br_mask,
  input  [2:0]  io_ren_uops_0_bits_br_tag,
  input  [3:0]  io_ren_uops_0_bits_ftq_idx,
  input         io_ren_uops_0_bits_edge_inst,
  input  [5:0]  io_ren_uops_0_bits_pc_lob,
  input         io_ren_uops_0_bits_taken,
  input  [19:0] io_ren_uops_0_bits_imm_packed,
  input  [11:0] io_ren_uops_0_bits_csr_addr,
  input  [4:0]  io_ren_uops_0_bits_rob_idx,
  input  [2:0]  io_ren_uops_0_bits_ldq_idx,
  input  [2:0]  io_ren_uops_0_bits_stq_idx,
  input  [1:0]  io_ren_uops_0_bits_rxq_idx,
  input  [5:0]  io_ren_uops_0_bits_pdst,
  input  [5:0]  io_ren_uops_0_bits_prs1,
  input  [5:0]  io_ren_uops_0_bits_prs2,
  input  [5:0]  io_ren_uops_0_bits_prs3,
  input  [3:0]  io_ren_uops_0_bits_ppred,
  input         io_ren_uops_0_bits_prs1_busy,
  input         io_ren_uops_0_bits_prs2_busy,
  input         io_ren_uops_0_bits_prs3_busy,
  input         io_ren_uops_0_bits_ppred_busy,
  input  [5:0]  io_ren_uops_0_bits_stale_pdst,
  input         io_ren_uops_0_bits_exception,
  input  [63:0] io_ren_uops_0_bits_exc_cause,
  input         io_ren_uops_0_bits_bypassable,
  input  [4:0]  io_ren_uops_0_bits_mem_cmd,
  input  [1:0]  io_ren_uops_0_bits_mem_size,
  input         io_ren_uops_0_bits_mem_signed,
  input         io_ren_uops_0_bits_is_fence,
  input         io_ren_uops_0_bits_is_fencei,
  input         io_ren_uops_0_bits_is_amo,
  input         io_ren_uops_0_bits_uses_ldq,
  input         io_ren_uops_0_bits_uses_stq,
  input         io_ren_uops_0_bits_is_sys_pc2epc,
  input         io_ren_uops_0_bits_is_unique,
  input         io_ren_uops_0_bits_flush_on_commit,
  input         io_ren_uops_0_bits_ldst_is_rs1,
  input  [5:0]  io_ren_uops_0_bits_ldst,
  input  [5:0]  io_ren_uops_0_bits_lrs1,
  input  [5:0]  io_ren_uops_0_bits_lrs2,
  input  [5:0]  io_ren_uops_0_bits_lrs3,
  input         io_ren_uops_0_bits_ldst_val,
  input  [1:0]  io_ren_uops_0_bits_dst_rtype,
  input  [1:0]  io_ren_uops_0_bits_lrs1_rtype,
  input  [1:0]  io_ren_uops_0_bits_lrs2_rtype,
  input         io_ren_uops_0_bits_frs3_en,
  input         io_ren_uops_0_bits_fp_val,
  input         io_ren_uops_0_bits_fp_single,
  input         io_ren_uops_0_bits_xcpt_pf_if,
  input         io_ren_uops_0_bits_xcpt_ae_if,
  input         io_ren_uops_0_bits_xcpt_ma_if,
  input         io_ren_uops_0_bits_bp_debug_if,
  input         io_ren_uops_0_bits_bp_xcpt_if,
  input  [1:0]  io_ren_uops_0_bits_debug_fsrc,
  input  [1:0]  io_ren_uops_0_bits_debug_tsrc,
  input         io_dis_uops_2_0_ready,
  output        io_dis_uops_2_0_valid,
  output [6:0]  io_dis_uops_2_0_bits_uopc,
  output [31:0] io_dis_uops_2_0_bits_inst,
  output [31:0] io_dis_uops_2_0_bits_debug_inst,
  output        io_dis_uops_2_0_bits_is_rvc,
  output [39:0] io_dis_uops_2_0_bits_debug_pc,
  output [2:0]  io_dis_uops_2_0_bits_iq_type,
  output [9:0]  io_dis_uops_2_0_bits_fu_code,
  output [3:0]  io_dis_uops_2_0_bits_ctrl_br_type,
  output [1:0]  io_dis_uops_2_0_bits_ctrl_op1_sel,
  output [2:0]  io_dis_uops_2_0_bits_ctrl_op2_sel,
  output [2:0]  io_dis_uops_2_0_bits_ctrl_imm_sel,
  output [3:0]  io_dis_uops_2_0_bits_ctrl_op_fcn,
  output        io_dis_uops_2_0_bits_ctrl_fcn_dw,
  output [2:0]  io_dis_uops_2_0_bits_ctrl_csr_cmd,
  output        io_dis_uops_2_0_bits_ctrl_is_load,
  output        io_dis_uops_2_0_bits_ctrl_is_sta,
  output        io_dis_uops_2_0_bits_ctrl_is_std,
  output [1:0]  io_dis_uops_2_0_bits_iw_state,
  output        io_dis_uops_2_0_bits_iw_p1_poisoned,
  output        io_dis_uops_2_0_bits_iw_p2_poisoned,
  output        io_dis_uops_2_0_bits_is_br,
  output        io_dis_uops_2_0_bits_is_jalr,
  output        io_dis_uops_2_0_bits_is_jal,
  output        io_dis_uops_2_0_bits_is_sfb,
  output [7:0]  io_dis_uops_2_0_bits_br_mask,
  output [2:0]  io_dis_uops_2_0_bits_br_tag,
  output [3:0]  io_dis_uops_2_0_bits_ftq_idx,
  output        io_dis_uops_2_0_bits_edge_inst,
  output [5:0]  io_dis_uops_2_0_bits_pc_lob,
  output        io_dis_uops_2_0_bits_taken,
  output [19:0] io_dis_uops_2_0_bits_imm_packed,
  output [11:0] io_dis_uops_2_0_bits_csr_addr,
  output [4:0]  io_dis_uops_2_0_bits_rob_idx,
  output [2:0]  io_dis_uops_2_0_bits_ldq_idx,
  output [2:0]  io_dis_uops_2_0_bits_stq_idx,
  output [1:0]  io_dis_uops_2_0_bits_rxq_idx,
  output [5:0]  io_dis_uops_2_0_bits_pdst,
  output [5:0]  io_dis_uops_2_0_bits_prs1,
  output [5:0]  io_dis_uops_2_0_bits_prs2,
  output [5:0]  io_dis_uops_2_0_bits_prs3,
  output [3:0]  io_dis_uops_2_0_bits_ppred,
  output        io_dis_uops_2_0_bits_prs1_busy,
  output        io_dis_uops_2_0_bits_prs2_busy,
  output        io_dis_uops_2_0_bits_prs3_busy,
  output        io_dis_uops_2_0_bits_ppred_busy,
  output [5:0]  io_dis_uops_2_0_bits_stale_pdst,
  output        io_dis_uops_2_0_bits_exception,
  output [63:0] io_dis_uops_2_0_bits_exc_cause,
  output        io_dis_uops_2_0_bits_bypassable,
  output [4:0]  io_dis_uops_2_0_bits_mem_cmd,
  output [1:0]  io_dis_uops_2_0_bits_mem_size,
  output        io_dis_uops_2_0_bits_mem_signed,
  output        io_dis_uops_2_0_bits_is_fence,
  output        io_dis_uops_2_0_bits_is_fencei,
  output        io_dis_uops_2_0_bits_is_amo,
  output        io_dis_uops_2_0_bits_uses_ldq,
  output        io_dis_uops_2_0_bits_uses_stq,
  output        io_dis_uops_2_0_bits_is_sys_pc2epc,
  output        io_dis_uops_2_0_bits_is_unique,
  output        io_dis_uops_2_0_bits_flush_on_commit,
  output        io_dis_uops_2_0_bits_ldst_is_rs1,
  output [5:0]  io_dis_uops_2_0_bits_ldst,
  output [5:0]  io_dis_uops_2_0_bits_lrs1,
  output [5:0]  io_dis_uops_2_0_bits_lrs2,
  output [5:0]  io_dis_uops_2_0_bits_lrs3,
  output        io_dis_uops_2_0_bits_ldst_val,
  output [1:0]  io_dis_uops_2_0_bits_dst_rtype,
  output [1:0]  io_dis_uops_2_0_bits_lrs1_rtype,
  output [1:0]  io_dis_uops_2_0_bits_lrs2_rtype,
  output        io_dis_uops_2_0_bits_frs3_en,
  output        io_dis_uops_2_0_bits_fp_val,
  output        io_dis_uops_2_0_bits_fp_single,
  output        io_dis_uops_2_0_bits_xcpt_pf_if,
  output        io_dis_uops_2_0_bits_xcpt_ae_if,
  output        io_dis_uops_2_0_bits_xcpt_ma_if,
  output        io_dis_uops_2_0_bits_bp_debug_if,
  output        io_dis_uops_2_0_bits_bp_xcpt_if,
  output [1:0]  io_dis_uops_2_0_bits_debug_fsrc,
  output [1:0]  io_dis_uops_2_0_bits_debug_tsrc,
  input         io_dis_uops_1_0_ready,
  output        io_dis_uops_1_0_valid,
  output [6:0]  io_dis_uops_1_0_bits_uopc,
  output [31:0] io_dis_uops_1_0_bits_inst,
  output [31:0] io_dis_uops_1_0_bits_debug_inst,
  output        io_dis_uops_1_0_bits_is_rvc,
  output [39:0] io_dis_uops_1_0_bits_debug_pc,
  output [2:0]  io_dis_uops_1_0_bits_iq_type,
  output [9:0]  io_dis_uops_1_0_bits_fu_code,
  output [3:0]  io_dis_uops_1_0_bits_ctrl_br_type,
  output [1:0]  io_dis_uops_1_0_bits_ctrl_op1_sel,
  output [2:0]  io_dis_uops_1_0_bits_ctrl_op2_sel,
  output [2:0]  io_dis_uops_1_0_bits_ctrl_imm_sel,
  output [3:0]  io_dis_uops_1_0_bits_ctrl_op_fcn,
  output        io_dis_uops_1_0_bits_ctrl_fcn_dw,
  output [2:0]  io_dis_uops_1_0_bits_ctrl_csr_cmd,
  output        io_dis_uops_1_0_bits_ctrl_is_load,
  output        io_dis_uops_1_0_bits_ctrl_is_sta,
  output        io_dis_uops_1_0_bits_ctrl_is_std,
  output [1:0]  io_dis_uops_1_0_bits_iw_state,
  output        io_dis_uops_1_0_bits_iw_p1_poisoned,
  output        io_dis_uops_1_0_bits_iw_p2_poisoned,
  output        io_dis_uops_1_0_bits_is_br,
  output        io_dis_uops_1_0_bits_is_jalr,
  output        io_dis_uops_1_0_bits_is_jal,
  output        io_dis_uops_1_0_bits_is_sfb,
  output [7:0]  io_dis_uops_1_0_bits_br_mask,
  output [2:0]  io_dis_uops_1_0_bits_br_tag,
  output [3:0]  io_dis_uops_1_0_bits_ftq_idx,
  output        io_dis_uops_1_0_bits_edge_inst,
  output [5:0]  io_dis_uops_1_0_bits_pc_lob,
  output        io_dis_uops_1_0_bits_taken,
  output [19:0] io_dis_uops_1_0_bits_imm_packed,
  output [11:0] io_dis_uops_1_0_bits_csr_addr,
  output [4:0]  io_dis_uops_1_0_bits_rob_idx,
  output [2:0]  io_dis_uops_1_0_bits_ldq_idx,
  output [2:0]  io_dis_uops_1_0_bits_stq_idx,
  output [1:0]  io_dis_uops_1_0_bits_rxq_idx,
  output [5:0]  io_dis_uops_1_0_bits_pdst,
  output [5:0]  io_dis_uops_1_0_bits_prs1,
  output [5:0]  io_dis_uops_1_0_bits_prs2,
  output [5:0]  io_dis_uops_1_0_bits_prs3,
  output [3:0]  io_dis_uops_1_0_bits_ppred,
  output        io_dis_uops_1_0_bits_prs1_busy,
  output        io_dis_uops_1_0_bits_prs2_busy,
  output        io_dis_uops_1_0_bits_prs3_busy,
  output        io_dis_uops_1_0_bits_ppred_busy,
  output [5:0]  io_dis_uops_1_0_bits_stale_pdst,
  output        io_dis_uops_1_0_bits_exception,
  output [63:0] io_dis_uops_1_0_bits_exc_cause,
  output        io_dis_uops_1_0_bits_bypassable,
  output [4:0]  io_dis_uops_1_0_bits_mem_cmd,
  output [1:0]  io_dis_uops_1_0_bits_mem_size,
  output        io_dis_uops_1_0_bits_mem_signed,
  output        io_dis_uops_1_0_bits_is_fence,
  output        io_dis_uops_1_0_bits_is_fencei,
  output        io_dis_uops_1_0_bits_is_amo,
  output        io_dis_uops_1_0_bits_uses_ldq,
  output        io_dis_uops_1_0_bits_uses_stq,
  output        io_dis_uops_1_0_bits_is_sys_pc2epc,
  output        io_dis_uops_1_0_bits_is_unique,
  output        io_dis_uops_1_0_bits_flush_on_commit,
  output        io_dis_uops_1_0_bits_ldst_is_rs1,
  output [5:0]  io_dis_uops_1_0_bits_ldst,
  output [5:0]  io_dis_uops_1_0_bits_lrs1,
  output [5:0]  io_dis_uops_1_0_bits_lrs2,
  output [5:0]  io_dis_uops_1_0_bits_lrs3,
  output        io_dis_uops_1_0_bits_ldst_val,
  output [1:0]  io_dis_uops_1_0_bits_dst_rtype,
  output [1:0]  io_dis_uops_1_0_bits_lrs1_rtype,
  output [1:0]  io_dis_uops_1_0_bits_lrs2_rtype,
  output        io_dis_uops_1_0_bits_frs3_en,
  output        io_dis_uops_1_0_bits_fp_val,
  output        io_dis_uops_1_0_bits_fp_single,
  output        io_dis_uops_1_0_bits_xcpt_pf_if,
  output        io_dis_uops_1_0_bits_xcpt_ae_if,
  output        io_dis_uops_1_0_bits_xcpt_ma_if,
  output        io_dis_uops_1_0_bits_bp_debug_if,
  output        io_dis_uops_1_0_bits_bp_xcpt_if,
  output [1:0]  io_dis_uops_1_0_bits_debug_fsrc,
  output [1:0]  io_dis_uops_1_0_bits_debug_tsrc,
  input         io_dis_uops_0_0_ready,
  output        io_dis_uops_0_0_valid,
  output [6:0]  io_dis_uops_0_0_bits_uopc,
  output [31:0] io_dis_uops_0_0_bits_inst,
  output [31:0] io_dis_uops_0_0_bits_debug_inst,
  output        io_dis_uops_0_0_bits_is_rvc,
  output [39:0] io_dis_uops_0_0_bits_debug_pc,
  output [2:0]  io_dis_uops_0_0_bits_iq_type,
  output [9:0]  io_dis_uops_0_0_bits_fu_code,
  output [3:0]  io_dis_uops_0_0_bits_ctrl_br_type,
  output [1:0]  io_dis_uops_0_0_bits_ctrl_op1_sel,
  output [2:0]  io_dis_uops_0_0_bits_ctrl_op2_sel,
  output [2:0]  io_dis_uops_0_0_bits_ctrl_imm_sel,
  output [3:0]  io_dis_uops_0_0_bits_ctrl_op_fcn,
  output        io_dis_uops_0_0_bits_ctrl_fcn_dw,
  output [2:0]  io_dis_uops_0_0_bits_ctrl_csr_cmd,
  output        io_dis_uops_0_0_bits_ctrl_is_load,
  output        io_dis_uops_0_0_bits_ctrl_is_sta,
  output        io_dis_uops_0_0_bits_ctrl_is_std,
  output [1:0]  io_dis_uops_0_0_bits_iw_state,
  output        io_dis_uops_0_0_bits_iw_p1_poisoned,
  output        io_dis_uops_0_0_bits_iw_p2_poisoned,
  output        io_dis_uops_0_0_bits_is_br,
  output        io_dis_uops_0_0_bits_is_jalr,
  output        io_dis_uops_0_0_bits_is_jal,
  output        io_dis_uops_0_0_bits_is_sfb,
  output [7:0]  io_dis_uops_0_0_bits_br_mask,
  output [2:0]  io_dis_uops_0_0_bits_br_tag,
  output [3:0]  io_dis_uops_0_0_bits_ftq_idx,
  output        io_dis_uops_0_0_bits_edge_inst,
  output [5:0]  io_dis_uops_0_0_bits_pc_lob,
  output        io_dis_uops_0_0_bits_taken,
  output [19:0] io_dis_uops_0_0_bits_imm_packed,
  output [11:0] io_dis_uops_0_0_bits_csr_addr,
  output [4:0]  io_dis_uops_0_0_bits_rob_idx,
  output [2:0]  io_dis_uops_0_0_bits_ldq_idx,
  output [2:0]  io_dis_uops_0_0_bits_stq_idx,
  output [1:0]  io_dis_uops_0_0_bits_rxq_idx,
  output [5:0]  io_dis_uops_0_0_bits_pdst,
  output [5:0]  io_dis_uops_0_0_bits_prs1,
  output [5:0]  io_dis_uops_0_0_bits_prs2,
  output [5:0]  io_dis_uops_0_0_bits_prs3,
  output [3:0]  io_dis_uops_0_0_bits_ppred,
  output        io_dis_uops_0_0_bits_prs1_busy,
  output        io_dis_uops_0_0_bits_prs2_busy,
  output        io_dis_uops_0_0_bits_prs3_busy,
  output        io_dis_uops_0_0_bits_ppred_busy,
  output [5:0]  io_dis_uops_0_0_bits_stale_pdst,
  output        io_dis_uops_0_0_bits_exception,
  output [63:0] io_dis_uops_0_0_bits_exc_cause,
  output        io_dis_uops_0_0_bits_bypassable,
  output [4:0]  io_dis_uops_0_0_bits_mem_cmd,
  output [1:0]  io_dis_uops_0_0_bits_mem_size,
  output        io_dis_uops_0_0_bits_mem_signed,
  output        io_dis_uops_0_0_bits_is_fence,
  output        io_dis_uops_0_0_bits_is_fencei,
  output        io_dis_uops_0_0_bits_is_amo,
  output        io_dis_uops_0_0_bits_uses_ldq,
  output        io_dis_uops_0_0_bits_uses_stq,
  output        io_dis_uops_0_0_bits_is_sys_pc2epc,
  output        io_dis_uops_0_0_bits_is_unique,
  output        io_dis_uops_0_0_bits_flush_on_commit,
  output        io_dis_uops_0_0_bits_ldst_is_rs1,
  output [5:0]  io_dis_uops_0_0_bits_ldst,
  output [5:0]  io_dis_uops_0_0_bits_lrs1,
  output [5:0]  io_dis_uops_0_0_bits_lrs2,
  output [5:0]  io_dis_uops_0_0_bits_lrs3,
  output        io_dis_uops_0_0_bits_ldst_val,
  output [1:0]  io_dis_uops_0_0_bits_dst_rtype,
  output [1:0]  io_dis_uops_0_0_bits_lrs1_rtype,
  output [1:0]  io_dis_uops_0_0_bits_lrs2_rtype,
  output        io_dis_uops_0_0_bits_frs3_en,
  output        io_dis_uops_0_0_bits_fp_val,
  output        io_dis_uops_0_0_bits_fp_single,
  output        io_dis_uops_0_0_bits_xcpt_pf_if,
  output        io_dis_uops_0_0_bits_xcpt_ae_if,
  output        io_dis_uops_0_0_bits_xcpt_ma_if,
  output        io_dis_uops_0_0_bits_bp_debug_if,
  output        io_dis_uops_0_0_bits_bp_xcpt_if,
  output [1:0]  io_dis_uops_0_0_bits_debug_fsrc,
  output [1:0]  io_dis_uops_0_0_bits_debug_tsrc
);
  wire [2:0] _T_5 = io_ren_uops_0_bits_iq_type & 3'h2; // @[dispatch.scala 58:75]
  wire [2:0] _T_8 = io_ren_uops_0_bits_iq_type & 3'h1; // @[dispatch.scala 58:75]
  wire [2:0] _T_11 = io_ren_uops_0_bits_iq_type & 3'h4; // @[dispatch.scala 58:75]
  assign io_ren_uops_0_ready = io_dis_uops_0_0_ready & io_dis_uops_1_0_ready & io_dis_uops_2_0_ready; // @[dispatch.scala 47:79]
  assign io_dis_uops_2_0_valid = io_ren_uops_0_valid & _T_11 != 3'h0; // @[dispatch.scala 58:42]
  assign io_dis_uops_2_0_bits_uopc = io_ren_uops_0_bits_uopc; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_inst = io_ren_uops_0_bits_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_debug_inst = io_ren_uops_0_bits_debug_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_rvc = io_ren_uops_0_bits_is_rvc; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_debug_pc = io_ren_uops_0_bits_debug_pc; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_iq_type = io_ren_uops_0_bits_iq_type; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_fu_code = io_ren_uops_0_bits_fu_code; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_br_type = io_ren_uops_0_bits_ctrl_br_type; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_op1_sel = io_ren_uops_0_bits_ctrl_op1_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_op2_sel = io_ren_uops_0_bits_ctrl_op2_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_imm_sel = io_ren_uops_0_bits_ctrl_imm_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_op_fcn = io_ren_uops_0_bits_ctrl_op_fcn; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_fcn_dw = io_ren_uops_0_bits_ctrl_fcn_dw; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_csr_cmd = io_ren_uops_0_bits_ctrl_csr_cmd; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_is_load = io_ren_uops_0_bits_ctrl_is_load; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_is_sta = io_ren_uops_0_bits_ctrl_is_sta; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ctrl_is_std = io_ren_uops_0_bits_ctrl_is_std; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_iw_state = io_ren_uops_0_bits_iw_state; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_iw_p1_poisoned = io_ren_uops_0_bits_iw_p1_poisoned; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_iw_p2_poisoned = io_ren_uops_0_bits_iw_p2_poisoned; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_br = io_ren_uops_0_bits_is_br; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_jalr = io_ren_uops_0_bits_is_jalr; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_jal = io_ren_uops_0_bits_is_jal; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_sfb = io_ren_uops_0_bits_is_sfb; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_br_mask = io_ren_uops_0_bits_br_mask; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_br_tag = io_ren_uops_0_bits_br_tag; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ftq_idx = io_ren_uops_0_bits_ftq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_edge_inst = io_ren_uops_0_bits_edge_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_pc_lob = io_ren_uops_0_bits_pc_lob; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_taken = io_ren_uops_0_bits_taken; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_imm_packed = io_ren_uops_0_bits_imm_packed; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_csr_addr = io_ren_uops_0_bits_csr_addr; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_rob_idx = io_ren_uops_0_bits_rob_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ldq_idx = io_ren_uops_0_bits_ldq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_stq_idx = io_ren_uops_0_bits_stq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_rxq_idx = io_ren_uops_0_bits_rxq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_pdst = io_ren_uops_0_bits_pdst; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_prs1 = io_ren_uops_0_bits_prs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_prs2 = io_ren_uops_0_bits_prs2; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_prs3 = io_ren_uops_0_bits_prs3; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ppred = io_ren_uops_0_bits_ppred; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_prs1_busy = io_ren_uops_0_bits_prs1_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_prs2_busy = io_ren_uops_0_bits_prs2_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_prs3_busy = io_ren_uops_0_bits_prs3_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ppred_busy = io_ren_uops_0_bits_ppred_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_stale_pdst = io_ren_uops_0_bits_stale_pdst; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_exception = io_ren_uops_0_bits_exception; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_exc_cause = io_ren_uops_0_bits_exc_cause; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_bypassable = io_ren_uops_0_bits_bypassable; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_mem_cmd = io_ren_uops_0_bits_mem_cmd; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_mem_size = io_ren_uops_0_bits_mem_size; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_mem_signed = io_ren_uops_0_bits_mem_signed; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_fence = io_ren_uops_0_bits_is_fence; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_fencei = io_ren_uops_0_bits_is_fencei; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_amo = io_ren_uops_0_bits_is_amo; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_uses_ldq = io_ren_uops_0_bits_uses_ldq; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_uses_stq = io_ren_uops_0_bits_uses_stq; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_sys_pc2epc = io_ren_uops_0_bits_is_sys_pc2epc; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_is_unique = io_ren_uops_0_bits_is_unique; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_flush_on_commit = io_ren_uops_0_bits_flush_on_commit; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ldst_is_rs1 = io_ren_uops_0_bits_ldst_is_rs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ldst = io_ren_uops_0_bits_ldst; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_lrs1 = io_ren_uops_0_bits_lrs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_lrs2 = io_ren_uops_0_bits_lrs2; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_lrs3 = io_ren_uops_0_bits_lrs3; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_ldst_val = io_ren_uops_0_bits_ldst_val; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_dst_rtype = io_ren_uops_0_bits_dst_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_lrs1_rtype = io_ren_uops_0_bits_lrs1_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_lrs2_rtype = io_ren_uops_0_bits_lrs2_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_frs3_en = io_ren_uops_0_bits_frs3_en; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_fp_val = io_ren_uops_0_bits_fp_val; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_fp_single = io_ren_uops_0_bits_fp_single; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_xcpt_pf_if = io_ren_uops_0_bits_xcpt_pf_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_xcpt_ae_if = io_ren_uops_0_bits_xcpt_ae_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_xcpt_ma_if = io_ren_uops_0_bits_xcpt_ma_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_bp_debug_if = io_ren_uops_0_bits_bp_debug_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_bp_xcpt_if = io_ren_uops_0_bits_bp_xcpt_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_debug_fsrc = io_ren_uops_0_bits_debug_fsrc; // @[dispatch.scala 59:18]
  assign io_dis_uops_2_0_bits_debug_tsrc = io_ren_uops_0_bits_debug_tsrc; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_valid = io_ren_uops_0_valid & _T_8 != 3'h0; // @[dispatch.scala 58:42]
  assign io_dis_uops_1_0_bits_uopc = io_ren_uops_0_bits_uopc; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_inst = io_ren_uops_0_bits_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_debug_inst = io_ren_uops_0_bits_debug_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_rvc = io_ren_uops_0_bits_is_rvc; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_debug_pc = io_ren_uops_0_bits_debug_pc; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_iq_type = io_ren_uops_0_bits_iq_type; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_fu_code = io_ren_uops_0_bits_fu_code; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_br_type = io_ren_uops_0_bits_ctrl_br_type; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_op1_sel = io_ren_uops_0_bits_ctrl_op1_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_op2_sel = io_ren_uops_0_bits_ctrl_op2_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_imm_sel = io_ren_uops_0_bits_ctrl_imm_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_op_fcn = io_ren_uops_0_bits_ctrl_op_fcn; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_fcn_dw = io_ren_uops_0_bits_ctrl_fcn_dw; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_csr_cmd = io_ren_uops_0_bits_ctrl_csr_cmd; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_is_load = io_ren_uops_0_bits_ctrl_is_load; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_is_sta = io_ren_uops_0_bits_ctrl_is_sta; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ctrl_is_std = io_ren_uops_0_bits_ctrl_is_std; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_iw_state = io_ren_uops_0_bits_iw_state; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_iw_p1_poisoned = io_ren_uops_0_bits_iw_p1_poisoned; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_iw_p2_poisoned = io_ren_uops_0_bits_iw_p2_poisoned; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_br = io_ren_uops_0_bits_is_br; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_jalr = io_ren_uops_0_bits_is_jalr; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_jal = io_ren_uops_0_bits_is_jal; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_sfb = io_ren_uops_0_bits_is_sfb; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_br_mask = io_ren_uops_0_bits_br_mask; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_br_tag = io_ren_uops_0_bits_br_tag; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ftq_idx = io_ren_uops_0_bits_ftq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_edge_inst = io_ren_uops_0_bits_edge_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_pc_lob = io_ren_uops_0_bits_pc_lob; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_taken = io_ren_uops_0_bits_taken; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_imm_packed = io_ren_uops_0_bits_imm_packed; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_csr_addr = io_ren_uops_0_bits_csr_addr; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_rob_idx = io_ren_uops_0_bits_rob_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ldq_idx = io_ren_uops_0_bits_ldq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_stq_idx = io_ren_uops_0_bits_stq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_rxq_idx = io_ren_uops_0_bits_rxq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_pdst = io_ren_uops_0_bits_pdst; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_prs1 = io_ren_uops_0_bits_prs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_prs2 = io_ren_uops_0_bits_prs2; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_prs3 = io_ren_uops_0_bits_prs3; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ppred = io_ren_uops_0_bits_ppred; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_prs1_busy = io_ren_uops_0_bits_prs1_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_prs2_busy = io_ren_uops_0_bits_prs2_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_prs3_busy = io_ren_uops_0_bits_prs3_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ppred_busy = io_ren_uops_0_bits_ppred_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_stale_pdst = io_ren_uops_0_bits_stale_pdst; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_exception = io_ren_uops_0_bits_exception; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_exc_cause = io_ren_uops_0_bits_exc_cause; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_bypassable = io_ren_uops_0_bits_bypassable; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_mem_cmd = io_ren_uops_0_bits_mem_cmd; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_mem_size = io_ren_uops_0_bits_mem_size; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_mem_signed = io_ren_uops_0_bits_mem_signed; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_fence = io_ren_uops_0_bits_is_fence; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_fencei = io_ren_uops_0_bits_is_fencei; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_amo = io_ren_uops_0_bits_is_amo; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_uses_ldq = io_ren_uops_0_bits_uses_ldq; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_uses_stq = io_ren_uops_0_bits_uses_stq; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_sys_pc2epc = io_ren_uops_0_bits_is_sys_pc2epc; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_is_unique = io_ren_uops_0_bits_is_unique; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_flush_on_commit = io_ren_uops_0_bits_flush_on_commit; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ldst_is_rs1 = io_ren_uops_0_bits_ldst_is_rs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ldst = io_ren_uops_0_bits_ldst; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_lrs1 = io_ren_uops_0_bits_lrs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_lrs2 = io_ren_uops_0_bits_lrs2; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_lrs3 = io_ren_uops_0_bits_lrs3; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_ldst_val = io_ren_uops_0_bits_ldst_val; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_dst_rtype = io_ren_uops_0_bits_dst_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_lrs1_rtype = io_ren_uops_0_bits_lrs1_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_lrs2_rtype = io_ren_uops_0_bits_lrs2_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_frs3_en = io_ren_uops_0_bits_frs3_en; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_fp_val = io_ren_uops_0_bits_fp_val; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_fp_single = io_ren_uops_0_bits_fp_single; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_xcpt_pf_if = io_ren_uops_0_bits_xcpt_pf_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_xcpt_ae_if = io_ren_uops_0_bits_xcpt_ae_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_xcpt_ma_if = io_ren_uops_0_bits_xcpt_ma_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_bp_debug_if = io_ren_uops_0_bits_bp_debug_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_bp_xcpt_if = io_ren_uops_0_bits_bp_xcpt_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_debug_fsrc = io_ren_uops_0_bits_debug_fsrc; // @[dispatch.scala 59:18]
  assign io_dis_uops_1_0_bits_debug_tsrc = io_ren_uops_0_bits_debug_tsrc; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_valid = io_ren_uops_0_valid & _T_5 != 3'h0; // @[dispatch.scala 58:42]
  assign io_dis_uops_0_0_bits_uopc = io_ren_uops_0_bits_uopc; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_inst = io_ren_uops_0_bits_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_debug_inst = io_ren_uops_0_bits_debug_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_rvc = io_ren_uops_0_bits_is_rvc; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_debug_pc = io_ren_uops_0_bits_debug_pc; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_iq_type = io_ren_uops_0_bits_iq_type; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_fu_code = io_ren_uops_0_bits_fu_code; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_br_type = io_ren_uops_0_bits_ctrl_br_type; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_op1_sel = io_ren_uops_0_bits_ctrl_op1_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_op2_sel = io_ren_uops_0_bits_ctrl_op2_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_imm_sel = io_ren_uops_0_bits_ctrl_imm_sel; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_op_fcn = io_ren_uops_0_bits_ctrl_op_fcn; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_fcn_dw = io_ren_uops_0_bits_ctrl_fcn_dw; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_csr_cmd = io_ren_uops_0_bits_ctrl_csr_cmd; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_is_load = io_ren_uops_0_bits_ctrl_is_load; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_is_sta = io_ren_uops_0_bits_ctrl_is_sta; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ctrl_is_std = io_ren_uops_0_bits_ctrl_is_std; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_iw_state = io_ren_uops_0_bits_iw_state; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_iw_p1_poisoned = io_ren_uops_0_bits_iw_p1_poisoned; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_iw_p2_poisoned = io_ren_uops_0_bits_iw_p2_poisoned; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_br = io_ren_uops_0_bits_is_br; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_jalr = io_ren_uops_0_bits_is_jalr; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_jal = io_ren_uops_0_bits_is_jal; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_sfb = io_ren_uops_0_bits_is_sfb; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_br_mask = io_ren_uops_0_bits_br_mask; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_br_tag = io_ren_uops_0_bits_br_tag; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ftq_idx = io_ren_uops_0_bits_ftq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_edge_inst = io_ren_uops_0_bits_edge_inst; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_pc_lob = io_ren_uops_0_bits_pc_lob; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_taken = io_ren_uops_0_bits_taken; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_imm_packed = io_ren_uops_0_bits_imm_packed; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_csr_addr = io_ren_uops_0_bits_csr_addr; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_rob_idx = io_ren_uops_0_bits_rob_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ldq_idx = io_ren_uops_0_bits_ldq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_stq_idx = io_ren_uops_0_bits_stq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_rxq_idx = io_ren_uops_0_bits_rxq_idx; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_pdst = io_ren_uops_0_bits_pdst; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_prs1 = io_ren_uops_0_bits_prs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_prs2 = io_ren_uops_0_bits_prs2; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_prs3 = io_ren_uops_0_bits_prs3; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ppred = io_ren_uops_0_bits_ppred; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_prs1_busy = io_ren_uops_0_bits_prs1_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_prs2_busy = io_ren_uops_0_bits_prs2_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_prs3_busy = io_ren_uops_0_bits_prs3_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ppred_busy = io_ren_uops_0_bits_ppred_busy; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_stale_pdst = io_ren_uops_0_bits_stale_pdst; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_exception = io_ren_uops_0_bits_exception; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_exc_cause = io_ren_uops_0_bits_exc_cause; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_bypassable = io_ren_uops_0_bits_bypassable; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_mem_cmd = io_ren_uops_0_bits_mem_cmd; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_mem_size = io_ren_uops_0_bits_mem_size; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_mem_signed = io_ren_uops_0_bits_mem_signed; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_fence = io_ren_uops_0_bits_is_fence; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_fencei = io_ren_uops_0_bits_is_fencei; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_amo = io_ren_uops_0_bits_is_amo; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_uses_ldq = io_ren_uops_0_bits_uses_ldq; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_uses_stq = io_ren_uops_0_bits_uses_stq; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_sys_pc2epc = io_ren_uops_0_bits_is_sys_pc2epc; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_is_unique = io_ren_uops_0_bits_is_unique; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_flush_on_commit = io_ren_uops_0_bits_flush_on_commit; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ldst_is_rs1 = io_ren_uops_0_bits_ldst_is_rs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ldst = io_ren_uops_0_bits_ldst; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_lrs1 = io_ren_uops_0_bits_lrs1; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_lrs2 = io_ren_uops_0_bits_lrs2; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_lrs3 = io_ren_uops_0_bits_lrs3; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_ldst_val = io_ren_uops_0_bits_ldst_val; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_dst_rtype = io_ren_uops_0_bits_dst_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_lrs1_rtype = io_ren_uops_0_bits_lrs1_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_lrs2_rtype = io_ren_uops_0_bits_lrs2_rtype; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_frs3_en = io_ren_uops_0_bits_frs3_en; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_fp_val = io_ren_uops_0_bits_fp_val; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_fp_single = io_ren_uops_0_bits_fp_single; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_xcpt_pf_if = io_ren_uops_0_bits_xcpt_pf_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_xcpt_ae_if = io_ren_uops_0_bits_xcpt_ae_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_xcpt_ma_if = io_ren_uops_0_bits_xcpt_ma_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_bp_debug_if = io_ren_uops_0_bits_bp_debug_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_bp_xcpt_if = io_ren_uops_0_bits_bp_xcpt_if; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_debug_fsrc = io_ren_uops_0_bits_debug_fsrc; // @[dispatch.scala 59:18]
  assign io_dis_uops_0_0_bits_debug_tsrc = io_ren_uops_0_bits_debug_tsrc; // @[dispatch.scala 59:18]
endmodule
