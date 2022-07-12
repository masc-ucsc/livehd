module NullPrefetcher(
  input         clock,
  input         reset,
  input         io_mshr_avail,
  input         io_req_val,
  input  [39:0] io_req_addr,
  input  [1:0]  io_req_coh_state,
  input         io_prefetch_ready,
  output        io_prefetch_valid,
  output [6:0]  io_prefetch_bits_uop_uopc,
  output [31:0] io_prefetch_bits_uop_inst,
  output [31:0] io_prefetch_bits_uop_debug_inst,
  output        io_prefetch_bits_uop_is_rvc,
  output [39:0] io_prefetch_bits_uop_debug_pc,
  output [2:0]  io_prefetch_bits_uop_iq_type,
  output [9:0]  io_prefetch_bits_uop_fu_code,
  output [3:0]  io_prefetch_bits_uop_ctrl_br_type,
  output [1:0]  io_prefetch_bits_uop_ctrl_op1_sel,
  output [2:0]  io_prefetch_bits_uop_ctrl_op2_sel,
  output [2:0]  io_prefetch_bits_uop_ctrl_imm_sel,
  output [3:0]  io_prefetch_bits_uop_ctrl_op_fcn,
  output        io_prefetch_bits_uop_ctrl_fcn_dw,
  output [2:0]  io_prefetch_bits_uop_ctrl_csr_cmd,
  output        io_prefetch_bits_uop_ctrl_is_load,
  output        io_prefetch_bits_uop_ctrl_is_sta,
  output        io_prefetch_bits_uop_ctrl_is_std,
  output [1:0]  io_prefetch_bits_uop_iw_state,
  output        io_prefetch_bits_uop_iw_p1_poisoned,
  output        io_prefetch_bits_uop_iw_p2_poisoned,
  output        io_prefetch_bits_uop_is_br,
  output        io_prefetch_bits_uop_is_jalr,
  output        io_prefetch_bits_uop_is_jal,
  output        io_prefetch_bits_uop_is_sfb,
  output [7:0]  io_prefetch_bits_uop_br_mask,
  output [2:0]  io_prefetch_bits_uop_br_tag,
  output [3:0]  io_prefetch_bits_uop_ftq_idx,
  output        io_prefetch_bits_uop_edge_inst,
  output [5:0]  io_prefetch_bits_uop_pc_lob,
  output        io_prefetch_bits_uop_taken,
  output [19:0] io_prefetch_bits_uop_imm_packed,
  output [11:0] io_prefetch_bits_uop_csr_addr,
  output [4:0]  io_prefetch_bits_uop_rob_idx,
  output [2:0]  io_prefetch_bits_uop_ldq_idx,
  output [2:0]  io_prefetch_bits_uop_stq_idx,
  output [1:0]  io_prefetch_bits_uop_rxq_idx,
  output [5:0]  io_prefetch_bits_uop_pdst,
  output [5:0]  io_prefetch_bits_uop_prs1,
  output [5:0]  io_prefetch_bits_uop_prs2,
  output [5:0]  io_prefetch_bits_uop_prs3,
  output [3:0]  io_prefetch_bits_uop_ppred,
  output        io_prefetch_bits_uop_prs1_busy,
  output        io_prefetch_bits_uop_prs2_busy,
  output        io_prefetch_bits_uop_prs3_busy,
  output        io_prefetch_bits_uop_ppred_busy,
  output [5:0]  io_prefetch_bits_uop_stale_pdst,
  output        io_prefetch_bits_uop_exception,
  output [63:0] io_prefetch_bits_uop_exc_cause,
  output        io_prefetch_bits_uop_bypassable,
  output [4:0]  io_prefetch_bits_uop_mem_cmd,
  output [1:0]  io_prefetch_bits_uop_mem_size,
  output        io_prefetch_bits_uop_mem_signed,
  output        io_prefetch_bits_uop_is_fence,
  output        io_prefetch_bits_uop_is_fencei,
  output        io_prefetch_bits_uop_is_amo,
  output        io_prefetch_bits_uop_uses_ldq,
  output        io_prefetch_bits_uop_uses_stq,
  output        io_prefetch_bits_uop_is_sys_pc2epc,
  output        io_prefetch_bits_uop_is_unique,
  output        io_prefetch_bits_uop_flush_on_commit,
  output        io_prefetch_bits_uop_ldst_is_rs1,
  output [5:0]  io_prefetch_bits_uop_ldst,
  output [5:0]  io_prefetch_bits_uop_lrs1,
  output [5:0]  io_prefetch_bits_uop_lrs2,
  output [5:0]  io_prefetch_bits_uop_lrs3,
  output        io_prefetch_bits_uop_ldst_val,
  output [1:0]  io_prefetch_bits_uop_dst_rtype,
  output [1:0]  io_prefetch_bits_uop_lrs1_rtype,
  output [1:0]  io_prefetch_bits_uop_lrs2_rtype,
  output        io_prefetch_bits_uop_frs3_en,
  output        io_prefetch_bits_uop_fp_val,
  output        io_prefetch_bits_uop_fp_single,
  output        io_prefetch_bits_uop_xcpt_pf_if,
  output        io_prefetch_bits_uop_xcpt_ae_if,
  output        io_prefetch_bits_uop_xcpt_ma_if,
  output        io_prefetch_bits_uop_bp_debug_if,
  output        io_prefetch_bits_uop_bp_xcpt_if,
  output [1:0]  io_prefetch_bits_uop_debug_fsrc,
  output [1:0]  io_prefetch_bits_uop_debug_tsrc,
  output [39:0] io_prefetch_bits_addr,
  output [63:0] io_prefetch_bits_data,
  output        io_prefetch_bits_is_hella
);
  assign io_prefetch_valid = 1'h0; // @[prefetcher.scala 41:21]
  assign io_prefetch_bits_uop_uopc = 7'h0;
  assign io_prefetch_bits_uop_inst = 32'h0;
  assign io_prefetch_bits_uop_debug_inst = 32'h0;
  assign io_prefetch_bits_uop_is_rvc = 1'h0;
  assign io_prefetch_bits_uop_debug_pc = 40'h0;
  assign io_prefetch_bits_uop_iq_type = 3'h0;
  assign io_prefetch_bits_uop_fu_code = 10'h0;
  assign io_prefetch_bits_uop_ctrl_br_type = 4'h0;
  assign io_prefetch_bits_uop_ctrl_op1_sel = 2'h0;
  assign io_prefetch_bits_uop_ctrl_op2_sel = 3'h0;
  assign io_prefetch_bits_uop_ctrl_imm_sel = 3'h0;
  assign io_prefetch_bits_uop_ctrl_op_fcn = 4'h0;
  assign io_prefetch_bits_uop_ctrl_fcn_dw = 1'h0;
  assign io_prefetch_bits_uop_ctrl_csr_cmd = 3'h0;
  assign io_prefetch_bits_uop_ctrl_is_load = 1'h0;
  assign io_prefetch_bits_uop_ctrl_is_sta = 1'h0;
  assign io_prefetch_bits_uop_ctrl_is_std = 1'h0;
  assign io_prefetch_bits_uop_iw_state = 2'h0;
  assign io_prefetch_bits_uop_iw_p1_poisoned = 1'h0;
  assign io_prefetch_bits_uop_iw_p2_poisoned = 1'h0;
  assign io_prefetch_bits_uop_is_br = 1'h0;
  assign io_prefetch_bits_uop_is_jalr = 1'h0;
  assign io_prefetch_bits_uop_is_jal = 1'h0;
  assign io_prefetch_bits_uop_is_sfb = 1'h0;
  assign io_prefetch_bits_uop_br_mask = 8'h0;
  assign io_prefetch_bits_uop_br_tag = 3'h0;
  assign io_prefetch_bits_uop_ftq_idx = 4'h0;
  assign io_prefetch_bits_uop_edge_inst = 1'h0;
  assign io_prefetch_bits_uop_pc_lob = 6'h0;
  assign io_prefetch_bits_uop_taken = 1'h0;
  assign io_prefetch_bits_uop_imm_packed = 20'h0;
  assign io_prefetch_bits_uop_csr_addr = 12'h0;
  assign io_prefetch_bits_uop_rob_idx = 5'h0;
  assign io_prefetch_bits_uop_ldq_idx = 3'h0;
  assign io_prefetch_bits_uop_stq_idx = 3'h0;
  assign io_prefetch_bits_uop_rxq_idx = 2'h0;
  assign io_prefetch_bits_uop_pdst = 6'h0;
  assign io_prefetch_bits_uop_prs1 = 6'h0;
  assign io_prefetch_bits_uop_prs2 = 6'h0;
  assign io_prefetch_bits_uop_prs3 = 6'h0;
  assign io_prefetch_bits_uop_ppred = 4'h0;
  assign io_prefetch_bits_uop_prs1_busy = 1'h0;
  assign io_prefetch_bits_uop_prs2_busy = 1'h0;
  assign io_prefetch_bits_uop_prs3_busy = 1'h0;
  assign io_prefetch_bits_uop_ppred_busy = 1'h0;
  assign io_prefetch_bits_uop_stale_pdst = 6'h0;
  assign io_prefetch_bits_uop_exception = 1'h0;
  assign io_prefetch_bits_uop_exc_cause = 64'h0;
  assign io_prefetch_bits_uop_bypassable = 1'h0;
  assign io_prefetch_bits_uop_mem_cmd = 5'h0;
  assign io_prefetch_bits_uop_mem_size = 2'h0;
  assign io_prefetch_bits_uop_mem_signed = 1'h0;
  assign io_prefetch_bits_uop_is_fence = 1'h0;
  assign io_prefetch_bits_uop_is_fencei = 1'h0;
  assign io_prefetch_bits_uop_is_amo = 1'h0;
  assign io_prefetch_bits_uop_uses_ldq = 1'h0;
  assign io_prefetch_bits_uop_uses_stq = 1'h0;
  assign io_prefetch_bits_uop_is_sys_pc2epc = 1'h0;
  assign io_prefetch_bits_uop_is_unique = 1'h0;
  assign io_prefetch_bits_uop_flush_on_commit = 1'h0;
  assign io_prefetch_bits_uop_ldst_is_rs1 = 1'h0;
  assign io_prefetch_bits_uop_ldst = 6'h0;
  assign io_prefetch_bits_uop_lrs1 = 6'h0;
  assign io_prefetch_bits_uop_lrs2 = 6'h0;
  assign io_prefetch_bits_uop_lrs3 = 6'h0;
  assign io_prefetch_bits_uop_ldst_val = 1'h0;
  assign io_prefetch_bits_uop_dst_rtype = 2'h0;
  assign io_prefetch_bits_uop_lrs1_rtype = 2'h0;
  assign io_prefetch_bits_uop_lrs2_rtype = 2'h0;
  assign io_prefetch_bits_uop_frs3_en = 1'h0;
  assign io_prefetch_bits_uop_fp_val = 1'h0;
  assign io_prefetch_bits_uop_fp_single = 1'h0;
  assign io_prefetch_bits_uop_xcpt_pf_if = 1'h0;
  assign io_prefetch_bits_uop_xcpt_ae_if = 1'h0;
  assign io_prefetch_bits_uop_xcpt_ma_if = 1'h0;
  assign io_prefetch_bits_uop_bp_debug_if = 1'h0;
  assign io_prefetch_bits_uop_bp_xcpt_if = 1'h0;
  assign io_prefetch_bits_uop_debug_fsrc = 2'h0;
  assign io_prefetch_bits_uop_debug_tsrc = 2'h0;
  assign io_prefetch_bits_addr = 40'h0;
  assign io_prefetch_bits_data = 64'h0;
  assign io_prefetch_bits_is_hella = 1'h0;
endmodule
