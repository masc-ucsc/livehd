module ALUExeUnit(
  input         clock,
  input         reset,
  output [9:0]  io_fu_types,
  input         io_req_valid,
  input         io_req_bits_uop_valid,
  input  [1:0]  io_req_bits_uop_iw_state,
  input  [8:0]  io_req_bits_uop_uopc,
  input  [31:0] io_req_bits_uop_inst,
  input  [39:0] io_req_bits_uop_pc,
  input  [1:0]  io_req_bits_uop_iqtype,
  input  [9:0]  io_req_bits_uop_fu_code,
  input  [3:0]  io_req_bits_uop_ctrl_br_type,
  input  [1:0]  io_req_bits_uop_ctrl_op1_sel,
  input  [2:0]  io_req_bits_uop_ctrl_op2_sel,
  input  [2:0]  io_req_bits_uop_ctrl_imm_sel,
  input  [3:0]  io_req_bits_uop_ctrl_op_fcn,
  input         io_req_bits_uop_ctrl_fcn_dw,
  input         io_req_bits_uop_ctrl_rf_wen,
  input  [2:0]  io_req_bits_uop_ctrl_csr_cmd,
  input         io_req_bits_uop_ctrl_is_load,
  input         io_req_bits_uop_ctrl_is_sta,
  input         io_req_bits_uop_ctrl_is_std,
  input         io_req_bits_uop_allocate_brtag,
  input         io_req_bits_uop_is_br_or_jmp,
  input         io_req_bits_uop_is_jump,
  input         io_req_bits_uop_is_jal,
  input         io_req_bits_uop_is_ret,
  input         io_req_bits_uop_is_call,
  input  [7:0]  io_req_bits_uop_br_mask,
  input  [2:0]  io_req_bits_uop_br_tag,
  input         io_req_bits_uop_br_prediction_btb_blame,
  input         io_req_bits_uop_br_prediction_btb_hit,
  input         io_req_bits_uop_br_prediction_btb_taken,
  input         io_req_bits_uop_br_prediction_bpd_blame,
  input         io_req_bits_uop_br_prediction_bpd_hit,
  input         io_req_bits_uop_br_prediction_bpd_taken,
  input  [1:0]  io_req_bits_uop_br_prediction_bim_resp_value,
  input  [5:0]  io_req_bits_uop_br_prediction_bim_resp_entry_idx,
  input  [1:0]  io_req_bits_uop_br_prediction_bim_resp_way_idx,
  input  [1:0]  io_req_bits_uop_br_prediction_bpd_resp_takens,
  input  [14:0] io_req_bits_uop_br_prediction_bpd_resp_history,
  input  [14:0] io_req_bits_uop_br_prediction_bpd_resp_history_u,
  input  [7:0]  io_req_bits_uop_br_prediction_bpd_resp_history_ptr,
  input  [14:0] io_req_bits_uop_br_prediction_bpd_resp_info,
  input         io_req_bits_uop_stat_brjmp_mispredicted,
  input         io_req_bits_uop_stat_btb_made_pred,
  input         io_req_bits_uop_stat_btb_mispredicted,
  input         io_req_bits_uop_stat_bpd_made_pred,
  input         io_req_bits_uop_stat_bpd_mispredicted,
  input  [2:0]  io_req_bits_uop_fetch_pc_lob,
  input  [19:0] io_req_bits_uop_imm_packed,
  input  [11:0] io_req_bits_uop_csr_addr,
  input  [6:0]  io_req_bits_uop_rob_idx,
  input  [3:0]  io_req_bits_uop_ldq_idx,
  input  [3:0]  io_req_bits_uop_stq_idx,
  input  [5:0]  io_req_bits_uop_brob_idx,
  input  [6:0]  io_req_bits_uop_pdst,
  input  [6:0]  io_req_bits_uop_pop1,
  input  [6:0]  io_req_bits_uop_pop2,
  input  [6:0]  io_req_bits_uop_pop3,
  input         io_req_bits_uop_prs1_busy,
  input         io_req_bits_uop_prs2_busy,
  input         io_req_bits_uop_prs3_busy,
  input  [6:0]  io_req_bits_uop_stale_pdst,
  input         io_req_bits_uop_exception,
  input  [63:0] io_req_bits_uop_exc_cause,
  input         io_req_bits_uop_bypassable,
  input  [4:0]  io_req_bits_uop_mem_cmd,
  input  [2:0]  io_req_bits_uop_mem_typ,
  input         io_req_bits_uop_is_fence,
  input         io_req_bits_uop_is_fencei,
  input         io_req_bits_uop_is_store,
  input         io_req_bits_uop_is_amo,
  input         io_req_bits_uop_is_load,
  input         io_req_bits_uop_is_sys_pc2epc,
  input         io_req_bits_uop_is_unique,
  input         io_req_bits_uop_flush_on_commit,
  input  [5:0]  io_req_bits_uop_ldst,
  input  [5:0]  io_req_bits_uop_lrs1,
  input  [5:0]  io_req_bits_uop_lrs2,
  input  [5:0]  io_req_bits_uop_lrs3,
  input         io_req_bits_uop_ldst_val,
  input  [1:0]  io_req_bits_uop_dst_rtype,
  input  [1:0]  io_req_bits_uop_lrs1_rtype,
  input  [1:0]  io_req_bits_uop_lrs2_rtype,
  input         io_req_bits_uop_frs3_en,
  input         io_req_bits_uop_fp_val,
  input         io_req_bits_uop_fp_single,
  input         io_req_bits_uop_xcpt_pf_if,
  input         io_req_bits_uop_xcpt_ae_if,
  input         io_req_bits_uop_replay_if,
  input         io_req_bits_uop_xcpt_ma_if,
  input  [63:0] io_req_bits_uop_debug_wdata,
  input  [31:0] io_req_bits_uop_debug_events_fetch_seq,
  input  [63:0] io_req_bits_rs1_data,
  input  [63:0] io_req_bits_rs2_data,
  input         io_req_bits_kill,
  output        io_resp_0_valid,
  output        io_resp_0_bits_uop_ctrl_rf_wen,
  output [2:0]  io_resp_0_bits_uop_ctrl_csr_cmd,
  output [11:0] io_resp_0_bits_uop_csr_addr,
  output [6:0]  io_resp_0_bits_uop_rob_idx,
  output [6:0]  io_resp_0_bits_uop_pdst,
  output        io_resp_0_bits_uop_bypassable,
  output        io_resp_0_bits_uop_is_store,
  output        io_resp_0_bits_uop_is_amo,
  output [1:0]  io_resp_0_bits_uop_dst_rtype,
  output [63:0] io_resp_0_bits_data,
  output        io_bypass_valid_0,
  output        io_bypass_valid_1,
  output        io_bypass_valid_2,
  output        io_bypass_uop_0_ctrl_rf_wen,
  output [6:0]  io_bypass_uop_0_pdst,
  output [1:0]  io_bypass_uop_0_dst_rtype,
  output        io_bypass_uop_1_ctrl_rf_wen,
  output [6:0]  io_bypass_uop_1_pdst,
  output [1:0]  io_bypass_uop_1_dst_rtype,
  output        io_bypass_uop_2_ctrl_rf_wen,
  output [6:0]  io_bypass_uop_2_pdst,
  output [1:0]  io_bypass_uop_2_dst_rtype,
  output [63:0] io_bypass_data_0,
  output [63:0] io_bypass_data_1,
  output [63:0] io_bypass_data_2,
  input         io_brinfo_valid,
  input         io_brinfo_mispredict,
  input  [7:0]  io_brinfo_mask,
  output        io_br_unit_take_pc,
  output [39:0] io_br_unit_target,
  output        io_br_unit_brinfo_valid,
  output        io_br_unit_brinfo_mispredict,
  output [7:0]  io_br_unit_brinfo_mask,
  output [2:0]  io_br_unit_brinfo_tag,
  output [7:0]  io_br_unit_brinfo_exe_mask,
  output [6:0]  io_br_unit_brinfo_rob_idx,
  output [3:0]  io_br_unit_brinfo_ldq_idx,
  output [3:0]  io_br_unit_brinfo_stq_idx,
  output        io_br_unit_brinfo_is_jr,
  output        io_br_unit_btb_update_valid,
  output [38:0] io_br_unit_btb_update_bits_pc,
  output [38:0] io_br_unit_btb_update_bits_target,
  output [38:0] io_br_unit_btb_update_bits_cfi_pc,
  output [2:0]  io_br_unit_btb_update_bits_bpd_type,
  output [2:0]  io_br_unit_btb_update_bits_cfi_type,
  output        io_br_unit_bim_update_valid,
  output        io_br_unit_bim_update_bits_taken,
  output [1:0]  io_br_unit_bim_update_bits_bim_resp_value,
  output [5:0]  io_br_unit_bim_update_bits_bim_resp_entry_idx,
  output [1:0]  io_br_unit_bim_update_bits_bim_resp_way_idx,
  output        io_br_unit_bpd_update_valid,
  output [2:0]  io_br_unit_bpd_update_bits_br_pc,
  output [5:0]  io_br_unit_bpd_update_bits_brob_idx,
  output        io_br_unit_bpd_update_bits_mispredict,
  output [14:0] io_br_unit_bpd_update_bits_history,
  output [7:0]  io_br_unit_bpd_update_bits_history_ptr,
  output        io_br_unit_bpd_update_bits_bpd_predict_val,
  output        io_br_unit_bpd_update_bits_bpd_mispredict,
  output        io_br_unit_bpd_update_bits_taken,
  output        io_br_unit_bpd_update_bits_is_br,
  output        io_br_unit_bpd_update_bits_new_pc_same_packet,
  output        io_br_unit_xcpt_valid,
  output [7:0]  io_br_unit_xcpt_bits_uop_br_mask,
  output [6:0]  io_br_unit_xcpt_bits_uop_rob_idx,
  output [39:0] io_br_unit_xcpt_bits_badvaddr,
  input  [39:0] io_get_rob_pc_curr_pc,
  input  [5:0]  io_get_rob_pc_curr_brob_idx,
  input         io_get_rob_pc_next_val,
  input  [39:0] io_get_rob_pc_next_pc,
  input  [1:0]  io_get_pred_info_bim_resp_value,
  input  [5:0]  io_get_pred_info_bim_resp_entry_idx,
  input  [1:0]  io_get_pred_info_bim_resp_way_idx,
  input  [14:0] io_get_pred_info_bpd_resp_history,
  input  [7:0]  io_get_pred_info_bpd_resp_history_ptr,
  input         io_status_debug
);
  wire  muldiv_busy;
  wire  _T_1210;
  wire [9:0] _T_1221;
  wire [9:0] _T_1222;
  wire [9:0] _T_1226;
  wire [9:0] _T_1230;
  wire  alu_clock;
  wire  alu_reset;
  wire  alu_io_req_valid;
  wire  alu_io_req_bits_uop_valid;
  wire [1:0] alu_io_req_bits_uop_iw_state;
  wire [8:0] alu_io_req_bits_uop_uopc;
  wire [31:0] alu_io_req_bits_uop_inst;
  wire [39:0] alu_io_req_bits_uop_pc;
  wire [1:0] alu_io_req_bits_uop_iqtype;
  wire [9:0] alu_io_req_bits_uop_fu_code;
  wire [3:0] alu_io_req_bits_uop_ctrl_br_type;
  wire [1:0] alu_io_req_bits_uop_ctrl_op1_sel;
  wire [2:0] alu_io_req_bits_uop_ctrl_op2_sel;
  wire [2:0] alu_io_req_bits_uop_ctrl_imm_sel;
  wire [3:0] alu_io_req_bits_uop_ctrl_op_fcn;
  wire  alu_io_req_bits_uop_ctrl_fcn_dw;
  wire  alu_io_req_bits_uop_ctrl_rf_wen;
  wire [2:0] alu_io_req_bits_uop_ctrl_csr_cmd;
  wire  alu_io_req_bits_uop_ctrl_is_load;
  wire  alu_io_req_bits_uop_ctrl_is_sta;
  wire  alu_io_req_bits_uop_ctrl_is_std;
  wire  alu_io_req_bits_uop_allocate_brtag;
  wire  alu_io_req_bits_uop_is_br_or_jmp;
  wire  alu_io_req_bits_uop_is_jump;
  wire  alu_io_req_bits_uop_is_jal;
  wire  alu_io_req_bits_uop_is_ret;
  wire  alu_io_req_bits_uop_is_call;
  wire [7:0] alu_io_req_bits_uop_br_mask;
  wire [2:0] alu_io_req_bits_uop_br_tag;
  wire  alu_io_req_bits_uop_br_prediction_btb_blame;
  wire  alu_io_req_bits_uop_br_prediction_btb_hit;
  wire  alu_io_req_bits_uop_br_prediction_btb_taken;
  wire  alu_io_req_bits_uop_br_prediction_bpd_blame;
  wire  alu_io_req_bits_uop_br_prediction_bpd_hit;
  wire  alu_io_req_bits_uop_br_prediction_bpd_taken;
  wire [1:0] alu_io_req_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] alu_io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] alu_io_req_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] alu_io_req_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] alu_io_req_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] alu_io_req_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] alu_io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] alu_io_req_bits_uop_br_prediction_bpd_resp_info;
  wire  alu_io_req_bits_uop_stat_brjmp_mispredicted;
  wire  alu_io_req_bits_uop_stat_btb_made_pred;
  wire  alu_io_req_bits_uop_stat_btb_mispredicted;
  wire  alu_io_req_bits_uop_stat_bpd_made_pred;
  wire  alu_io_req_bits_uop_stat_bpd_mispredicted;
  wire [2:0] alu_io_req_bits_uop_fetch_pc_lob;
  wire [19:0] alu_io_req_bits_uop_imm_packed;
  wire [11:0] alu_io_req_bits_uop_csr_addr;
  wire [6:0] alu_io_req_bits_uop_rob_idx;
  wire [3:0] alu_io_req_bits_uop_ldq_idx;
  wire [3:0] alu_io_req_bits_uop_stq_idx;
  wire [5:0] alu_io_req_bits_uop_brob_idx;
  wire [6:0] alu_io_req_bits_uop_pdst;
  wire [6:0] alu_io_req_bits_uop_pop1;
  wire [6:0] alu_io_req_bits_uop_pop2;
  wire [6:0] alu_io_req_bits_uop_pop3;
  wire  alu_io_req_bits_uop_prs1_busy;
  wire  alu_io_req_bits_uop_prs2_busy;
  wire  alu_io_req_bits_uop_prs3_busy;
  wire [6:0] alu_io_req_bits_uop_stale_pdst;
  wire  alu_io_req_bits_uop_exception;
  wire [63:0] alu_io_req_bits_uop_exc_cause;
  wire  alu_io_req_bits_uop_bypassable;
  wire [4:0] alu_io_req_bits_uop_mem_cmd;
  wire [2:0] alu_io_req_bits_uop_mem_typ;
  wire  alu_io_req_bits_uop_is_fence;
  wire  alu_io_req_bits_uop_is_fencei;
  wire  alu_io_req_bits_uop_is_store;
  wire  alu_io_req_bits_uop_is_amo;
  wire  alu_io_req_bits_uop_is_load;
  wire  alu_io_req_bits_uop_is_sys_pc2epc;
  wire  alu_io_req_bits_uop_is_unique;
  wire  alu_io_req_bits_uop_flush_on_commit;
  wire [5:0] alu_io_req_bits_uop_ldst;
  wire [5:0] alu_io_req_bits_uop_lrs1;
  wire [5:0] alu_io_req_bits_uop_lrs2;
  wire [5:0] alu_io_req_bits_uop_lrs3;
  wire  alu_io_req_bits_uop_ldst_val;
  wire [1:0] alu_io_req_bits_uop_dst_rtype;
  wire [1:0] alu_io_req_bits_uop_lrs1_rtype;
  wire [1:0] alu_io_req_bits_uop_lrs2_rtype;
  wire  alu_io_req_bits_uop_frs3_en;
  wire  alu_io_req_bits_uop_fp_val;
  wire  alu_io_req_bits_uop_fp_single;
  wire  alu_io_req_bits_uop_xcpt_pf_if;
  wire  alu_io_req_bits_uop_xcpt_ae_if;
  wire  alu_io_req_bits_uop_replay_if;
  wire  alu_io_req_bits_uop_xcpt_ma_if;
  wire [63:0] alu_io_req_bits_uop_debug_wdata;
  wire [31:0] alu_io_req_bits_uop_debug_events_fetch_seq;
  wire [63:0] alu_io_req_bits_rs1_data;
  wire [63:0] alu_io_req_bits_rs2_data;
  wire  alu_io_req_bits_kill;
  wire  alu_io_resp_valid;
  wire  alu_io_resp_bits_uop_valid;
  wire [1:0] alu_io_resp_bits_uop_iw_state;
  wire [8:0] alu_io_resp_bits_uop_uopc;
  wire [31:0] alu_io_resp_bits_uop_inst;
  wire [39:0] alu_io_resp_bits_uop_pc;
  wire [1:0] alu_io_resp_bits_uop_iqtype;
  wire [9:0] alu_io_resp_bits_uop_fu_code;
  wire [3:0] alu_io_resp_bits_uop_ctrl_br_type;
  wire [1:0] alu_io_resp_bits_uop_ctrl_op1_sel;
  wire [2:0] alu_io_resp_bits_uop_ctrl_op2_sel;
  wire [2:0] alu_io_resp_bits_uop_ctrl_imm_sel;
  wire [3:0] alu_io_resp_bits_uop_ctrl_op_fcn;
  wire  alu_io_resp_bits_uop_ctrl_fcn_dw;
  wire  alu_io_resp_bits_uop_ctrl_rf_wen;
  wire [2:0] alu_io_resp_bits_uop_ctrl_csr_cmd;
  wire  alu_io_resp_bits_uop_ctrl_is_load;
  wire  alu_io_resp_bits_uop_ctrl_is_sta;
  wire  alu_io_resp_bits_uop_ctrl_is_std;
  wire  alu_io_resp_bits_uop_allocate_brtag;
  wire  alu_io_resp_bits_uop_is_br_or_jmp;
  wire  alu_io_resp_bits_uop_is_jump;
  wire  alu_io_resp_bits_uop_is_jal;
  wire  alu_io_resp_bits_uop_is_ret;
  wire  alu_io_resp_bits_uop_is_call;
  wire [7:0] alu_io_resp_bits_uop_br_mask;
  wire [2:0] alu_io_resp_bits_uop_br_tag;
  wire  alu_io_resp_bits_uop_br_prediction_btb_blame;
  wire  alu_io_resp_bits_uop_br_prediction_btb_hit;
  wire  alu_io_resp_bits_uop_br_prediction_btb_taken;
  wire  alu_io_resp_bits_uop_br_prediction_bpd_blame;
  wire  alu_io_resp_bits_uop_br_prediction_bpd_hit;
  wire  alu_io_resp_bits_uop_br_prediction_bpd_taken;
  wire [1:0] alu_io_resp_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] alu_io_resp_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] alu_io_resp_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] alu_io_resp_bits_uop_br_prediction_bpd_resp_info;
  wire  alu_io_resp_bits_uop_stat_brjmp_mispredicted;
  wire  alu_io_resp_bits_uop_stat_btb_made_pred;
  wire  alu_io_resp_bits_uop_stat_btb_mispredicted;
  wire  alu_io_resp_bits_uop_stat_bpd_made_pred;
  wire  alu_io_resp_bits_uop_stat_bpd_mispredicted;
  wire [2:0] alu_io_resp_bits_uop_fetch_pc_lob;
  wire [19:0] alu_io_resp_bits_uop_imm_packed;
  wire [11:0] alu_io_resp_bits_uop_csr_addr;
  wire [6:0] alu_io_resp_bits_uop_rob_idx;
  wire [3:0] alu_io_resp_bits_uop_ldq_idx;
  wire [3:0] alu_io_resp_bits_uop_stq_idx;
  wire [5:0] alu_io_resp_bits_uop_brob_idx;
  wire [6:0] alu_io_resp_bits_uop_pdst;
  wire [6:0] alu_io_resp_bits_uop_pop1;
  wire [6:0] alu_io_resp_bits_uop_pop2;
  wire [6:0] alu_io_resp_bits_uop_pop3;
  wire  alu_io_resp_bits_uop_prs1_busy;
  wire  alu_io_resp_bits_uop_prs2_busy;
  wire  alu_io_resp_bits_uop_prs3_busy;
  wire [6:0] alu_io_resp_bits_uop_stale_pdst;
  wire  alu_io_resp_bits_uop_exception;
  wire [63:0] alu_io_resp_bits_uop_exc_cause;
  wire  alu_io_resp_bits_uop_bypassable;
  wire [4:0] alu_io_resp_bits_uop_mem_cmd;
  wire [2:0] alu_io_resp_bits_uop_mem_typ;
  wire  alu_io_resp_bits_uop_is_fence;
  wire  alu_io_resp_bits_uop_is_fencei;
  wire  alu_io_resp_bits_uop_is_store;
  wire  alu_io_resp_bits_uop_is_amo;
  wire  alu_io_resp_bits_uop_is_load;
  wire  alu_io_resp_bits_uop_is_sys_pc2epc;
  wire  alu_io_resp_bits_uop_is_unique;
  wire  alu_io_resp_bits_uop_flush_on_commit;
  wire [5:0] alu_io_resp_bits_uop_ldst;
  wire [5:0] alu_io_resp_bits_uop_lrs1;
  wire [5:0] alu_io_resp_bits_uop_lrs2;
  wire [5:0] alu_io_resp_bits_uop_lrs3;
  wire  alu_io_resp_bits_uop_ldst_val;
  wire [1:0] alu_io_resp_bits_uop_dst_rtype;
  wire [1:0] alu_io_resp_bits_uop_lrs1_rtype;
  wire [1:0] alu_io_resp_bits_uop_lrs2_rtype;
  wire  alu_io_resp_bits_uop_frs3_en;
  wire  alu_io_resp_bits_uop_fp_val;
  wire  alu_io_resp_bits_uop_fp_single;
  wire  alu_io_resp_bits_uop_xcpt_pf_if;
  wire  alu_io_resp_bits_uop_xcpt_ae_if;
  wire  alu_io_resp_bits_uop_replay_if;
  wire  alu_io_resp_bits_uop_xcpt_ma_if;
  wire [63:0] alu_io_resp_bits_uop_debug_wdata;
  wire [31:0] alu_io_resp_bits_uop_debug_events_fetch_seq;
  wire [63:0] alu_io_resp_bits_data;
  wire  alu_io_brinfo_valid;
  wire  alu_io_brinfo_mispredict;
  wire [7:0] alu_io_brinfo_mask;
  wire  alu_io_bypass_valid_0;
  wire  alu_io_bypass_valid_1;
  wire  alu_io_bypass_valid_2;
  wire  alu_io_bypass_uop_0_ctrl_rf_wen;
  wire [6:0] alu_io_bypass_uop_0_pdst;
  wire [1:0] alu_io_bypass_uop_0_dst_rtype;
  wire  alu_io_bypass_uop_1_ctrl_rf_wen;
  wire [6:0] alu_io_bypass_uop_1_pdst;
  wire [1:0] alu_io_bypass_uop_1_dst_rtype;
  wire  alu_io_bypass_uop_2_ctrl_rf_wen;
  wire [6:0] alu_io_bypass_uop_2_pdst;
  wire [1:0] alu_io_bypass_uop_2_dst_rtype;
  wire [63:0] alu_io_bypass_data_0;
  wire [63:0] alu_io_bypass_data_1;
  wire [63:0] alu_io_bypass_data_2;
  wire  alu_io_br_unit_take_pc;
  wire [39:0] alu_io_br_unit_target;
  wire  alu_io_br_unit_brinfo_valid;
  wire  alu_io_br_unit_brinfo_mispredict;
  wire [7:0] alu_io_br_unit_brinfo_mask;
  wire [2:0] alu_io_br_unit_brinfo_tag;
  wire [7:0] alu_io_br_unit_brinfo_exe_mask;
  wire [6:0] alu_io_br_unit_brinfo_rob_idx;
  wire [3:0] alu_io_br_unit_brinfo_ldq_idx;
  wire [3:0] alu_io_br_unit_brinfo_stq_idx;
  wire  alu_io_br_unit_brinfo_is_jr;
  wire  alu_io_br_unit_btb_update_valid;
  wire [38:0] alu_io_br_unit_btb_update_bits_pc;
  wire [38:0] alu_io_br_unit_btb_update_bits_target;
  wire [38:0] alu_io_br_unit_btb_update_bits_cfi_pc;
  wire [2:0] alu_io_br_unit_btb_update_bits_bpd_type;
  wire [2:0] alu_io_br_unit_btb_update_bits_cfi_type;
  wire  alu_io_br_unit_bim_update_valid;
  wire  alu_io_br_unit_bim_update_bits_taken;
  wire [1:0] alu_io_br_unit_bim_update_bits_bim_resp_value;
  wire [5:0] alu_io_br_unit_bim_update_bits_bim_resp_entry_idx;
  wire [1:0] alu_io_br_unit_bim_update_bits_bim_resp_way_idx;
  wire  alu_io_br_unit_bpd_update_valid;
  wire [2:0] alu_io_br_unit_bpd_update_bits_br_pc;
  wire [5:0] alu_io_br_unit_bpd_update_bits_brob_idx;
  wire  alu_io_br_unit_bpd_update_bits_mispredict;
  wire [14:0] alu_io_br_unit_bpd_update_bits_history;
  wire [7:0] alu_io_br_unit_bpd_update_bits_history_ptr;
  wire  alu_io_br_unit_bpd_update_bits_bpd_predict_val;
  wire  alu_io_br_unit_bpd_update_bits_bpd_mispredict;
  wire  alu_io_br_unit_bpd_update_bits_taken;
  wire  alu_io_br_unit_bpd_update_bits_is_br;
  wire  alu_io_br_unit_bpd_update_bits_new_pc_same_packet;
  wire  alu_io_br_unit_xcpt_valid;
  wire [7:0] alu_io_br_unit_xcpt_bits_uop_br_mask;
  wire [6:0] alu_io_br_unit_xcpt_bits_uop_rob_idx;
  wire [39:0] alu_io_br_unit_xcpt_bits_badvaddr;
  wire [39:0] alu_io_get_rob_pc_curr_pc;
  wire [5:0] alu_io_get_rob_pc_curr_brob_idx;
  wire  alu_io_get_rob_pc_next_val;
  wire [39:0] alu_io_get_rob_pc_next_pc;
  wire [1:0] alu_io_get_pred_info_bim_resp_value;
  wire [5:0] alu_io_get_pred_info_bim_resp_entry_idx;
  wire [1:0] alu_io_get_pred_info_bim_resp_way_idx;
  wire [14:0] alu_io_get_pred_info_bpd_resp_history;
  wire [7:0] alu_io_get_pred_info_bpd_resp_history_ptr;
  wire  alu_io_status_debug;
  wire  _T_1238;
  wire  _T_1239;
  wire  _T_1240;
  wire  _T_1241;
  wire  _T_1242;
  wire  _T_1243;
  wire  fu_units_1_clock;
  wire  fu_units_1_reset;
  wire  fu_units_1_io_req_valid;
  wire  fu_units_1_io_req_bits_uop_valid;
  wire [1:0] fu_units_1_io_req_bits_uop_iw_state;
  wire [8:0] fu_units_1_io_req_bits_uop_uopc;
  wire [31:0] fu_units_1_io_req_bits_uop_inst;
  wire [39:0] fu_units_1_io_req_bits_uop_pc;
  wire [1:0] fu_units_1_io_req_bits_uop_iqtype;
  wire [9:0] fu_units_1_io_req_bits_uop_fu_code;
  wire [3:0] fu_units_1_io_req_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_1_io_req_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_1_io_req_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_1_io_req_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_1_io_req_bits_uop_ctrl_op_fcn;
  wire  fu_units_1_io_req_bits_uop_ctrl_fcn_dw;
  wire  fu_units_1_io_req_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_1_io_req_bits_uop_ctrl_csr_cmd;
  wire  fu_units_1_io_req_bits_uop_ctrl_is_load;
  wire  fu_units_1_io_req_bits_uop_ctrl_is_sta;
  wire  fu_units_1_io_req_bits_uop_ctrl_is_std;
  wire  fu_units_1_io_req_bits_uop_allocate_brtag;
  wire  fu_units_1_io_req_bits_uop_is_br_or_jmp;
  wire  fu_units_1_io_req_bits_uop_is_jump;
  wire  fu_units_1_io_req_bits_uop_is_jal;
  wire  fu_units_1_io_req_bits_uop_is_ret;
  wire  fu_units_1_io_req_bits_uop_is_call;
  wire [7:0] fu_units_1_io_req_bits_uop_br_mask;
  wire [2:0] fu_units_1_io_req_bits_uop_br_tag;
  wire  fu_units_1_io_req_bits_uop_br_prediction_btb_blame;
  wire  fu_units_1_io_req_bits_uop_br_prediction_btb_hit;
  wire  fu_units_1_io_req_bits_uop_br_prediction_btb_taken;
  wire  fu_units_1_io_req_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_1_io_req_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_1_io_req_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_1_io_req_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_1_io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_1_io_req_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_1_io_req_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_1_io_req_bits_uop_stat_btb_made_pred;
  wire  fu_units_1_io_req_bits_uop_stat_btb_mispredicted;
  wire  fu_units_1_io_req_bits_uop_stat_bpd_made_pred;
  wire  fu_units_1_io_req_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_1_io_req_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_1_io_req_bits_uop_imm_packed;
  wire [11:0] fu_units_1_io_req_bits_uop_csr_addr;
  wire [6:0] fu_units_1_io_req_bits_uop_rob_idx;
  wire [3:0] fu_units_1_io_req_bits_uop_ldq_idx;
  wire [3:0] fu_units_1_io_req_bits_uop_stq_idx;
  wire [5:0] fu_units_1_io_req_bits_uop_brob_idx;
  wire [6:0] fu_units_1_io_req_bits_uop_pdst;
  wire [6:0] fu_units_1_io_req_bits_uop_pop1;
  wire [6:0] fu_units_1_io_req_bits_uop_pop2;
  wire [6:0] fu_units_1_io_req_bits_uop_pop3;
  wire  fu_units_1_io_req_bits_uop_prs1_busy;
  wire  fu_units_1_io_req_bits_uop_prs2_busy;
  wire  fu_units_1_io_req_bits_uop_prs3_busy;
  wire [6:0] fu_units_1_io_req_bits_uop_stale_pdst;
  wire  fu_units_1_io_req_bits_uop_exception;
  wire [63:0] fu_units_1_io_req_bits_uop_exc_cause;
  wire  fu_units_1_io_req_bits_uop_bypassable;
  wire [4:0] fu_units_1_io_req_bits_uop_mem_cmd;
  wire [2:0] fu_units_1_io_req_bits_uop_mem_typ;
  wire  fu_units_1_io_req_bits_uop_is_fence;
  wire  fu_units_1_io_req_bits_uop_is_fencei;
  wire  fu_units_1_io_req_bits_uop_is_store;
  wire  fu_units_1_io_req_bits_uop_is_amo;
  wire  fu_units_1_io_req_bits_uop_is_load;
  wire  fu_units_1_io_req_bits_uop_is_sys_pc2epc;
  wire  fu_units_1_io_req_bits_uop_is_unique;
  wire  fu_units_1_io_req_bits_uop_flush_on_commit;
  wire [5:0] fu_units_1_io_req_bits_uop_ldst;
  wire [5:0] fu_units_1_io_req_bits_uop_lrs1;
  wire [5:0] fu_units_1_io_req_bits_uop_lrs2;
  wire [5:0] fu_units_1_io_req_bits_uop_lrs3;
  wire  fu_units_1_io_req_bits_uop_ldst_val;
  wire [1:0] fu_units_1_io_req_bits_uop_dst_rtype;
  wire [1:0] fu_units_1_io_req_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_1_io_req_bits_uop_lrs2_rtype;
  wire  fu_units_1_io_req_bits_uop_frs3_en;
  wire  fu_units_1_io_req_bits_uop_fp_val;
  wire  fu_units_1_io_req_bits_uop_fp_single;
  wire  fu_units_1_io_req_bits_uop_xcpt_pf_if;
  wire  fu_units_1_io_req_bits_uop_xcpt_ae_if;
  wire  fu_units_1_io_req_bits_uop_replay_if;
  wire  fu_units_1_io_req_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_1_io_req_bits_uop_debug_wdata;
  wire [31:0] fu_units_1_io_req_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_1_io_req_bits_rs1_data;
  wire [63:0] fu_units_1_io_req_bits_rs2_data;
  wire  fu_units_1_io_req_bits_kill;
  wire  fu_units_1_io_resp_valid;
  wire  fu_units_1_io_resp_bits_uop_valid;
  wire [1:0] fu_units_1_io_resp_bits_uop_iw_state;
  wire [8:0] fu_units_1_io_resp_bits_uop_uopc;
  wire [31:0] fu_units_1_io_resp_bits_uop_inst;
  wire [39:0] fu_units_1_io_resp_bits_uop_pc;
  wire [1:0] fu_units_1_io_resp_bits_uop_iqtype;
  wire [9:0] fu_units_1_io_resp_bits_uop_fu_code;
  wire [3:0] fu_units_1_io_resp_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_1_io_resp_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_1_io_resp_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_1_io_resp_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_1_io_resp_bits_uop_ctrl_op_fcn;
  wire  fu_units_1_io_resp_bits_uop_ctrl_fcn_dw;
  wire  fu_units_1_io_resp_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_1_io_resp_bits_uop_ctrl_csr_cmd;
  wire  fu_units_1_io_resp_bits_uop_ctrl_is_load;
  wire  fu_units_1_io_resp_bits_uop_ctrl_is_sta;
  wire  fu_units_1_io_resp_bits_uop_ctrl_is_std;
  wire  fu_units_1_io_resp_bits_uop_allocate_brtag;
  wire  fu_units_1_io_resp_bits_uop_is_br_or_jmp;
  wire  fu_units_1_io_resp_bits_uop_is_jump;
  wire  fu_units_1_io_resp_bits_uop_is_jal;
  wire  fu_units_1_io_resp_bits_uop_is_ret;
  wire  fu_units_1_io_resp_bits_uop_is_call;
  wire [7:0] fu_units_1_io_resp_bits_uop_br_mask;
  wire [2:0] fu_units_1_io_resp_bits_uop_br_tag;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_btb_blame;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_btb_hit;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_btb_taken;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_1_io_resp_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_1_io_resp_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_1_io_resp_bits_uop_stat_btb_made_pred;
  wire  fu_units_1_io_resp_bits_uop_stat_btb_mispredicted;
  wire  fu_units_1_io_resp_bits_uop_stat_bpd_made_pred;
  wire  fu_units_1_io_resp_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_1_io_resp_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_1_io_resp_bits_uop_imm_packed;
  wire [11:0] fu_units_1_io_resp_bits_uop_csr_addr;
  wire [6:0] fu_units_1_io_resp_bits_uop_rob_idx;
  wire [3:0] fu_units_1_io_resp_bits_uop_ldq_idx;
  wire [3:0] fu_units_1_io_resp_bits_uop_stq_idx;
  wire [5:0] fu_units_1_io_resp_bits_uop_brob_idx;
  wire [6:0] fu_units_1_io_resp_bits_uop_pdst;
  wire [6:0] fu_units_1_io_resp_bits_uop_pop1;
  wire [6:0] fu_units_1_io_resp_bits_uop_pop2;
  wire [6:0] fu_units_1_io_resp_bits_uop_pop3;
  wire  fu_units_1_io_resp_bits_uop_prs1_busy;
  wire  fu_units_1_io_resp_bits_uop_prs2_busy;
  wire  fu_units_1_io_resp_bits_uop_prs3_busy;
  wire [6:0] fu_units_1_io_resp_bits_uop_stale_pdst;
  wire  fu_units_1_io_resp_bits_uop_exception;
  wire [63:0] fu_units_1_io_resp_bits_uop_exc_cause;
  wire  fu_units_1_io_resp_bits_uop_bypassable;
  wire [4:0] fu_units_1_io_resp_bits_uop_mem_cmd;
  wire [2:0] fu_units_1_io_resp_bits_uop_mem_typ;
  wire  fu_units_1_io_resp_bits_uop_is_fence;
  wire  fu_units_1_io_resp_bits_uop_is_fencei;
  wire  fu_units_1_io_resp_bits_uop_is_store;
  wire  fu_units_1_io_resp_bits_uop_is_amo;
  wire  fu_units_1_io_resp_bits_uop_is_load;
  wire  fu_units_1_io_resp_bits_uop_is_sys_pc2epc;
  wire  fu_units_1_io_resp_bits_uop_is_unique;
  wire  fu_units_1_io_resp_bits_uop_flush_on_commit;
  wire [5:0] fu_units_1_io_resp_bits_uop_ldst;
  wire [5:0] fu_units_1_io_resp_bits_uop_lrs1;
  wire [5:0] fu_units_1_io_resp_bits_uop_lrs2;
  wire [5:0] fu_units_1_io_resp_bits_uop_lrs3;
  wire  fu_units_1_io_resp_bits_uop_ldst_val;
  wire [1:0] fu_units_1_io_resp_bits_uop_dst_rtype;
  wire [1:0] fu_units_1_io_resp_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_1_io_resp_bits_uop_lrs2_rtype;
  wire  fu_units_1_io_resp_bits_uop_frs3_en;
  wire  fu_units_1_io_resp_bits_uop_fp_val;
  wire  fu_units_1_io_resp_bits_uop_fp_single;
  wire  fu_units_1_io_resp_bits_uop_xcpt_pf_if;
  wire  fu_units_1_io_resp_bits_uop_xcpt_ae_if;
  wire  fu_units_1_io_resp_bits_uop_replay_if;
  wire  fu_units_1_io_resp_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_1_io_resp_bits_uop_debug_wdata;
  wire [31:0] fu_units_1_io_resp_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_1_io_resp_bits_data;
  wire  fu_units_1_io_brinfo_valid;
  wire  fu_units_1_io_brinfo_mispredict;
  wire [7:0] fu_units_1_io_brinfo_mask;
  wire  _T_1244;
  wire  _T_1245;
  wire  muldiv_resp_val;
  wire  fu_units_2_clock;
  wire  fu_units_2_reset;
  wire  fu_units_2_io_req_ready;
  wire  fu_units_2_io_req_valid;
  wire  fu_units_2_io_req_bits_uop_valid;
  wire [1:0] fu_units_2_io_req_bits_uop_iw_state;
  wire [8:0] fu_units_2_io_req_bits_uop_uopc;
  wire [31:0] fu_units_2_io_req_bits_uop_inst;
  wire [39:0] fu_units_2_io_req_bits_uop_pc;
  wire [1:0] fu_units_2_io_req_bits_uop_iqtype;
  wire [9:0] fu_units_2_io_req_bits_uop_fu_code;
  wire [3:0] fu_units_2_io_req_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_2_io_req_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_2_io_req_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_2_io_req_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_2_io_req_bits_uop_ctrl_op_fcn;
  wire  fu_units_2_io_req_bits_uop_ctrl_fcn_dw;
  wire  fu_units_2_io_req_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_2_io_req_bits_uop_ctrl_csr_cmd;
  wire  fu_units_2_io_req_bits_uop_ctrl_is_load;
  wire  fu_units_2_io_req_bits_uop_ctrl_is_sta;
  wire  fu_units_2_io_req_bits_uop_ctrl_is_std;
  wire  fu_units_2_io_req_bits_uop_allocate_brtag;
  wire  fu_units_2_io_req_bits_uop_is_br_or_jmp;
  wire  fu_units_2_io_req_bits_uop_is_jump;
  wire  fu_units_2_io_req_bits_uop_is_jal;
  wire  fu_units_2_io_req_bits_uop_is_ret;
  wire  fu_units_2_io_req_bits_uop_is_call;
  wire [7:0] fu_units_2_io_req_bits_uop_br_mask;
  wire [2:0] fu_units_2_io_req_bits_uop_br_tag;
  wire  fu_units_2_io_req_bits_uop_br_prediction_btb_blame;
  wire  fu_units_2_io_req_bits_uop_br_prediction_btb_hit;
  wire  fu_units_2_io_req_bits_uop_br_prediction_btb_taken;
  wire  fu_units_2_io_req_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_2_io_req_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_2_io_req_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_2_io_req_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_2_io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_2_io_req_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_2_io_req_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_2_io_req_bits_uop_stat_btb_made_pred;
  wire  fu_units_2_io_req_bits_uop_stat_btb_mispredicted;
  wire  fu_units_2_io_req_bits_uop_stat_bpd_made_pred;
  wire  fu_units_2_io_req_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_2_io_req_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_2_io_req_bits_uop_imm_packed;
  wire [11:0] fu_units_2_io_req_bits_uop_csr_addr;
  wire [6:0] fu_units_2_io_req_bits_uop_rob_idx;
  wire [3:0] fu_units_2_io_req_bits_uop_ldq_idx;
  wire [3:0] fu_units_2_io_req_bits_uop_stq_idx;
  wire [5:0] fu_units_2_io_req_bits_uop_brob_idx;
  wire [6:0] fu_units_2_io_req_bits_uop_pdst;
  wire [6:0] fu_units_2_io_req_bits_uop_pop1;
  wire [6:0] fu_units_2_io_req_bits_uop_pop2;
  wire [6:0] fu_units_2_io_req_bits_uop_pop3;
  wire  fu_units_2_io_req_bits_uop_prs1_busy;
  wire  fu_units_2_io_req_bits_uop_prs2_busy;
  wire  fu_units_2_io_req_bits_uop_prs3_busy;
  wire [6:0] fu_units_2_io_req_bits_uop_stale_pdst;
  wire  fu_units_2_io_req_bits_uop_exception;
  wire [63:0] fu_units_2_io_req_bits_uop_exc_cause;
  wire  fu_units_2_io_req_bits_uop_bypassable;
  wire [4:0] fu_units_2_io_req_bits_uop_mem_cmd;
  wire [2:0] fu_units_2_io_req_bits_uop_mem_typ;
  wire  fu_units_2_io_req_bits_uop_is_fence;
  wire  fu_units_2_io_req_bits_uop_is_fencei;
  wire  fu_units_2_io_req_bits_uop_is_store;
  wire  fu_units_2_io_req_bits_uop_is_amo;
  wire  fu_units_2_io_req_bits_uop_is_load;
  wire  fu_units_2_io_req_bits_uop_is_sys_pc2epc;
  wire  fu_units_2_io_req_bits_uop_is_unique;
  wire  fu_units_2_io_req_bits_uop_flush_on_commit;
  wire [5:0] fu_units_2_io_req_bits_uop_ldst;
  wire [5:0] fu_units_2_io_req_bits_uop_lrs1;
  wire [5:0] fu_units_2_io_req_bits_uop_lrs2;
  wire [5:0] fu_units_2_io_req_bits_uop_lrs3;
  wire  fu_units_2_io_req_bits_uop_ldst_val;
  wire [1:0] fu_units_2_io_req_bits_uop_dst_rtype;
  wire [1:0] fu_units_2_io_req_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_2_io_req_bits_uop_lrs2_rtype;
  wire  fu_units_2_io_req_bits_uop_frs3_en;
  wire  fu_units_2_io_req_bits_uop_fp_val;
  wire  fu_units_2_io_req_bits_uop_fp_single;
  wire  fu_units_2_io_req_bits_uop_xcpt_pf_if;
  wire  fu_units_2_io_req_bits_uop_xcpt_ae_if;
  wire  fu_units_2_io_req_bits_uop_replay_if;
  wire  fu_units_2_io_req_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_2_io_req_bits_uop_debug_wdata;
  wire [31:0] fu_units_2_io_req_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_2_io_req_bits_rs1_data;
  wire [63:0] fu_units_2_io_req_bits_rs2_data;
  wire  fu_units_2_io_req_bits_kill;
  wire  fu_units_2_io_resp_ready;
  wire  fu_units_2_io_resp_valid;
  wire  fu_units_2_io_resp_bits_uop_valid;
  wire [1:0] fu_units_2_io_resp_bits_uop_iw_state;
  wire [8:0] fu_units_2_io_resp_bits_uop_uopc;
  wire [31:0] fu_units_2_io_resp_bits_uop_inst;
  wire [39:0] fu_units_2_io_resp_bits_uop_pc;
  wire [1:0] fu_units_2_io_resp_bits_uop_iqtype;
  wire [9:0] fu_units_2_io_resp_bits_uop_fu_code;
  wire [3:0] fu_units_2_io_resp_bits_uop_ctrl_br_type;
  wire [1:0] fu_units_2_io_resp_bits_uop_ctrl_op1_sel;
  wire [2:0] fu_units_2_io_resp_bits_uop_ctrl_op2_sel;
  wire [2:0] fu_units_2_io_resp_bits_uop_ctrl_imm_sel;
  wire [3:0] fu_units_2_io_resp_bits_uop_ctrl_op_fcn;
  wire  fu_units_2_io_resp_bits_uop_ctrl_fcn_dw;
  wire  fu_units_2_io_resp_bits_uop_ctrl_rf_wen;
  wire [2:0] fu_units_2_io_resp_bits_uop_ctrl_csr_cmd;
  wire  fu_units_2_io_resp_bits_uop_ctrl_is_load;
  wire  fu_units_2_io_resp_bits_uop_ctrl_is_sta;
  wire  fu_units_2_io_resp_bits_uop_ctrl_is_std;
  wire  fu_units_2_io_resp_bits_uop_allocate_brtag;
  wire  fu_units_2_io_resp_bits_uop_is_br_or_jmp;
  wire  fu_units_2_io_resp_bits_uop_is_jump;
  wire  fu_units_2_io_resp_bits_uop_is_jal;
  wire  fu_units_2_io_resp_bits_uop_is_ret;
  wire  fu_units_2_io_resp_bits_uop_is_call;
  wire [7:0] fu_units_2_io_resp_bits_uop_br_mask;
  wire [2:0] fu_units_2_io_resp_bits_uop_br_tag;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_btb_blame;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_btb_hit;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_btb_taken;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_bpd_blame;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_bpd_hit;
  wire  fu_units_2_io_resp_bits_uop_br_prediction_bpd_taken;
  wire [1:0] fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_value;
  wire [5:0] fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_entry_idx;
  wire [1:0] fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_way_idx;
  wire [1:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_takens;
  wire [14:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history;
  wire [14:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_u;
  wire [7:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr;
  wire [14:0] fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_info;
  wire  fu_units_2_io_resp_bits_uop_stat_brjmp_mispredicted;
  wire  fu_units_2_io_resp_bits_uop_stat_btb_made_pred;
  wire  fu_units_2_io_resp_bits_uop_stat_btb_mispredicted;
  wire  fu_units_2_io_resp_bits_uop_stat_bpd_made_pred;
  wire  fu_units_2_io_resp_bits_uop_stat_bpd_mispredicted;
  wire [2:0] fu_units_2_io_resp_bits_uop_fetch_pc_lob;
  wire [19:0] fu_units_2_io_resp_bits_uop_imm_packed;
  wire [11:0] fu_units_2_io_resp_bits_uop_csr_addr;
  wire [6:0] fu_units_2_io_resp_bits_uop_rob_idx;
  wire [3:0] fu_units_2_io_resp_bits_uop_ldq_idx;
  wire [3:0] fu_units_2_io_resp_bits_uop_stq_idx;
  wire [5:0] fu_units_2_io_resp_bits_uop_brob_idx;
  wire [6:0] fu_units_2_io_resp_bits_uop_pdst;
  wire [6:0] fu_units_2_io_resp_bits_uop_pop1;
  wire [6:0] fu_units_2_io_resp_bits_uop_pop2;
  wire [6:0] fu_units_2_io_resp_bits_uop_pop3;
  wire  fu_units_2_io_resp_bits_uop_prs1_busy;
  wire  fu_units_2_io_resp_bits_uop_prs2_busy;
  wire  fu_units_2_io_resp_bits_uop_prs3_busy;
  wire [6:0] fu_units_2_io_resp_bits_uop_stale_pdst;
  wire  fu_units_2_io_resp_bits_uop_exception;
  wire [63:0] fu_units_2_io_resp_bits_uop_exc_cause;
  wire  fu_units_2_io_resp_bits_uop_bypassable;
  wire [4:0] fu_units_2_io_resp_bits_uop_mem_cmd;
  wire [2:0] fu_units_2_io_resp_bits_uop_mem_typ;
  wire  fu_units_2_io_resp_bits_uop_is_fence;
  wire  fu_units_2_io_resp_bits_uop_is_fencei;
  wire  fu_units_2_io_resp_bits_uop_is_store;
  wire  fu_units_2_io_resp_bits_uop_is_amo;
  wire  fu_units_2_io_resp_bits_uop_is_load;
  wire  fu_units_2_io_resp_bits_uop_is_sys_pc2epc;
  wire  fu_units_2_io_resp_bits_uop_is_unique;
  wire  fu_units_2_io_resp_bits_uop_flush_on_commit;
  wire [5:0] fu_units_2_io_resp_bits_uop_ldst;
  wire [5:0] fu_units_2_io_resp_bits_uop_lrs1;
  wire [5:0] fu_units_2_io_resp_bits_uop_lrs2;
  wire [5:0] fu_units_2_io_resp_bits_uop_lrs3;
  wire  fu_units_2_io_resp_bits_uop_ldst_val;
  wire [1:0] fu_units_2_io_resp_bits_uop_dst_rtype;
  wire [1:0] fu_units_2_io_resp_bits_uop_lrs1_rtype;
  wire [1:0] fu_units_2_io_resp_bits_uop_lrs2_rtype;
  wire  fu_units_2_io_resp_bits_uop_frs3_en;
  wire  fu_units_2_io_resp_bits_uop_fp_val;
  wire  fu_units_2_io_resp_bits_uop_fp_single;
  wire  fu_units_2_io_resp_bits_uop_xcpt_pf_if;
  wire  fu_units_2_io_resp_bits_uop_xcpt_ae_if;
  wire  fu_units_2_io_resp_bits_uop_replay_if;
  wire  fu_units_2_io_resp_bits_uop_xcpt_ma_if;
  wire [63:0] fu_units_2_io_resp_bits_uop_debug_wdata;
  wire [31:0] fu_units_2_io_resp_bits_uop_debug_events_fetch_seq;
  wire [63:0] fu_units_2_io_resp_bits_data;
  wire  fu_units_2_io_brinfo_valid;
  wire  fu_units_2_io_brinfo_mispredict;
  wire [7:0] fu_units_2_io_brinfo_mask;
  wire  _T_1326;
  wire  _T_1333;
  wire  _T_1334;
  wire  _T_1336;
  wire  _T_1338;
  wire  _T_1345;
  wire  _T_1351;
  wire [95:0] _T_1357;
  wire [1:0] _T_1358;
  wire [2:0] _T_1359;
  wire [98:0] _T_1360;
  wire [1:0] _T_1361;
  wire [2:0] _T_1362;
  wire [3:0] _T_1363;
  wire [4:0] _T_1364;
  wire [7:0] _T_1365;
  wire [106:0] _T_1366;
  wire [2:0] _T_1367;
  wire [11:0] _T_1368;
  wire [17:0] _T_1369;
  wire [20:0] _T_1370;
  wire [1:0] _T_1371;
  wire [7:0] _T_1372;
  wire [1:0] _T_1373;
  wire [2:0] _T_1374;
  wire [10:0] _T_1375;
  wire [31:0] _T_1376;
  wire [138:0] _T_1377;
  wire [1:0] _T_1378;
  wire [7:0] _T_1379;
  wire [8:0] _T_1380;
  wire [10:0] _T_1381;
  wire [64:0] _T_1382;
  wire [65:0] _T_1383;
  wire [1:0] _T_1384;
  wire [8:0] _T_1385;
  wire [74:0] _T_1386;
  wire [85:0] _T_1387;
  wire [13:0] _T_1388;
  wire [14:0] _T_1389;
  wire [12:0] _T_1390;
  wire [19:0] _T_1391;
  wire [34:0] _T_1392;
  wire [10:0] _T_1393;
  wire [14:0] _T_1394;
  wire [22:0] _T_1395;
  wire [34:0] _T_1396;
  wire [49:0] _T_1397;
  wire [84:0] _T_1398;
  wire [170:0] _T_1399;
  wire [309:0] _T_1400;
  wire [1:0] _T_1401;
  wire [1:0] _T_1402;
  wire [2:0] _T_1403;
  wire [4:0] _T_1404;
  wire [22:0] _T_1405;
  wire [37:0] _T_1406;
  wire [3:0] _T_1407;
  wire [18:0] _T_1408;
  wire [56:0] _T_1409;
  wire [61:0] _T_1410;
  wire [7:0] _T_1411;
  wire [1:0] _T_1412;
  wire [2:0] _T_1413;
  wire [10:0] _T_1414;
  wire [1:0] _T_1415;
  wire [2:0] _T_1416;
  wire [8:0] _T_1417;
  wire [11:0] _T_1418;
  wire [14:0] _T_1419;
  wire [25:0] _T_1420;
  wire [87:0] _T_1421;
  wire [1:0] _T_1422;
  wire [1:0] _T_1423;
  wire [2:0] _T_1424;
  wire [4:0] _T_1425;
  wire [1:0] _T_1426;
  wire [2:0] _T_1427;
  wire [1:0] _T_1428;
  wire [4:0] _T_1429;
  wire [7:0] _T_1430;
  wire [12:0] _T_1431;
  wire [5:0] _T_1432;
  wire [9:0] _T_1433;
  wire [13:0] _T_1434;
  wire [15:0] _T_1435;
  wire [25:0] _T_1436;
  wire [71:0] _T_1437;
  wire [73:0] _T_1438;
  wire [2:0] _T_1439;
  wire [11:0] _T_1440;
  wire [85:0] _T_1441;
  wire [111:0] _T_1442;
  wire [124:0] _T_1443;
  wire [212:0] _T_1444;
  wire [522:0] _T_1445;
  wire [95:0] _T_1446;
  wire [1:0] _T_1447;
  wire [2:0] _T_1448;
  wire [98:0] _T_1449;
  wire [1:0] _T_1450;
  wire [2:0] _T_1451;
  wire [3:0] _T_1452;
  wire [4:0] _T_1453;
  wire [7:0] _T_1454;
  wire [106:0] _T_1455;
  wire [2:0] _T_1456;
  wire [11:0] _T_1457;
  wire [17:0] _T_1458;
  wire [20:0] _T_1459;
  wire [1:0] _T_1460;
  wire [7:0] _T_1461;
  wire [1:0] _T_1462;
  wire [2:0] _T_1463;
  wire [10:0] _T_1464;
  wire [31:0] _T_1465;
  wire [138:0] _T_1466;
  wire [1:0] _T_1467;
  wire [7:0] _T_1468;
  wire [8:0] _T_1469;
  wire [10:0] _T_1470;
  wire [64:0] _T_1471;
  wire [65:0] _T_1472;
  wire [1:0] _T_1473;
  wire [8:0] _T_1474;
  wire [74:0] _T_1475;
  wire [85:0] _T_1476;
  wire [13:0] _T_1477;
  wire [14:0] _T_1478;
  wire [12:0] _T_1479;
  wire [19:0] _T_1480;
  wire [34:0] _T_1481;
  wire [10:0] _T_1482;
  wire [14:0] _T_1483;
  wire [22:0] _T_1484;
  wire [34:0] _T_1485;
  wire [49:0] _T_1486;
  wire [84:0] _T_1487;
  wire [170:0] _T_1488;
  wire [309:0] _T_1489;
  wire [1:0] _T_1490;
  wire [1:0] _T_1491;
  wire [2:0] _T_1492;
  wire [4:0] _T_1493;
  wire [22:0] _T_1494;
  wire [37:0] _T_1495;
  wire [3:0] _T_1496;
  wire [18:0] _T_1497;
  wire [56:0] _T_1498;
  wire [61:0] _T_1499;
  wire [7:0] _T_1500;
  wire [1:0] _T_1501;
  wire [2:0] _T_1502;
  wire [10:0] _T_1503;
  wire [1:0] _T_1504;
  wire [2:0] _T_1505;
  wire [8:0] _T_1506;
  wire [11:0] _T_1507;
  wire [14:0] _T_1508;
  wire [25:0] _T_1509;
  wire [87:0] _T_1510;
  wire [1:0] _T_1511;
  wire [1:0] _T_1512;
  wire [2:0] _T_1513;
  wire [4:0] _T_1514;
  wire [1:0] _T_1515;
  wire [2:0] _T_1516;
  wire [1:0] _T_1517;
  wire [4:0] _T_1518;
  wire [7:0] _T_1519;
  wire [12:0] _T_1520;
  wire [5:0] _T_1521;
  wire [9:0] _T_1522;
  wire [13:0] _T_1523;
  wire [15:0] _T_1524;
  wire [25:0] _T_1525;
  wire [71:0] _T_1526;
  wire [73:0] _T_1527;
  wire [2:0] _T_1528;
  wire [11:0] _T_1529;
  wire [85:0] _T_1530;
  wire [111:0] _T_1531;
  wire [124:0] _T_1532;
  wire [212:0] _T_1533;
  wire [522:0] _T_1534;
  wire [95:0] _T_1535;
  wire [1:0] _T_1536;
  wire [2:0] _T_1537;
  wire [98:0] _T_1538;
  wire [1:0] _T_1539;
  wire [2:0] _T_1540;
  wire [3:0] _T_1541;
  wire [4:0] _T_1542;
  wire [7:0] _T_1543;
  wire [106:0] _T_1544;
  wire [2:0] _T_1545;
  wire [11:0] _T_1546;
  wire [17:0] _T_1547;
  wire [20:0] _T_1548;
  wire [1:0] _T_1549;
  wire [7:0] _T_1550;
  wire [1:0] _T_1551;
  wire [2:0] _T_1552;
  wire [10:0] _T_1553;
  wire [31:0] _T_1554;
  wire [138:0] _T_1555;
  wire [1:0] _T_1556;
  wire [7:0] _T_1557;
  wire [8:0] _T_1558;
  wire [10:0] _T_1559;
  wire [64:0] _T_1560;
  wire [65:0] _T_1561;
  wire [1:0] _T_1562;
  wire [8:0] _T_1563;
  wire [74:0] _T_1564;
  wire [85:0] _T_1565;
  wire [13:0] _T_1566;
  wire [14:0] _T_1567;
  wire [12:0] _T_1568;
  wire [19:0] _T_1569;
  wire [34:0] _T_1570;
  wire [10:0] _T_1571;
  wire [14:0] _T_1572;
  wire [22:0] _T_1573;
  wire [34:0] _T_1574;
  wire [49:0] _T_1575;
  wire [84:0] _T_1576;
  wire [170:0] _T_1577;
  wire [309:0] _T_1578;
  wire [1:0] _T_1579;
  wire [1:0] _T_1580;
  wire [2:0] _T_1581;
  wire [4:0] _T_1582;
  wire [22:0] _T_1583;
  wire [37:0] _T_1584;
  wire [3:0] _T_1585;
  wire [18:0] _T_1586;
  wire [56:0] _T_1587;
  wire [61:0] _T_1588;
  wire [7:0] _T_1589;
  wire [1:0] _T_1590;
  wire [2:0] _T_1591;
  wire [10:0] _T_1592;
  wire [1:0] _T_1593;
  wire [2:0] _T_1594;
  wire [8:0] _T_1595;
  wire [11:0] _T_1596;
  wire [14:0] _T_1597;
  wire [25:0] _T_1598;
  wire [87:0] _T_1599;
  wire [1:0] _T_1600;
  wire [1:0] _T_1601;
  wire [2:0] _T_1602;
  wire [4:0] _T_1603;
  wire [1:0] _T_1604;
  wire [2:0] _T_1605;
  wire [1:0] _T_1606;
  wire [4:0] _T_1607;
  wire [7:0] _T_1608;
  wire [12:0] _T_1609;
  wire [5:0] _T_1610;
  wire [9:0] _T_1611;
  wire [13:0] _T_1612;
  wire [15:0] _T_1613;
  wire [25:0] _T_1614;
  wire [71:0] _T_1615;
  wire [73:0] _T_1616;
  wire [2:0] _T_1617;
  wire [11:0] _T_1618;
  wire [85:0] _T_1619;
  wire [111:0] _T_1620;
  wire [124:0] _T_1621;
  wire [212:0] _T_1622;
  wire [522:0] _T_1623;
  wire [522:0] _T_1624;
  wire [522:0] _T_1625;
  wire  _T_1631_ctrl_rf_wen;
  wire [6:0] _T_1631_rob_idx;
  wire [6:0] _T_1631_pdst;
  wire  _T_1631_bypassable;
  wire  _T_1631_is_store;
  wire  _T_1631_is_amo;
  wire [1:0] _T_1631_dst_rtype;
  wire [522:0] _T_1637;
  wire [1:0] _T_1649;
  wire  _T_1659;
  wire  _T_1660;
  wire  _T_1665;
  wire [6:0] _T_1675;
  wire [6:0] _T_1679;
  wire  _T_1714;
  wire [63:0] _T_1728;
  wire [63:0] _T_1729;
  wire  _T_1730;
  wire  _T_1731;
  wire [10:0] _T_1735;
  wire [7:0] _T_1741;
  wire  _T_1747;
  wire  _T_1748;
  wire [4:0] _T_1753;
  wire [4:0] _T_1754;
  wire [4:0] _T_1758;
  wire [4:0] _T_1759;
  wire  _T_1768;
  wire [4:0] _T_1769;
  wire [4:0] _T_1770;
  wire [9:0] _T_1771;
  wire [10:0] _T_1772;
  wire  _T_1773;
  wire [7:0] _T_1774;
  wire [8:0] _T_1775;
  wire [10:0] _T_1776;
  wire [11:0] _T_1778;
  wire [20:0] _T_1779;
  wire [31:0] _T_1780;
  wire [31:0] _T_1781;
  wire [31:0] _T_1782;
  wire [1:0] _T_1796;
  wire [1:0] _GEN_0;
  wire [2:0] _T_1797;
  wire  _T_1799;
  wire  _T_1801;
  wire  _T_1802;
  wire  _T_1809;
  wire  _T_1811;
  wire  _T_1812;
  wire  _T_1821;
  wire  _T_1823;
  ALUUnit alu (
    .clock(alu_clock),
    .reset(alu_reset),
    .io_req_valid(alu_io_req_valid),
    .io_req_bits_uop_valid(alu_io_req_bits_uop_valid),
    .io_req_bits_uop_iw_state(alu_io_req_bits_uop_iw_state),
    .io_req_bits_uop_uopc(alu_io_req_bits_uop_uopc),
    .io_req_bits_uop_inst(alu_io_req_bits_uop_inst),
    .io_req_bits_uop_pc(alu_io_req_bits_uop_pc),
    .io_req_bits_uop_iqtype(alu_io_req_bits_uop_iqtype),
    .io_req_bits_uop_fu_code(alu_io_req_bits_uop_fu_code),
    .io_req_bits_uop_ctrl_br_type(alu_io_req_bits_uop_ctrl_br_type),
    .io_req_bits_uop_ctrl_op1_sel(alu_io_req_bits_uop_ctrl_op1_sel),
    .io_req_bits_uop_ctrl_op2_sel(alu_io_req_bits_uop_ctrl_op2_sel),
    .io_req_bits_uop_ctrl_imm_sel(alu_io_req_bits_uop_ctrl_imm_sel),
    .io_req_bits_uop_ctrl_op_fcn(alu_io_req_bits_uop_ctrl_op_fcn),
    .io_req_bits_uop_ctrl_fcn_dw(alu_io_req_bits_uop_ctrl_fcn_dw),
    .io_req_bits_uop_ctrl_rf_wen(alu_io_req_bits_uop_ctrl_rf_wen),
    .io_req_bits_uop_ctrl_csr_cmd(alu_io_req_bits_uop_ctrl_csr_cmd),
    .io_req_bits_uop_ctrl_is_load(alu_io_req_bits_uop_ctrl_is_load),
    .io_req_bits_uop_ctrl_is_sta(alu_io_req_bits_uop_ctrl_is_sta),
    .io_req_bits_uop_ctrl_is_std(alu_io_req_bits_uop_ctrl_is_std),
    .io_req_bits_uop_allocate_brtag(alu_io_req_bits_uop_allocate_brtag),
    .io_req_bits_uop_is_br_or_jmp(alu_io_req_bits_uop_is_br_or_jmp),
    .io_req_bits_uop_is_jump(alu_io_req_bits_uop_is_jump),
    .io_req_bits_uop_is_jal(alu_io_req_bits_uop_is_jal),
    .io_req_bits_uop_is_ret(alu_io_req_bits_uop_is_ret),
    .io_req_bits_uop_is_call(alu_io_req_bits_uop_is_call),
    .io_req_bits_uop_br_mask(alu_io_req_bits_uop_br_mask),
    .io_req_bits_uop_br_tag(alu_io_req_bits_uop_br_tag),
    .io_req_bits_uop_br_prediction_btb_blame(alu_io_req_bits_uop_br_prediction_btb_blame),
    .io_req_bits_uop_br_prediction_btb_hit(alu_io_req_bits_uop_br_prediction_btb_hit),
    .io_req_bits_uop_br_prediction_btb_taken(alu_io_req_bits_uop_br_prediction_btb_taken),
    .io_req_bits_uop_br_prediction_bpd_blame(alu_io_req_bits_uop_br_prediction_bpd_blame),
    .io_req_bits_uop_br_prediction_bpd_hit(alu_io_req_bits_uop_br_prediction_bpd_hit),
    .io_req_bits_uop_br_prediction_bpd_taken(alu_io_req_bits_uop_br_prediction_bpd_taken),
    .io_req_bits_uop_br_prediction_bim_resp_value(alu_io_req_bits_uop_br_prediction_bim_resp_value),
    .io_req_bits_uop_br_prediction_bim_resp_entry_idx(alu_io_req_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_req_bits_uop_br_prediction_bim_resp_way_idx(alu_io_req_bits_uop_br_prediction_bim_resp_way_idx),
    .io_req_bits_uop_br_prediction_bpd_resp_takens(alu_io_req_bits_uop_br_prediction_bpd_resp_takens),
    .io_req_bits_uop_br_prediction_bpd_resp_history(alu_io_req_bits_uop_br_prediction_bpd_resp_history),
    .io_req_bits_uop_br_prediction_bpd_resp_history_u(alu_io_req_bits_uop_br_prediction_bpd_resp_history_u),
    .io_req_bits_uop_br_prediction_bpd_resp_history_ptr(alu_io_req_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_req_bits_uop_br_prediction_bpd_resp_info(alu_io_req_bits_uop_br_prediction_bpd_resp_info),
    .io_req_bits_uop_stat_brjmp_mispredicted(alu_io_req_bits_uop_stat_brjmp_mispredicted),
    .io_req_bits_uop_stat_btb_made_pred(alu_io_req_bits_uop_stat_btb_made_pred),
    .io_req_bits_uop_stat_btb_mispredicted(alu_io_req_bits_uop_stat_btb_mispredicted),
    .io_req_bits_uop_stat_bpd_made_pred(alu_io_req_bits_uop_stat_bpd_made_pred),
    .io_req_bits_uop_stat_bpd_mispredicted(alu_io_req_bits_uop_stat_bpd_mispredicted),
    .io_req_bits_uop_fetch_pc_lob(alu_io_req_bits_uop_fetch_pc_lob),
    .io_req_bits_uop_imm_packed(alu_io_req_bits_uop_imm_packed),
    .io_req_bits_uop_csr_addr(alu_io_req_bits_uop_csr_addr),
    .io_req_bits_uop_rob_idx(alu_io_req_bits_uop_rob_idx),
    .io_req_bits_uop_ldq_idx(alu_io_req_bits_uop_ldq_idx),
    .io_req_bits_uop_stq_idx(alu_io_req_bits_uop_stq_idx),
    .io_req_bits_uop_brob_idx(alu_io_req_bits_uop_brob_idx),
    .io_req_bits_uop_pdst(alu_io_req_bits_uop_pdst),
    .io_req_bits_uop_pop1(alu_io_req_bits_uop_pop1),
    .io_req_bits_uop_pop2(alu_io_req_bits_uop_pop2),
    .io_req_bits_uop_pop3(alu_io_req_bits_uop_pop3),
    .io_req_bits_uop_prs1_busy(alu_io_req_bits_uop_prs1_busy),
    .io_req_bits_uop_prs2_busy(alu_io_req_bits_uop_prs2_busy),
    .io_req_bits_uop_prs3_busy(alu_io_req_bits_uop_prs3_busy),
    .io_req_bits_uop_stale_pdst(alu_io_req_bits_uop_stale_pdst),
    .io_req_bits_uop_exception(alu_io_req_bits_uop_exception),
    .io_req_bits_uop_exc_cause(alu_io_req_bits_uop_exc_cause),
    .io_req_bits_uop_bypassable(alu_io_req_bits_uop_bypassable),
    .io_req_bits_uop_mem_cmd(alu_io_req_bits_uop_mem_cmd),
    .io_req_bits_uop_mem_typ(alu_io_req_bits_uop_mem_typ),
    .io_req_bits_uop_is_fence(alu_io_req_bits_uop_is_fence),
    .io_req_bits_uop_is_fencei(alu_io_req_bits_uop_is_fencei),
    .io_req_bits_uop_is_store(alu_io_req_bits_uop_is_store),
    .io_req_bits_uop_is_amo(alu_io_req_bits_uop_is_amo),
    .io_req_bits_uop_is_load(alu_io_req_bits_uop_is_load),
    .io_req_bits_uop_is_sys_pc2epc(alu_io_req_bits_uop_is_sys_pc2epc),
    .io_req_bits_uop_is_unique(alu_io_req_bits_uop_is_unique),
    .io_req_bits_uop_flush_on_commit(alu_io_req_bits_uop_flush_on_commit),
    .io_req_bits_uop_ldst(alu_io_req_bits_uop_ldst),
    .io_req_bits_uop_lrs1(alu_io_req_bits_uop_lrs1),
    .io_req_bits_uop_lrs2(alu_io_req_bits_uop_lrs2),
    .io_req_bits_uop_lrs3(alu_io_req_bits_uop_lrs3),
    .io_req_bits_uop_ldst_val(alu_io_req_bits_uop_ldst_val),
    .io_req_bits_uop_dst_rtype(alu_io_req_bits_uop_dst_rtype),
    .io_req_bits_uop_lrs1_rtype(alu_io_req_bits_uop_lrs1_rtype),
    .io_req_bits_uop_lrs2_rtype(alu_io_req_bits_uop_lrs2_rtype),
    .io_req_bits_uop_frs3_en(alu_io_req_bits_uop_frs3_en),
    .io_req_bits_uop_fp_val(alu_io_req_bits_uop_fp_val),
    .io_req_bits_uop_fp_single(alu_io_req_bits_uop_fp_single),
    .io_req_bits_uop_xcpt_pf_if(alu_io_req_bits_uop_xcpt_pf_if),
    .io_req_bits_uop_xcpt_ae_if(alu_io_req_bits_uop_xcpt_ae_if),
    .io_req_bits_uop_replay_if(alu_io_req_bits_uop_replay_if),
    .io_req_bits_uop_xcpt_ma_if(alu_io_req_bits_uop_xcpt_ma_if),
    .io_req_bits_uop_debug_wdata(alu_io_req_bits_uop_debug_wdata),
    .io_req_bits_uop_debug_events_fetch_seq(alu_io_req_bits_uop_debug_events_fetch_seq),
    .io_req_bits_rs1_data(alu_io_req_bits_rs1_data),
    .io_req_bits_rs2_data(alu_io_req_bits_rs2_data),
    .io_req_bits_kill(alu_io_req_bits_kill),
    .io_resp_valid(alu_io_resp_valid),
    .io_resp_bits_uop_valid(alu_io_resp_bits_uop_valid),
    .io_resp_bits_uop_iw_state(alu_io_resp_bits_uop_iw_state),
    .io_resp_bits_uop_uopc(alu_io_resp_bits_uop_uopc),
    .io_resp_bits_uop_inst(alu_io_resp_bits_uop_inst),
    .io_resp_bits_uop_pc(alu_io_resp_bits_uop_pc),
    .io_resp_bits_uop_iqtype(alu_io_resp_bits_uop_iqtype),
    .io_resp_bits_uop_fu_code(alu_io_resp_bits_uop_fu_code),
    .io_resp_bits_uop_ctrl_br_type(alu_io_resp_bits_uop_ctrl_br_type),
    .io_resp_bits_uop_ctrl_op1_sel(alu_io_resp_bits_uop_ctrl_op1_sel),
    .io_resp_bits_uop_ctrl_op2_sel(alu_io_resp_bits_uop_ctrl_op2_sel),
    .io_resp_bits_uop_ctrl_imm_sel(alu_io_resp_bits_uop_ctrl_imm_sel),
    .io_resp_bits_uop_ctrl_op_fcn(alu_io_resp_bits_uop_ctrl_op_fcn),
    .io_resp_bits_uop_ctrl_fcn_dw(alu_io_resp_bits_uop_ctrl_fcn_dw),
    .io_resp_bits_uop_ctrl_rf_wen(alu_io_resp_bits_uop_ctrl_rf_wen),
    .io_resp_bits_uop_ctrl_csr_cmd(alu_io_resp_bits_uop_ctrl_csr_cmd),
    .io_resp_bits_uop_ctrl_is_load(alu_io_resp_bits_uop_ctrl_is_load),
    .io_resp_bits_uop_ctrl_is_sta(alu_io_resp_bits_uop_ctrl_is_sta),
    .io_resp_bits_uop_ctrl_is_std(alu_io_resp_bits_uop_ctrl_is_std),
    .io_resp_bits_uop_allocate_brtag(alu_io_resp_bits_uop_allocate_brtag),
    .io_resp_bits_uop_is_br_or_jmp(alu_io_resp_bits_uop_is_br_or_jmp),
    .io_resp_bits_uop_is_jump(alu_io_resp_bits_uop_is_jump),
    .io_resp_bits_uop_is_jal(alu_io_resp_bits_uop_is_jal),
    .io_resp_bits_uop_is_ret(alu_io_resp_bits_uop_is_ret),
    .io_resp_bits_uop_is_call(alu_io_resp_bits_uop_is_call),
    .io_resp_bits_uop_br_mask(alu_io_resp_bits_uop_br_mask),
    .io_resp_bits_uop_br_tag(alu_io_resp_bits_uop_br_tag),
    .io_resp_bits_uop_br_prediction_btb_blame(alu_io_resp_bits_uop_br_prediction_btb_blame),
    .io_resp_bits_uop_br_prediction_btb_hit(alu_io_resp_bits_uop_br_prediction_btb_hit),
    .io_resp_bits_uop_br_prediction_btb_taken(alu_io_resp_bits_uop_br_prediction_btb_taken),
    .io_resp_bits_uop_br_prediction_bpd_blame(alu_io_resp_bits_uop_br_prediction_bpd_blame),
    .io_resp_bits_uop_br_prediction_bpd_hit(alu_io_resp_bits_uop_br_prediction_bpd_hit),
    .io_resp_bits_uop_br_prediction_bpd_taken(alu_io_resp_bits_uop_br_prediction_bpd_taken),
    .io_resp_bits_uop_br_prediction_bim_resp_value(alu_io_resp_bits_uop_br_prediction_bim_resp_value),
    .io_resp_bits_uop_br_prediction_bim_resp_entry_idx(alu_io_resp_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_resp_bits_uop_br_prediction_bim_resp_way_idx(alu_io_resp_bits_uop_br_prediction_bim_resp_way_idx),
    .io_resp_bits_uop_br_prediction_bpd_resp_takens(alu_io_resp_bits_uop_br_prediction_bpd_resp_takens),
    .io_resp_bits_uop_br_prediction_bpd_resp_history(alu_io_resp_bits_uop_br_prediction_bpd_resp_history),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_u(alu_io_resp_bits_uop_br_prediction_bpd_resp_history_u),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_ptr(alu_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_resp_bits_uop_br_prediction_bpd_resp_info(alu_io_resp_bits_uop_br_prediction_bpd_resp_info),
    .io_resp_bits_uop_stat_brjmp_mispredicted(alu_io_resp_bits_uop_stat_brjmp_mispredicted),
    .io_resp_bits_uop_stat_btb_made_pred(alu_io_resp_bits_uop_stat_btb_made_pred),
    .io_resp_bits_uop_stat_btb_mispredicted(alu_io_resp_bits_uop_stat_btb_mispredicted),
    .io_resp_bits_uop_stat_bpd_made_pred(alu_io_resp_bits_uop_stat_bpd_made_pred),
    .io_resp_bits_uop_stat_bpd_mispredicted(alu_io_resp_bits_uop_stat_bpd_mispredicted),
    .io_resp_bits_uop_fetch_pc_lob(alu_io_resp_bits_uop_fetch_pc_lob),
    .io_resp_bits_uop_imm_packed(alu_io_resp_bits_uop_imm_packed),
    .io_resp_bits_uop_csr_addr(alu_io_resp_bits_uop_csr_addr),
    .io_resp_bits_uop_rob_idx(alu_io_resp_bits_uop_rob_idx),
    .io_resp_bits_uop_ldq_idx(alu_io_resp_bits_uop_ldq_idx),
    .io_resp_bits_uop_stq_idx(alu_io_resp_bits_uop_stq_idx),
    .io_resp_bits_uop_brob_idx(alu_io_resp_bits_uop_brob_idx),
    .io_resp_bits_uop_pdst(alu_io_resp_bits_uop_pdst),
    .io_resp_bits_uop_pop1(alu_io_resp_bits_uop_pop1),
    .io_resp_bits_uop_pop2(alu_io_resp_bits_uop_pop2),
    .io_resp_bits_uop_pop3(alu_io_resp_bits_uop_pop3),
    .io_resp_bits_uop_prs1_busy(alu_io_resp_bits_uop_prs1_busy),
    .io_resp_bits_uop_prs2_busy(alu_io_resp_bits_uop_prs2_busy),
    .io_resp_bits_uop_prs3_busy(alu_io_resp_bits_uop_prs3_busy),
    .io_resp_bits_uop_stale_pdst(alu_io_resp_bits_uop_stale_pdst),
    .io_resp_bits_uop_exception(alu_io_resp_bits_uop_exception),
    .io_resp_bits_uop_exc_cause(alu_io_resp_bits_uop_exc_cause),
    .io_resp_bits_uop_bypassable(alu_io_resp_bits_uop_bypassable),
    .io_resp_bits_uop_mem_cmd(alu_io_resp_bits_uop_mem_cmd),
    .io_resp_bits_uop_mem_typ(alu_io_resp_bits_uop_mem_typ),
    .io_resp_bits_uop_is_fence(alu_io_resp_bits_uop_is_fence),
    .io_resp_bits_uop_is_fencei(alu_io_resp_bits_uop_is_fencei),
    .io_resp_bits_uop_is_store(alu_io_resp_bits_uop_is_store),
    .io_resp_bits_uop_is_amo(alu_io_resp_bits_uop_is_amo),
    .io_resp_bits_uop_is_load(alu_io_resp_bits_uop_is_load),
    .io_resp_bits_uop_is_sys_pc2epc(alu_io_resp_bits_uop_is_sys_pc2epc),
    .io_resp_bits_uop_is_unique(alu_io_resp_bits_uop_is_unique),
    .io_resp_bits_uop_flush_on_commit(alu_io_resp_bits_uop_flush_on_commit),
    .io_resp_bits_uop_ldst(alu_io_resp_bits_uop_ldst),
    .io_resp_bits_uop_lrs1(alu_io_resp_bits_uop_lrs1),
    .io_resp_bits_uop_lrs2(alu_io_resp_bits_uop_lrs2),
    .io_resp_bits_uop_lrs3(alu_io_resp_bits_uop_lrs3),
    .io_resp_bits_uop_ldst_val(alu_io_resp_bits_uop_ldst_val),
    .io_resp_bits_uop_dst_rtype(alu_io_resp_bits_uop_dst_rtype),
    .io_resp_bits_uop_lrs1_rtype(alu_io_resp_bits_uop_lrs1_rtype),
    .io_resp_bits_uop_lrs2_rtype(alu_io_resp_bits_uop_lrs2_rtype),
    .io_resp_bits_uop_frs3_en(alu_io_resp_bits_uop_frs3_en),
    .io_resp_bits_uop_fp_val(alu_io_resp_bits_uop_fp_val),
    .io_resp_bits_uop_fp_single(alu_io_resp_bits_uop_fp_single),
    .io_resp_bits_uop_xcpt_pf_if(alu_io_resp_bits_uop_xcpt_pf_if),
    .io_resp_bits_uop_xcpt_ae_if(alu_io_resp_bits_uop_xcpt_ae_if),
    .io_resp_bits_uop_replay_if(alu_io_resp_bits_uop_replay_if),
    .io_resp_bits_uop_xcpt_ma_if(alu_io_resp_bits_uop_xcpt_ma_if),
    .io_resp_bits_uop_debug_wdata(alu_io_resp_bits_uop_debug_wdata),
    .io_resp_bits_uop_debug_events_fetch_seq(alu_io_resp_bits_uop_debug_events_fetch_seq),
    .io_resp_bits_data(alu_io_resp_bits_data),
    .io_brinfo_valid(alu_io_brinfo_valid),
    .io_brinfo_mispredict(alu_io_brinfo_mispredict),
    .io_brinfo_mask(alu_io_brinfo_mask),
    .io_bypass_valid_0(alu_io_bypass_valid_0),
    .io_bypass_valid_1(alu_io_bypass_valid_1),
    .io_bypass_valid_2(alu_io_bypass_valid_2),
    .io_bypass_uop_0_ctrl_rf_wen(alu_io_bypass_uop_0_ctrl_rf_wen),
    .io_bypass_uop_0_pdst(alu_io_bypass_uop_0_pdst),
    .io_bypass_uop_0_dst_rtype(alu_io_bypass_uop_0_dst_rtype),
    .io_bypass_uop_1_ctrl_rf_wen(alu_io_bypass_uop_1_ctrl_rf_wen),
    .io_bypass_uop_1_pdst(alu_io_bypass_uop_1_pdst),
    .io_bypass_uop_1_dst_rtype(alu_io_bypass_uop_1_dst_rtype),
    .io_bypass_uop_2_ctrl_rf_wen(alu_io_bypass_uop_2_ctrl_rf_wen),
    .io_bypass_uop_2_pdst(alu_io_bypass_uop_2_pdst),
    .io_bypass_uop_2_dst_rtype(alu_io_bypass_uop_2_dst_rtype),
    .io_bypass_data_0(alu_io_bypass_data_0),
    .io_bypass_data_1(alu_io_bypass_data_1),
    .io_bypass_data_2(alu_io_bypass_data_2),
    .io_br_unit_take_pc(alu_io_br_unit_take_pc),
    .io_br_unit_target(alu_io_br_unit_target),
    .io_br_unit_brinfo_valid(alu_io_br_unit_brinfo_valid),
    .io_br_unit_brinfo_mispredict(alu_io_br_unit_brinfo_mispredict),
    .io_br_unit_brinfo_mask(alu_io_br_unit_brinfo_mask),
    .io_br_unit_brinfo_tag(alu_io_br_unit_brinfo_tag),
    .io_br_unit_brinfo_exe_mask(alu_io_br_unit_brinfo_exe_mask),
    .io_br_unit_brinfo_rob_idx(alu_io_br_unit_brinfo_rob_idx),
    .io_br_unit_brinfo_ldq_idx(alu_io_br_unit_brinfo_ldq_idx),
    .io_br_unit_brinfo_stq_idx(alu_io_br_unit_brinfo_stq_idx),
    .io_br_unit_brinfo_is_jr(alu_io_br_unit_brinfo_is_jr),
    .io_br_unit_btb_update_valid(alu_io_br_unit_btb_update_valid),
    .io_br_unit_btb_update_bits_pc(alu_io_br_unit_btb_update_bits_pc),
    .io_br_unit_btb_update_bits_target(alu_io_br_unit_btb_update_bits_target),
    .io_br_unit_btb_update_bits_cfi_pc(alu_io_br_unit_btb_update_bits_cfi_pc),
    .io_br_unit_btb_update_bits_bpd_type(alu_io_br_unit_btb_update_bits_bpd_type),
    .io_br_unit_btb_update_bits_cfi_type(alu_io_br_unit_btb_update_bits_cfi_type),
    .io_br_unit_bim_update_valid(alu_io_br_unit_bim_update_valid),
    .io_br_unit_bim_update_bits_taken(alu_io_br_unit_bim_update_bits_taken),
    .io_br_unit_bim_update_bits_bim_resp_value(alu_io_br_unit_bim_update_bits_bim_resp_value),
    .io_br_unit_bim_update_bits_bim_resp_entry_idx(alu_io_br_unit_bim_update_bits_bim_resp_entry_idx),
    .io_br_unit_bim_update_bits_bim_resp_way_idx(alu_io_br_unit_bim_update_bits_bim_resp_way_idx),
    .io_br_unit_bpd_update_valid(alu_io_br_unit_bpd_update_valid),
    .io_br_unit_bpd_update_bits_br_pc(alu_io_br_unit_bpd_update_bits_br_pc),
    .io_br_unit_bpd_update_bits_brob_idx(alu_io_br_unit_bpd_update_bits_brob_idx),
    .io_br_unit_bpd_update_bits_mispredict(alu_io_br_unit_bpd_update_bits_mispredict),
    .io_br_unit_bpd_update_bits_history(alu_io_br_unit_bpd_update_bits_history),
    .io_br_unit_bpd_update_bits_history_ptr(alu_io_br_unit_bpd_update_bits_history_ptr),
    .io_br_unit_bpd_update_bits_bpd_predict_val(alu_io_br_unit_bpd_update_bits_bpd_predict_val),
    .io_br_unit_bpd_update_bits_bpd_mispredict(alu_io_br_unit_bpd_update_bits_bpd_mispredict),
    .io_br_unit_bpd_update_bits_taken(alu_io_br_unit_bpd_update_bits_taken),
    .io_br_unit_bpd_update_bits_is_br(alu_io_br_unit_bpd_update_bits_is_br),
    .io_br_unit_bpd_update_bits_new_pc_same_packet(alu_io_br_unit_bpd_update_bits_new_pc_same_packet),
    .io_br_unit_xcpt_valid(alu_io_br_unit_xcpt_valid),
    .io_br_unit_xcpt_bits_uop_br_mask(alu_io_br_unit_xcpt_bits_uop_br_mask),
    .io_br_unit_xcpt_bits_uop_rob_idx(alu_io_br_unit_xcpt_bits_uop_rob_idx),
    .io_br_unit_xcpt_bits_badvaddr(alu_io_br_unit_xcpt_bits_badvaddr),
    .io_get_rob_pc_curr_pc(alu_io_get_rob_pc_curr_pc),
    .io_get_rob_pc_curr_brob_idx(alu_io_get_rob_pc_curr_brob_idx),
    .io_get_rob_pc_next_val(alu_io_get_rob_pc_next_val),
    .io_get_rob_pc_next_pc(alu_io_get_rob_pc_next_pc),
    .io_get_pred_info_bim_resp_value(alu_io_get_pred_info_bim_resp_value),
    .io_get_pred_info_bim_resp_entry_idx(alu_io_get_pred_info_bim_resp_entry_idx),
    .io_get_pred_info_bim_resp_way_idx(alu_io_get_pred_info_bim_resp_way_idx),
    .io_get_pred_info_bpd_resp_history(alu_io_get_pred_info_bpd_resp_history),
    .io_get_pred_info_bpd_resp_history_ptr(alu_io_get_pred_info_bpd_resp_history_ptr),
    .io_status_debug(alu_io_status_debug)
  );
  PipelinedMulUnit fu_units_1 (
    .clock(fu_units_1_clock),
    .reset(fu_units_1_reset),
    .io_req_valid(fu_units_1_io_req_valid),
    .io_req_bits_uop_valid(fu_units_1_io_req_bits_uop_valid),
    .io_req_bits_uop_iw_state(fu_units_1_io_req_bits_uop_iw_state),
    .io_req_bits_uop_uopc(fu_units_1_io_req_bits_uop_uopc),
    .io_req_bits_uop_inst(fu_units_1_io_req_bits_uop_inst),
    .io_req_bits_uop_pc(fu_units_1_io_req_bits_uop_pc),
    .io_req_bits_uop_iqtype(fu_units_1_io_req_bits_uop_iqtype),
    .io_req_bits_uop_fu_code(fu_units_1_io_req_bits_uop_fu_code),
    .io_req_bits_uop_ctrl_br_type(fu_units_1_io_req_bits_uop_ctrl_br_type),
    .io_req_bits_uop_ctrl_op1_sel(fu_units_1_io_req_bits_uop_ctrl_op1_sel),
    .io_req_bits_uop_ctrl_op2_sel(fu_units_1_io_req_bits_uop_ctrl_op2_sel),
    .io_req_bits_uop_ctrl_imm_sel(fu_units_1_io_req_bits_uop_ctrl_imm_sel),
    .io_req_bits_uop_ctrl_op_fcn(fu_units_1_io_req_bits_uop_ctrl_op_fcn),
    .io_req_bits_uop_ctrl_fcn_dw(fu_units_1_io_req_bits_uop_ctrl_fcn_dw),
    .io_req_bits_uop_ctrl_rf_wen(fu_units_1_io_req_bits_uop_ctrl_rf_wen),
    .io_req_bits_uop_ctrl_csr_cmd(fu_units_1_io_req_bits_uop_ctrl_csr_cmd),
    .io_req_bits_uop_ctrl_is_load(fu_units_1_io_req_bits_uop_ctrl_is_load),
    .io_req_bits_uop_ctrl_is_sta(fu_units_1_io_req_bits_uop_ctrl_is_sta),
    .io_req_bits_uop_ctrl_is_std(fu_units_1_io_req_bits_uop_ctrl_is_std),
    .io_req_bits_uop_allocate_brtag(fu_units_1_io_req_bits_uop_allocate_brtag),
    .io_req_bits_uop_is_br_or_jmp(fu_units_1_io_req_bits_uop_is_br_or_jmp),
    .io_req_bits_uop_is_jump(fu_units_1_io_req_bits_uop_is_jump),
    .io_req_bits_uop_is_jal(fu_units_1_io_req_bits_uop_is_jal),
    .io_req_bits_uop_is_ret(fu_units_1_io_req_bits_uop_is_ret),
    .io_req_bits_uop_is_call(fu_units_1_io_req_bits_uop_is_call),
    .io_req_bits_uop_br_mask(fu_units_1_io_req_bits_uop_br_mask),
    .io_req_bits_uop_br_tag(fu_units_1_io_req_bits_uop_br_tag),
    .io_req_bits_uop_br_prediction_btb_blame(fu_units_1_io_req_bits_uop_br_prediction_btb_blame),
    .io_req_bits_uop_br_prediction_btb_hit(fu_units_1_io_req_bits_uop_br_prediction_btb_hit),
    .io_req_bits_uop_br_prediction_btb_taken(fu_units_1_io_req_bits_uop_br_prediction_btb_taken),
    .io_req_bits_uop_br_prediction_bpd_blame(fu_units_1_io_req_bits_uop_br_prediction_bpd_blame),
    .io_req_bits_uop_br_prediction_bpd_hit(fu_units_1_io_req_bits_uop_br_prediction_bpd_hit),
    .io_req_bits_uop_br_prediction_bpd_taken(fu_units_1_io_req_bits_uop_br_prediction_bpd_taken),
    .io_req_bits_uop_br_prediction_bim_resp_value(fu_units_1_io_req_bits_uop_br_prediction_bim_resp_value),
    .io_req_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_1_io_req_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_req_bits_uop_br_prediction_bim_resp_way_idx(fu_units_1_io_req_bits_uop_br_prediction_bim_resp_way_idx),
    .io_req_bits_uop_br_prediction_bpd_resp_takens(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_takens),
    .io_req_bits_uop_br_prediction_bpd_resp_history(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history),
    .io_req_bits_uop_br_prediction_bpd_resp_history_u(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_u),
    .io_req_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_req_bits_uop_br_prediction_bpd_resp_info(fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_info),
    .io_req_bits_uop_stat_brjmp_mispredicted(fu_units_1_io_req_bits_uop_stat_brjmp_mispredicted),
    .io_req_bits_uop_stat_btb_made_pred(fu_units_1_io_req_bits_uop_stat_btb_made_pred),
    .io_req_bits_uop_stat_btb_mispredicted(fu_units_1_io_req_bits_uop_stat_btb_mispredicted),
    .io_req_bits_uop_stat_bpd_made_pred(fu_units_1_io_req_bits_uop_stat_bpd_made_pred),
    .io_req_bits_uop_stat_bpd_mispredicted(fu_units_1_io_req_bits_uop_stat_bpd_mispredicted),
    .io_req_bits_uop_fetch_pc_lob(fu_units_1_io_req_bits_uop_fetch_pc_lob),
    .io_req_bits_uop_imm_packed(fu_units_1_io_req_bits_uop_imm_packed),
    .io_req_bits_uop_csr_addr(fu_units_1_io_req_bits_uop_csr_addr),
    .io_req_bits_uop_rob_idx(fu_units_1_io_req_bits_uop_rob_idx),
    .io_req_bits_uop_ldq_idx(fu_units_1_io_req_bits_uop_ldq_idx),
    .io_req_bits_uop_stq_idx(fu_units_1_io_req_bits_uop_stq_idx),
    .io_req_bits_uop_brob_idx(fu_units_1_io_req_bits_uop_brob_idx),
    .io_req_bits_uop_pdst(fu_units_1_io_req_bits_uop_pdst),
    .io_req_bits_uop_pop1(fu_units_1_io_req_bits_uop_pop1),
    .io_req_bits_uop_pop2(fu_units_1_io_req_bits_uop_pop2),
    .io_req_bits_uop_pop3(fu_units_1_io_req_bits_uop_pop3),
    .io_req_bits_uop_prs1_busy(fu_units_1_io_req_bits_uop_prs1_busy),
    .io_req_bits_uop_prs2_busy(fu_units_1_io_req_bits_uop_prs2_busy),
    .io_req_bits_uop_prs3_busy(fu_units_1_io_req_bits_uop_prs3_busy),
    .io_req_bits_uop_stale_pdst(fu_units_1_io_req_bits_uop_stale_pdst),
    .io_req_bits_uop_exception(fu_units_1_io_req_bits_uop_exception),
    .io_req_bits_uop_exc_cause(fu_units_1_io_req_bits_uop_exc_cause),
    .io_req_bits_uop_bypassable(fu_units_1_io_req_bits_uop_bypassable),
    .io_req_bits_uop_mem_cmd(fu_units_1_io_req_bits_uop_mem_cmd),
    .io_req_bits_uop_mem_typ(fu_units_1_io_req_bits_uop_mem_typ),
    .io_req_bits_uop_is_fence(fu_units_1_io_req_bits_uop_is_fence),
    .io_req_bits_uop_is_fencei(fu_units_1_io_req_bits_uop_is_fencei),
    .io_req_bits_uop_is_store(fu_units_1_io_req_bits_uop_is_store),
    .io_req_bits_uop_is_amo(fu_units_1_io_req_bits_uop_is_amo),
    .io_req_bits_uop_is_load(fu_units_1_io_req_bits_uop_is_load),
    .io_req_bits_uop_is_sys_pc2epc(fu_units_1_io_req_bits_uop_is_sys_pc2epc),
    .io_req_bits_uop_is_unique(fu_units_1_io_req_bits_uop_is_unique),
    .io_req_bits_uop_flush_on_commit(fu_units_1_io_req_bits_uop_flush_on_commit),
    .io_req_bits_uop_ldst(fu_units_1_io_req_bits_uop_ldst),
    .io_req_bits_uop_lrs1(fu_units_1_io_req_bits_uop_lrs1),
    .io_req_bits_uop_lrs2(fu_units_1_io_req_bits_uop_lrs2),
    .io_req_bits_uop_lrs3(fu_units_1_io_req_bits_uop_lrs3),
    .io_req_bits_uop_ldst_val(fu_units_1_io_req_bits_uop_ldst_val),
    .io_req_bits_uop_dst_rtype(fu_units_1_io_req_bits_uop_dst_rtype),
    .io_req_bits_uop_lrs1_rtype(fu_units_1_io_req_bits_uop_lrs1_rtype),
    .io_req_bits_uop_lrs2_rtype(fu_units_1_io_req_bits_uop_lrs2_rtype),
    .io_req_bits_uop_frs3_en(fu_units_1_io_req_bits_uop_frs3_en),
    .io_req_bits_uop_fp_val(fu_units_1_io_req_bits_uop_fp_val),
    .io_req_bits_uop_fp_single(fu_units_1_io_req_bits_uop_fp_single),
    .io_req_bits_uop_xcpt_pf_if(fu_units_1_io_req_bits_uop_xcpt_pf_if),
    .io_req_bits_uop_xcpt_ae_if(fu_units_1_io_req_bits_uop_xcpt_ae_if),
    .io_req_bits_uop_replay_if(fu_units_1_io_req_bits_uop_replay_if),
    .io_req_bits_uop_xcpt_ma_if(fu_units_1_io_req_bits_uop_xcpt_ma_if),
    .io_req_bits_uop_debug_wdata(fu_units_1_io_req_bits_uop_debug_wdata),
    .io_req_bits_uop_debug_events_fetch_seq(fu_units_1_io_req_bits_uop_debug_events_fetch_seq),
    .io_req_bits_rs1_data(fu_units_1_io_req_bits_rs1_data),
    .io_req_bits_rs2_data(fu_units_1_io_req_bits_rs2_data),
    .io_req_bits_kill(fu_units_1_io_req_bits_kill),
    .io_resp_valid(fu_units_1_io_resp_valid),
    .io_resp_bits_uop_valid(fu_units_1_io_resp_bits_uop_valid),
    .io_resp_bits_uop_iw_state(fu_units_1_io_resp_bits_uop_iw_state),
    .io_resp_bits_uop_uopc(fu_units_1_io_resp_bits_uop_uopc),
    .io_resp_bits_uop_inst(fu_units_1_io_resp_bits_uop_inst),
    .io_resp_bits_uop_pc(fu_units_1_io_resp_bits_uop_pc),
    .io_resp_bits_uop_iqtype(fu_units_1_io_resp_bits_uop_iqtype),
    .io_resp_bits_uop_fu_code(fu_units_1_io_resp_bits_uop_fu_code),
    .io_resp_bits_uop_ctrl_br_type(fu_units_1_io_resp_bits_uop_ctrl_br_type),
    .io_resp_bits_uop_ctrl_op1_sel(fu_units_1_io_resp_bits_uop_ctrl_op1_sel),
    .io_resp_bits_uop_ctrl_op2_sel(fu_units_1_io_resp_bits_uop_ctrl_op2_sel),
    .io_resp_bits_uop_ctrl_imm_sel(fu_units_1_io_resp_bits_uop_ctrl_imm_sel),
    .io_resp_bits_uop_ctrl_op_fcn(fu_units_1_io_resp_bits_uop_ctrl_op_fcn),
    .io_resp_bits_uop_ctrl_fcn_dw(fu_units_1_io_resp_bits_uop_ctrl_fcn_dw),
    .io_resp_bits_uop_ctrl_rf_wen(fu_units_1_io_resp_bits_uop_ctrl_rf_wen),
    .io_resp_bits_uop_ctrl_csr_cmd(fu_units_1_io_resp_bits_uop_ctrl_csr_cmd),
    .io_resp_bits_uop_ctrl_is_load(fu_units_1_io_resp_bits_uop_ctrl_is_load),
    .io_resp_bits_uop_ctrl_is_sta(fu_units_1_io_resp_bits_uop_ctrl_is_sta),
    .io_resp_bits_uop_ctrl_is_std(fu_units_1_io_resp_bits_uop_ctrl_is_std),
    .io_resp_bits_uop_allocate_brtag(fu_units_1_io_resp_bits_uop_allocate_brtag),
    .io_resp_bits_uop_is_br_or_jmp(fu_units_1_io_resp_bits_uop_is_br_or_jmp),
    .io_resp_bits_uop_is_jump(fu_units_1_io_resp_bits_uop_is_jump),
    .io_resp_bits_uop_is_jal(fu_units_1_io_resp_bits_uop_is_jal),
    .io_resp_bits_uop_is_ret(fu_units_1_io_resp_bits_uop_is_ret),
    .io_resp_bits_uop_is_call(fu_units_1_io_resp_bits_uop_is_call),
    .io_resp_bits_uop_br_mask(fu_units_1_io_resp_bits_uop_br_mask),
    .io_resp_bits_uop_br_tag(fu_units_1_io_resp_bits_uop_br_tag),
    .io_resp_bits_uop_br_prediction_btb_blame(fu_units_1_io_resp_bits_uop_br_prediction_btb_blame),
    .io_resp_bits_uop_br_prediction_btb_hit(fu_units_1_io_resp_bits_uop_br_prediction_btb_hit),
    .io_resp_bits_uop_br_prediction_btb_taken(fu_units_1_io_resp_bits_uop_br_prediction_btb_taken),
    .io_resp_bits_uop_br_prediction_bpd_blame(fu_units_1_io_resp_bits_uop_br_prediction_bpd_blame),
    .io_resp_bits_uop_br_prediction_bpd_hit(fu_units_1_io_resp_bits_uop_br_prediction_bpd_hit),
    .io_resp_bits_uop_br_prediction_bpd_taken(fu_units_1_io_resp_bits_uop_br_prediction_bpd_taken),
    .io_resp_bits_uop_br_prediction_bim_resp_value(fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_value),
    .io_resp_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_resp_bits_uop_br_prediction_bim_resp_way_idx(fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_way_idx),
    .io_resp_bits_uop_br_prediction_bpd_resp_takens(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_takens),
    .io_resp_bits_uop_br_prediction_bpd_resp_history(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_u(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_u),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_resp_bits_uop_br_prediction_bpd_resp_info(fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_info),
    .io_resp_bits_uop_stat_brjmp_mispredicted(fu_units_1_io_resp_bits_uop_stat_brjmp_mispredicted),
    .io_resp_bits_uop_stat_btb_made_pred(fu_units_1_io_resp_bits_uop_stat_btb_made_pred),
    .io_resp_bits_uop_stat_btb_mispredicted(fu_units_1_io_resp_bits_uop_stat_btb_mispredicted),
    .io_resp_bits_uop_stat_bpd_made_pred(fu_units_1_io_resp_bits_uop_stat_bpd_made_pred),
    .io_resp_bits_uop_stat_bpd_mispredicted(fu_units_1_io_resp_bits_uop_stat_bpd_mispredicted),
    .io_resp_bits_uop_fetch_pc_lob(fu_units_1_io_resp_bits_uop_fetch_pc_lob),
    .io_resp_bits_uop_imm_packed(fu_units_1_io_resp_bits_uop_imm_packed),
    .io_resp_bits_uop_csr_addr(fu_units_1_io_resp_bits_uop_csr_addr),
    .io_resp_bits_uop_rob_idx(fu_units_1_io_resp_bits_uop_rob_idx),
    .io_resp_bits_uop_ldq_idx(fu_units_1_io_resp_bits_uop_ldq_idx),
    .io_resp_bits_uop_stq_idx(fu_units_1_io_resp_bits_uop_stq_idx),
    .io_resp_bits_uop_brob_idx(fu_units_1_io_resp_bits_uop_brob_idx),
    .io_resp_bits_uop_pdst(fu_units_1_io_resp_bits_uop_pdst),
    .io_resp_bits_uop_pop1(fu_units_1_io_resp_bits_uop_pop1),
    .io_resp_bits_uop_pop2(fu_units_1_io_resp_bits_uop_pop2),
    .io_resp_bits_uop_pop3(fu_units_1_io_resp_bits_uop_pop3),
    .io_resp_bits_uop_prs1_busy(fu_units_1_io_resp_bits_uop_prs1_busy),
    .io_resp_bits_uop_prs2_busy(fu_units_1_io_resp_bits_uop_prs2_busy),
    .io_resp_bits_uop_prs3_busy(fu_units_1_io_resp_bits_uop_prs3_busy),
    .io_resp_bits_uop_stale_pdst(fu_units_1_io_resp_bits_uop_stale_pdst),
    .io_resp_bits_uop_exception(fu_units_1_io_resp_bits_uop_exception),
    .io_resp_bits_uop_exc_cause(fu_units_1_io_resp_bits_uop_exc_cause),
    .io_resp_bits_uop_bypassable(fu_units_1_io_resp_bits_uop_bypassable),
    .io_resp_bits_uop_mem_cmd(fu_units_1_io_resp_bits_uop_mem_cmd),
    .io_resp_bits_uop_mem_typ(fu_units_1_io_resp_bits_uop_mem_typ),
    .io_resp_bits_uop_is_fence(fu_units_1_io_resp_bits_uop_is_fence),
    .io_resp_bits_uop_is_fencei(fu_units_1_io_resp_bits_uop_is_fencei),
    .io_resp_bits_uop_is_store(fu_units_1_io_resp_bits_uop_is_store),
    .io_resp_bits_uop_is_amo(fu_units_1_io_resp_bits_uop_is_amo),
    .io_resp_bits_uop_is_load(fu_units_1_io_resp_bits_uop_is_load),
    .io_resp_bits_uop_is_sys_pc2epc(fu_units_1_io_resp_bits_uop_is_sys_pc2epc),
    .io_resp_bits_uop_is_unique(fu_units_1_io_resp_bits_uop_is_unique),
    .io_resp_bits_uop_flush_on_commit(fu_units_1_io_resp_bits_uop_flush_on_commit),
    .io_resp_bits_uop_ldst(fu_units_1_io_resp_bits_uop_ldst),
    .io_resp_bits_uop_lrs1(fu_units_1_io_resp_bits_uop_lrs1),
    .io_resp_bits_uop_lrs2(fu_units_1_io_resp_bits_uop_lrs2),
    .io_resp_bits_uop_lrs3(fu_units_1_io_resp_bits_uop_lrs3),
    .io_resp_bits_uop_ldst_val(fu_units_1_io_resp_bits_uop_ldst_val),
    .io_resp_bits_uop_dst_rtype(fu_units_1_io_resp_bits_uop_dst_rtype),
    .io_resp_bits_uop_lrs1_rtype(fu_units_1_io_resp_bits_uop_lrs1_rtype),
    .io_resp_bits_uop_lrs2_rtype(fu_units_1_io_resp_bits_uop_lrs2_rtype),
    .io_resp_bits_uop_frs3_en(fu_units_1_io_resp_bits_uop_frs3_en),
    .io_resp_bits_uop_fp_val(fu_units_1_io_resp_bits_uop_fp_val),
    .io_resp_bits_uop_fp_single(fu_units_1_io_resp_bits_uop_fp_single),
    .io_resp_bits_uop_xcpt_pf_if(fu_units_1_io_resp_bits_uop_xcpt_pf_if),
    .io_resp_bits_uop_xcpt_ae_if(fu_units_1_io_resp_bits_uop_xcpt_ae_if),
    .io_resp_bits_uop_replay_if(fu_units_1_io_resp_bits_uop_replay_if),
    .io_resp_bits_uop_xcpt_ma_if(fu_units_1_io_resp_bits_uop_xcpt_ma_if),
    .io_resp_bits_uop_debug_wdata(fu_units_1_io_resp_bits_uop_debug_wdata),
    .io_resp_bits_uop_debug_events_fetch_seq(fu_units_1_io_resp_bits_uop_debug_events_fetch_seq),
    .io_resp_bits_data(fu_units_1_io_resp_bits_data),
    .io_brinfo_valid(fu_units_1_io_brinfo_valid),
    .io_brinfo_mispredict(fu_units_1_io_brinfo_mispredict),
    .io_brinfo_mask(fu_units_1_io_brinfo_mask)
  );
  MulDivUnit fu_units_2 (
    .clock(fu_units_2_clock),
    .reset(fu_units_2_reset),
    .io_req_ready(fu_units_2_io_req_ready),
    .io_req_valid(fu_units_2_io_req_valid),
    .io_req_bits_uop_valid(fu_units_2_io_req_bits_uop_valid),
    .io_req_bits_uop_iw_state(fu_units_2_io_req_bits_uop_iw_state),
    .io_req_bits_uop_uopc(fu_units_2_io_req_bits_uop_uopc),
    .io_req_bits_uop_inst(fu_units_2_io_req_bits_uop_inst),
    .io_req_bits_uop_pc(fu_units_2_io_req_bits_uop_pc),
    .io_req_bits_uop_iqtype(fu_units_2_io_req_bits_uop_iqtype),
    .io_req_bits_uop_fu_code(fu_units_2_io_req_bits_uop_fu_code),
    .io_req_bits_uop_ctrl_br_type(fu_units_2_io_req_bits_uop_ctrl_br_type),
    .io_req_bits_uop_ctrl_op1_sel(fu_units_2_io_req_bits_uop_ctrl_op1_sel),
    .io_req_bits_uop_ctrl_op2_sel(fu_units_2_io_req_bits_uop_ctrl_op2_sel),
    .io_req_bits_uop_ctrl_imm_sel(fu_units_2_io_req_bits_uop_ctrl_imm_sel),
    .io_req_bits_uop_ctrl_op_fcn(fu_units_2_io_req_bits_uop_ctrl_op_fcn),
    .io_req_bits_uop_ctrl_fcn_dw(fu_units_2_io_req_bits_uop_ctrl_fcn_dw),
    .io_req_bits_uop_ctrl_rf_wen(fu_units_2_io_req_bits_uop_ctrl_rf_wen),
    .io_req_bits_uop_ctrl_csr_cmd(fu_units_2_io_req_bits_uop_ctrl_csr_cmd),
    .io_req_bits_uop_ctrl_is_load(fu_units_2_io_req_bits_uop_ctrl_is_load),
    .io_req_bits_uop_ctrl_is_sta(fu_units_2_io_req_bits_uop_ctrl_is_sta),
    .io_req_bits_uop_ctrl_is_std(fu_units_2_io_req_bits_uop_ctrl_is_std),
    .io_req_bits_uop_allocate_brtag(fu_units_2_io_req_bits_uop_allocate_brtag),
    .io_req_bits_uop_is_br_or_jmp(fu_units_2_io_req_bits_uop_is_br_or_jmp),
    .io_req_bits_uop_is_jump(fu_units_2_io_req_bits_uop_is_jump),
    .io_req_bits_uop_is_jal(fu_units_2_io_req_bits_uop_is_jal),
    .io_req_bits_uop_is_ret(fu_units_2_io_req_bits_uop_is_ret),
    .io_req_bits_uop_is_call(fu_units_2_io_req_bits_uop_is_call),
    .io_req_bits_uop_br_mask(fu_units_2_io_req_bits_uop_br_mask),
    .io_req_bits_uop_br_tag(fu_units_2_io_req_bits_uop_br_tag),
    .io_req_bits_uop_br_prediction_btb_blame(fu_units_2_io_req_bits_uop_br_prediction_btb_blame),
    .io_req_bits_uop_br_prediction_btb_hit(fu_units_2_io_req_bits_uop_br_prediction_btb_hit),
    .io_req_bits_uop_br_prediction_btb_taken(fu_units_2_io_req_bits_uop_br_prediction_btb_taken),
    .io_req_bits_uop_br_prediction_bpd_blame(fu_units_2_io_req_bits_uop_br_prediction_bpd_blame),
    .io_req_bits_uop_br_prediction_bpd_hit(fu_units_2_io_req_bits_uop_br_prediction_bpd_hit),
    .io_req_bits_uop_br_prediction_bpd_taken(fu_units_2_io_req_bits_uop_br_prediction_bpd_taken),
    .io_req_bits_uop_br_prediction_bim_resp_value(fu_units_2_io_req_bits_uop_br_prediction_bim_resp_value),
    .io_req_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_2_io_req_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_req_bits_uop_br_prediction_bim_resp_way_idx(fu_units_2_io_req_bits_uop_br_prediction_bim_resp_way_idx),
    .io_req_bits_uop_br_prediction_bpd_resp_takens(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_takens),
    .io_req_bits_uop_br_prediction_bpd_resp_history(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history),
    .io_req_bits_uop_br_prediction_bpd_resp_history_u(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_u),
    .io_req_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_req_bits_uop_br_prediction_bpd_resp_info(fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_info),
    .io_req_bits_uop_stat_brjmp_mispredicted(fu_units_2_io_req_bits_uop_stat_brjmp_mispredicted),
    .io_req_bits_uop_stat_btb_made_pred(fu_units_2_io_req_bits_uop_stat_btb_made_pred),
    .io_req_bits_uop_stat_btb_mispredicted(fu_units_2_io_req_bits_uop_stat_btb_mispredicted),
    .io_req_bits_uop_stat_bpd_made_pred(fu_units_2_io_req_bits_uop_stat_bpd_made_pred),
    .io_req_bits_uop_stat_bpd_mispredicted(fu_units_2_io_req_bits_uop_stat_bpd_mispredicted),
    .io_req_bits_uop_fetch_pc_lob(fu_units_2_io_req_bits_uop_fetch_pc_lob),
    .io_req_bits_uop_imm_packed(fu_units_2_io_req_bits_uop_imm_packed),
    .io_req_bits_uop_csr_addr(fu_units_2_io_req_bits_uop_csr_addr),
    .io_req_bits_uop_rob_idx(fu_units_2_io_req_bits_uop_rob_idx),
    .io_req_bits_uop_ldq_idx(fu_units_2_io_req_bits_uop_ldq_idx),
    .io_req_bits_uop_stq_idx(fu_units_2_io_req_bits_uop_stq_idx),
    .io_req_bits_uop_brob_idx(fu_units_2_io_req_bits_uop_brob_idx),
    .io_req_bits_uop_pdst(fu_units_2_io_req_bits_uop_pdst),
    .io_req_bits_uop_pop1(fu_units_2_io_req_bits_uop_pop1),
    .io_req_bits_uop_pop2(fu_units_2_io_req_bits_uop_pop2),
    .io_req_bits_uop_pop3(fu_units_2_io_req_bits_uop_pop3),
    .io_req_bits_uop_prs1_busy(fu_units_2_io_req_bits_uop_prs1_busy),
    .io_req_bits_uop_prs2_busy(fu_units_2_io_req_bits_uop_prs2_busy),
    .io_req_bits_uop_prs3_busy(fu_units_2_io_req_bits_uop_prs3_busy),
    .io_req_bits_uop_stale_pdst(fu_units_2_io_req_bits_uop_stale_pdst),
    .io_req_bits_uop_exception(fu_units_2_io_req_bits_uop_exception),
    .io_req_bits_uop_exc_cause(fu_units_2_io_req_bits_uop_exc_cause),
    .io_req_bits_uop_bypassable(fu_units_2_io_req_bits_uop_bypassable),
    .io_req_bits_uop_mem_cmd(fu_units_2_io_req_bits_uop_mem_cmd),
    .io_req_bits_uop_mem_typ(fu_units_2_io_req_bits_uop_mem_typ),
    .io_req_bits_uop_is_fence(fu_units_2_io_req_bits_uop_is_fence),
    .io_req_bits_uop_is_fencei(fu_units_2_io_req_bits_uop_is_fencei),
    .io_req_bits_uop_is_store(fu_units_2_io_req_bits_uop_is_store),
    .io_req_bits_uop_is_amo(fu_units_2_io_req_bits_uop_is_amo),
    .io_req_bits_uop_is_load(fu_units_2_io_req_bits_uop_is_load),
    .io_req_bits_uop_is_sys_pc2epc(fu_units_2_io_req_bits_uop_is_sys_pc2epc),
    .io_req_bits_uop_is_unique(fu_units_2_io_req_bits_uop_is_unique),
    .io_req_bits_uop_flush_on_commit(fu_units_2_io_req_bits_uop_flush_on_commit),
    .io_req_bits_uop_ldst(fu_units_2_io_req_bits_uop_ldst),
    .io_req_bits_uop_lrs1(fu_units_2_io_req_bits_uop_lrs1),
    .io_req_bits_uop_lrs2(fu_units_2_io_req_bits_uop_lrs2),
    .io_req_bits_uop_lrs3(fu_units_2_io_req_bits_uop_lrs3),
    .io_req_bits_uop_ldst_val(fu_units_2_io_req_bits_uop_ldst_val),
    .io_req_bits_uop_dst_rtype(fu_units_2_io_req_bits_uop_dst_rtype),
    .io_req_bits_uop_lrs1_rtype(fu_units_2_io_req_bits_uop_lrs1_rtype),
    .io_req_bits_uop_lrs2_rtype(fu_units_2_io_req_bits_uop_lrs2_rtype),
    .io_req_bits_uop_frs3_en(fu_units_2_io_req_bits_uop_frs3_en),
    .io_req_bits_uop_fp_val(fu_units_2_io_req_bits_uop_fp_val),
    .io_req_bits_uop_fp_single(fu_units_2_io_req_bits_uop_fp_single),
    .io_req_bits_uop_xcpt_pf_if(fu_units_2_io_req_bits_uop_xcpt_pf_if),
    .io_req_bits_uop_xcpt_ae_if(fu_units_2_io_req_bits_uop_xcpt_ae_if),
    .io_req_bits_uop_replay_if(fu_units_2_io_req_bits_uop_replay_if),
    .io_req_bits_uop_xcpt_ma_if(fu_units_2_io_req_bits_uop_xcpt_ma_if),
    .io_req_bits_uop_debug_wdata(fu_units_2_io_req_bits_uop_debug_wdata),
    .io_req_bits_uop_debug_events_fetch_seq(fu_units_2_io_req_bits_uop_debug_events_fetch_seq),
    .io_req_bits_rs1_data(fu_units_2_io_req_bits_rs1_data),
    .io_req_bits_rs2_data(fu_units_2_io_req_bits_rs2_data),
    .io_req_bits_kill(fu_units_2_io_req_bits_kill),
    .io_resp_ready(fu_units_2_io_resp_ready),
    .io_resp_valid(fu_units_2_io_resp_valid),
    .io_resp_bits_uop_valid(fu_units_2_io_resp_bits_uop_valid),
    .io_resp_bits_uop_iw_state(fu_units_2_io_resp_bits_uop_iw_state),
    .io_resp_bits_uop_uopc(fu_units_2_io_resp_bits_uop_uopc),
    .io_resp_bits_uop_inst(fu_units_2_io_resp_bits_uop_inst),
    .io_resp_bits_uop_pc(fu_units_2_io_resp_bits_uop_pc),
    .io_resp_bits_uop_iqtype(fu_units_2_io_resp_bits_uop_iqtype),
    .io_resp_bits_uop_fu_code(fu_units_2_io_resp_bits_uop_fu_code),
    .io_resp_bits_uop_ctrl_br_type(fu_units_2_io_resp_bits_uop_ctrl_br_type),
    .io_resp_bits_uop_ctrl_op1_sel(fu_units_2_io_resp_bits_uop_ctrl_op1_sel),
    .io_resp_bits_uop_ctrl_op2_sel(fu_units_2_io_resp_bits_uop_ctrl_op2_sel),
    .io_resp_bits_uop_ctrl_imm_sel(fu_units_2_io_resp_bits_uop_ctrl_imm_sel),
    .io_resp_bits_uop_ctrl_op_fcn(fu_units_2_io_resp_bits_uop_ctrl_op_fcn),
    .io_resp_bits_uop_ctrl_fcn_dw(fu_units_2_io_resp_bits_uop_ctrl_fcn_dw),
    .io_resp_bits_uop_ctrl_rf_wen(fu_units_2_io_resp_bits_uop_ctrl_rf_wen),
    .io_resp_bits_uop_ctrl_csr_cmd(fu_units_2_io_resp_bits_uop_ctrl_csr_cmd),
    .io_resp_bits_uop_ctrl_is_load(fu_units_2_io_resp_bits_uop_ctrl_is_load),
    .io_resp_bits_uop_ctrl_is_sta(fu_units_2_io_resp_bits_uop_ctrl_is_sta),
    .io_resp_bits_uop_ctrl_is_std(fu_units_2_io_resp_bits_uop_ctrl_is_std),
    .io_resp_bits_uop_allocate_brtag(fu_units_2_io_resp_bits_uop_allocate_brtag),
    .io_resp_bits_uop_is_br_or_jmp(fu_units_2_io_resp_bits_uop_is_br_or_jmp),
    .io_resp_bits_uop_is_jump(fu_units_2_io_resp_bits_uop_is_jump),
    .io_resp_bits_uop_is_jal(fu_units_2_io_resp_bits_uop_is_jal),
    .io_resp_bits_uop_is_ret(fu_units_2_io_resp_bits_uop_is_ret),
    .io_resp_bits_uop_is_call(fu_units_2_io_resp_bits_uop_is_call),
    .io_resp_bits_uop_br_mask(fu_units_2_io_resp_bits_uop_br_mask),
    .io_resp_bits_uop_br_tag(fu_units_2_io_resp_bits_uop_br_tag),
    .io_resp_bits_uop_br_prediction_btb_blame(fu_units_2_io_resp_bits_uop_br_prediction_btb_blame),
    .io_resp_bits_uop_br_prediction_btb_hit(fu_units_2_io_resp_bits_uop_br_prediction_btb_hit),
    .io_resp_bits_uop_br_prediction_btb_taken(fu_units_2_io_resp_bits_uop_br_prediction_btb_taken),
    .io_resp_bits_uop_br_prediction_bpd_blame(fu_units_2_io_resp_bits_uop_br_prediction_bpd_blame),
    .io_resp_bits_uop_br_prediction_bpd_hit(fu_units_2_io_resp_bits_uop_br_prediction_bpd_hit),
    .io_resp_bits_uop_br_prediction_bpd_taken(fu_units_2_io_resp_bits_uop_br_prediction_bpd_taken),
    .io_resp_bits_uop_br_prediction_bim_resp_value(fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_value),
    .io_resp_bits_uop_br_prediction_bim_resp_entry_idx(fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_entry_idx),
    .io_resp_bits_uop_br_prediction_bim_resp_way_idx(fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_way_idx),
    .io_resp_bits_uop_br_prediction_bpd_resp_takens(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_takens),
    .io_resp_bits_uop_br_prediction_bpd_resp_history(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_u(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_u),
    .io_resp_bits_uop_br_prediction_bpd_resp_history_ptr(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr),
    .io_resp_bits_uop_br_prediction_bpd_resp_info(fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_info),
    .io_resp_bits_uop_stat_brjmp_mispredicted(fu_units_2_io_resp_bits_uop_stat_brjmp_mispredicted),
    .io_resp_bits_uop_stat_btb_made_pred(fu_units_2_io_resp_bits_uop_stat_btb_made_pred),
    .io_resp_bits_uop_stat_btb_mispredicted(fu_units_2_io_resp_bits_uop_stat_btb_mispredicted),
    .io_resp_bits_uop_stat_bpd_made_pred(fu_units_2_io_resp_bits_uop_stat_bpd_made_pred),
    .io_resp_bits_uop_stat_bpd_mispredicted(fu_units_2_io_resp_bits_uop_stat_bpd_mispredicted),
    .io_resp_bits_uop_fetch_pc_lob(fu_units_2_io_resp_bits_uop_fetch_pc_lob),
    .io_resp_bits_uop_imm_packed(fu_units_2_io_resp_bits_uop_imm_packed),
    .io_resp_bits_uop_csr_addr(fu_units_2_io_resp_bits_uop_csr_addr),
    .io_resp_bits_uop_rob_idx(fu_units_2_io_resp_bits_uop_rob_idx),
    .io_resp_bits_uop_ldq_idx(fu_units_2_io_resp_bits_uop_ldq_idx),
    .io_resp_bits_uop_stq_idx(fu_units_2_io_resp_bits_uop_stq_idx),
    .io_resp_bits_uop_brob_idx(fu_units_2_io_resp_bits_uop_brob_idx),
    .io_resp_bits_uop_pdst(fu_units_2_io_resp_bits_uop_pdst),
    .io_resp_bits_uop_pop1(fu_units_2_io_resp_bits_uop_pop1),
    .io_resp_bits_uop_pop2(fu_units_2_io_resp_bits_uop_pop2),
    .io_resp_bits_uop_pop3(fu_units_2_io_resp_bits_uop_pop3),
    .io_resp_bits_uop_prs1_busy(fu_units_2_io_resp_bits_uop_prs1_busy),
    .io_resp_bits_uop_prs2_busy(fu_units_2_io_resp_bits_uop_prs2_busy),
    .io_resp_bits_uop_prs3_busy(fu_units_2_io_resp_bits_uop_prs3_busy),
    .io_resp_bits_uop_stale_pdst(fu_units_2_io_resp_bits_uop_stale_pdst),
    .io_resp_bits_uop_exception(fu_units_2_io_resp_bits_uop_exception),
    .io_resp_bits_uop_exc_cause(fu_units_2_io_resp_bits_uop_exc_cause),
    .io_resp_bits_uop_bypassable(fu_units_2_io_resp_bits_uop_bypassable),
    .io_resp_bits_uop_mem_cmd(fu_units_2_io_resp_bits_uop_mem_cmd),
    .io_resp_bits_uop_mem_typ(fu_units_2_io_resp_bits_uop_mem_typ),
    .io_resp_bits_uop_is_fence(fu_units_2_io_resp_bits_uop_is_fence),
    .io_resp_bits_uop_is_fencei(fu_units_2_io_resp_bits_uop_is_fencei),
    .io_resp_bits_uop_is_store(fu_units_2_io_resp_bits_uop_is_store),
    .io_resp_bits_uop_is_amo(fu_units_2_io_resp_bits_uop_is_amo),
    .io_resp_bits_uop_is_load(fu_units_2_io_resp_bits_uop_is_load),
    .io_resp_bits_uop_is_sys_pc2epc(fu_units_2_io_resp_bits_uop_is_sys_pc2epc),
    .io_resp_bits_uop_is_unique(fu_units_2_io_resp_bits_uop_is_unique),
    .io_resp_bits_uop_flush_on_commit(fu_units_2_io_resp_bits_uop_flush_on_commit),
    .io_resp_bits_uop_ldst(fu_units_2_io_resp_bits_uop_ldst),
    .io_resp_bits_uop_lrs1(fu_units_2_io_resp_bits_uop_lrs1),
    .io_resp_bits_uop_lrs2(fu_units_2_io_resp_bits_uop_lrs2),
    .io_resp_bits_uop_lrs3(fu_units_2_io_resp_bits_uop_lrs3),
    .io_resp_bits_uop_ldst_val(fu_units_2_io_resp_bits_uop_ldst_val),
    .io_resp_bits_uop_dst_rtype(fu_units_2_io_resp_bits_uop_dst_rtype),
    .io_resp_bits_uop_lrs1_rtype(fu_units_2_io_resp_bits_uop_lrs1_rtype),
    .io_resp_bits_uop_lrs2_rtype(fu_units_2_io_resp_bits_uop_lrs2_rtype),
    .io_resp_bits_uop_frs3_en(fu_units_2_io_resp_bits_uop_frs3_en),
    .io_resp_bits_uop_fp_val(fu_units_2_io_resp_bits_uop_fp_val),
    .io_resp_bits_uop_fp_single(fu_units_2_io_resp_bits_uop_fp_single),
    .io_resp_bits_uop_xcpt_pf_if(fu_units_2_io_resp_bits_uop_xcpt_pf_if),
    .io_resp_bits_uop_xcpt_ae_if(fu_units_2_io_resp_bits_uop_xcpt_ae_if),
    .io_resp_bits_uop_replay_if(fu_units_2_io_resp_bits_uop_replay_if),
    .io_resp_bits_uop_xcpt_ma_if(fu_units_2_io_resp_bits_uop_xcpt_ma_if),
    .io_resp_bits_uop_debug_wdata(fu_units_2_io_resp_bits_uop_debug_wdata),
    .io_resp_bits_uop_debug_events_fetch_seq(fu_units_2_io_resp_bits_uop_debug_events_fetch_seq),
    .io_resp_bits_data(fu_units_2_io_resp_bits_data),
    .io_brinfo_valid(fu_units_2_io_brinfo_valid),
    .io_brinfo_mispredict(fu_units_2_io_brinfo_mispredict),
    .io_brinfo_mask(fu_units_2_io_brinfo_mask)
  );
  assign _T_1210 = muldiv_busy == 1'h0;
  assign _T_1221 = _T_1210 ? 10'h10 : 10'h0;
  assign _T_1222 = 10'h9 | _T_1221;
  assign _T_1226 = _T_1222 | 10'h20;
  assign _T_1230 = _T_1226 | 10'h2;
  assign _T_1238 = io_req_bits_uop_fu_code == 10'h1;
  assign _T_1239 = io_req_bits_uop_fu_code == 10'h2;
  assign _T_1240 = _T_1238 | _T_1239;
  assign _T_1241 = io_req_bits_uop_fu_code == 10'h20;
  assign _T_1242 = _T_1240 | _T_1241;
  assign _T_1243 = io_req_valid & _T_1242;
  assign _T_1244 = io_req_bits_uop_fu_code == 10'h8;
  assign _T_1245 = io_req_valid & _T_1244;
  assign _T_1326 = io_req_bits_uop_fu_code == 10'h10;
  assign _T_1333 = io_req_valid & _T_1326;
  assign _T_1334 = alu_io_resp_valid | fu_units_1_io_resp_valid;
  assign _T_1336 = _T_1334 == 1'h0;
  assign _T_1338 = fu_units_2_io_req_ready == 1'h0;
  assign _T_1345 = _T_1338 | _T_1333;
  assign _T_1351 = _T_1334 | fu_units_2_io_resp_valid;
  assign _T_1357 = {alu_io_resp_bits_uop_debug_wdata,alu_io_resp_bits_uop_debug_events_fetch_seq};
  assign _T_1358 = {alu_io_resp_bits_uop_xcpt_ae_if,alu_io_resp_bits_uop_replay_if};
  assign _T_1359 = {_T_1358,alu_io_resp_bits_uop_xcpt_ma_if};
  assign _T_1360 = {_T_1359,_T_1357};
  assign _T_1361 = {alu_io_resp_bits_uop_fp_val,alu_io_resp_bits_uop_fp_single};
  assign _T_1362 = {_T_1361,alu_io_resp_bits_uop_xcpt_pf_if};
  assign _T_1363 = {alu_io_resp_bits_uop_lrs1_rtype,alu_io_resp_bits_uop_lrs2_rtype};
  assign _T_1364 = {_T_1363,alu_io_resp_bits_uop_frs3_en};
  assign _T_1365 = {_T_1364,_T_1362};
  assign _T_1366 = {_T_1365,_T_1360};
  assign _T_1367 = {alu_io_resp_bits_uop_ldst_val,alu_io_resp_bits_uop_dst_rtype};
  assign _T_1368 = {alu_io_resp_bits_uop_lrs1,alu_io_resp_bits_uop_lrs2};
  assign _T_1369 = {_T_1368,alu_io_resp_bits_uop_lrs3};
  assign _T_1370 = {_T_1369,_T_1367};
  assign _T_1371 = {alu_io_resp_bits_uop_is_unique,alu_io_resp_bits_uop_flush_on_commit};
  assign _T_1372 = {_T_1371,alu_io_resp_bits_uop_ldst};
  assign _T_1373 = {alu_io_resp_bits_uop_is_amo,alu_io_resp_bits_uop_is_load};
  assign _T_1374 = {_T_1373,alu_io_resp_bits_uop_is_sys_pc2epc};
  assign _T_1375 = {_T_1374,_T_1372};
  assign _T_1376 = {_T_1375,_T_1370};
  assign _T_1377 = {_T_1376,_T_1366};
  assign _T_1378 = {alu_io_resp_bits_uop_is_fencei,alu_io_resp_bits_uop_is_store};
  assign _T_1379 = {alu_io_resp_bits_uop_mem_cmd,alu_io_resp_bits_uop_mem_typ};
  assign _T_1380 = {_T_1379,alu_io_resp_bits_uop_is_fence};
  assign _T_1381 = {_T_1380,_T_1378};
  assign _T_1382 = {alu_io_resp_bits_uop_exception,alu_io_resp_bits_uop_exc_cause};
  assign _T_1383 = {_T_1382,alu_io_resp_bits_uop_bypassable};
  assign _T_1384 = {alu_io_resp_bits_uop_prs2_busy,alu_io_resp_bits_uop_prs3_busy};
  assign _T_1385 = {_T_1384,alu_io_resp_bits_uop_stale_pdst};
  assign _T_1386 = {_T_1385,_T_1383};
  assign _T_1387 = {_T_1386,_T_1381};
  assign _T_1388 = {alu_io_resp_bits_uop_pop2,alu_io_resp_bits_uop_pop3};
  assign _T_1389 = {_T_1388,alu_io_resp_bits_uop_prs1_busy};
  assign _T_1390 = {alu_io_resp_bits_uop_brob_idx,alu_io_resp_bits_uop_pdst};
  assign _T_1391 = {_T_1390,alu_io_resp_bits_uop_pop1};
  assign _T_1392 = {_T_1391,_T_1389};
  assign _T_1393 = {alu_io_resp_bits_uop_rob_idx,alu_io_resp_bits_uop_ldq_idx};
  assign _T_1394 = {_T_1393,alu_io_resp_bits_uop_stq_idx};
  assign _T_1395 = {alu_io_resp_bits_uop_fetch_pc_lob,alu_io_resp_bits_uop_imm_packed};
  assign _T_1396 = {_T_1395,alu_io_resp_bits_uop_csr_addr};
  assign _T_1397 = {_T_1396,_T_1394};
  assign _T_1398 = {_T_1397,_T_1392};
  assign _T_1399 = {_T_1398,_T_1387};
  assign _T_1400 = {_T_1399,_T_1377};
  assign _T_1401 = {alu_io_resp_bits_uop_stat_bpd_made_pred,alu_io_resp_bits_uop_stat_bpd_mispredicted};
  assign _T_1402 = {alu_io_resp_bits_uop_stat_brjmp_mispredicted,alu_io_resp_bits_uop_stat_btb_made_pred};
  assign _T_1403 = {_T_1402,alu_io_resp_bits_uop_stat_btb_mispredicted};
  assign _T_1404 = {_T_1403,_T_1401};
  assign _T_1405 = {alu_io_resp_bits_uop_br_prediction_bpd_resp_history_u,alu_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr};
  assign _T_1406 = {_T_1405,alu_io_resp_bits_uop_br_prediction_bpd_resp_info};
  assign _T_1407 = {alu_io_resp_bits_uop_br_prediction_bim_resp_way_idx,alu_io_resp_bits_uop_br_prediction_bpd_resp_takens};
  assign _T_1408 = {_T_1407,alu_io_resp_bits_uop_br_prediction_bpd_resp_history};
  assign _T_1409 = {_T_1408,_T_1406};
  assign _T_1410 = {_T_1409,_T_1404};
  assign _T_1411 = {alu_io_resp_bits_uop_br_prediction_bim_resp_value,alu_io_resp_bits_uop_br_prediction_bim_resp_entry_idx};
  assign _T_1412 = {alu_io_resp_bits_uop_br_prediction_bpd_blame,alu_io_resp_bits_uop_br_prediction_bpd_hit};
  assign _T_1413 = {_T_1412,alu_io_resp_bits_uop_br_prediction_bpd_taken};
  assign _T_1414 = {_T_1413,_T_1411};
  assign _T_1415 = {alu_io_resp_bits_uop_br_prediction_btb_blame,alu_io_resp_bits_uop_br_prediction_btb_hit};
  assign _T_1416 = {_T_1415,alu_io_resp_bits_uop_br_prediction_btb_taken};
  assign _T_1417 = {alu_io_resp_bits_uop_is_call,alu_io_resp_bits_uop_br_mask};
  assign _T_1418 = {_T_1417,alu_io_resp_bits_uop_br_tag};
  assign _T_1419 = {_T_1418,_T_1416};
  assign _T_1420 = {_T_1419,_T_1414};
  assign _T_1421 = {_T_1420,_T_1410};
  assign _T_1422 = {alu_io_resp_bits_uop_is_jal,alu_io_resp_bits_uop_is_ret};
  assign _T_1423 = {alu_io_resp_bits_uop_allocate_brtag,alu_io_resp_bits_uop_is_br_or_jmp};
  assign _T_1424 = {_T_1423,alu_io_resp_bits_uop_is_jump};
  assign _T_1425 = {_T_1424,_T_1422};
  assign _T_1426 = {alu_io_resp_bits_uop_ctrl_is_load,alu_io_resp_bits_uop_ctrl_is_sta};
  assign _T_1427 = {_T_1426,alu_io_resp_bits_uop_ctrl_is_std};
  assign _T_1428 = {alu_io_resp_bits_uop_ctrl_fcn_dw,alu_io_resp_bits_uop_ctrl_rf_wen};
  assign _T_1429 = {_T_1428,alu_io_resp_bits_uop_ctrl_csr_cmd};
  assign _T_1430 = {_T_1429,_T_1427};
  assign _T_1431 = {_T_1430,_T_1425};
  assign _T_1432 = {alu_io_resp_bits_uop_ctrl_op2_sel,alu_io_resp_bits_uop_ctrl_imm_sel};
  assign _T_1433 = {_T_1432,alu_io_resp_bits_uop_ctrl_op_fcn};
  assign _T_1434 = {alu_io_resp_bits_uop_fu_code,alu_io_resp_bits_uop_ctrl_br_type};
  assign _T_1435 = {_T_1434,alu_io_resp_bits_uop_ctrl_op1_sel};
  assign _T_1436 = {_T_1435,_T_1433};
  assign _T_1437 = {alu_io_resp_bits_uop_inst,alu_io_resp_bits_uop_pc};
  assign _T_1438 = {_T_1437,alu_io_resp_bits_uop_iqtype};
  assign _T_1439 = {alu_io_resp_bits_uop_valid,alu_io_resp_bits_uop_iw_state};
  assign _T_1440 = {_T_1439,alu_io_resp_bits_uop_uopc};
  assign _T_1441 = {_T_1440,_T_1438};
  assign _T_1442 = {_T_1441,_T_1436};
  assign _T_1443 = {_T_1442,_T_1431};
  assign _T_1444 = {_T_1443,_T_1421};
  assign _T_1445 = {_T_1444,_T_1400};
  assign _T_1446 = {fu_units_1_io_resp_bits_uop_debug_wdata,fu_units_1_io_resp_bits_uop_debug_events_fetch_seq};
  assign _T_1447 = {fu_units_1_io_resp_bits_uop_xcpt_ae_if,fu_units_1_io_resp_bits_uop_replay_if};
  assign _T_1448 = {_T_1447,fu_units_1_io_resp_bits_uop_xcpt_ma_if};
  assign _T_1449 = {_T_1448,_T_1446};
  assign _T_1450 = {fu_units_1_io_resp_bits_uop_fp_val,fu_units_1_io_resp_bits_uop_fp_single};
  assign _T_1451 = {_T_1450,fu_units_1_io_resp_bits_uop_xcpt_pf_if};
  assign _T_1452 = {fu_units_1_io_resp_bits_uop_lrs1_rtype,fu_units_1_io_resp_bits_uop_lrs2_rtype};
  assign _T_1453 = {_T_1452,fu_units_1_io_resp_bits_uop_frs3_en};
  assign _T_1454 = {_T_1453,_T_1451};
  assign _T_1455 = {_T_1454,_T_1449};
  assign _T_1456 = {fu_units_1_io_resp_bits_uop_ldst_val,fu_units_1_io_resp_bits_uop_dst_rtype};
  assign _T_1457 = {fu_units_1_io_resp_bits_uop_lrs1,fu_units_1_io_resp_bits_uop_lrs2};
  assign _T_1458 = {_T_1457,fu_units_1_io_resp_bits_uop_lrs3};
  assign _T_1459 = {_T_1458,_T_1456};
  assign _T_1460 = {fu_units_1_io_resp_bits_uop_is_unique,fu_units_1_io_resp_bits_uop_flush_on_commit};
  assign _T_1461 = {_T_1460,fu_units_1_io_resp_bits_uop_ldst};
  assign _T_1462 = {fu_units_1_io_resp_bits_uop_is_amo,fu_units_1_io_resp_bits_uop_is_load};
  assign _T_1463 = {_T_1462,fu_units_1_io_resp_bits_uop_is_sys_pc2epc};
  assign _T_1464 = {_T_1463,_T_1461};
  assign _T_1465 = {_T_1464,_T_1459};
  assign _T_1466 = {_T_1465,_T_1455};
  assign _T_1467 = {fu_units_1_io_resp_bits_uop_is_fencei,fu_units_1_io_resp_bits_uop_is_store};
  assign _T_1468 = {fu_units_1_io_resp_bits_uop_mem_cmd,fu_units_1_io_resp_bits_uop_mem_typ};
  assign _T_1469 = {_T_1468,fu_units_1_io_resp_bits_uop_is_fence};
  assign _T_1470 = {_T_1469,_T_1467};
  assign _T_1471 = {fu_units_1_io_resp_bits_uop_exception,fu_units_1_io_resp_bits_uop_exc_cause};
  assign _T_1472 = {_T_1471,fu_units_1_io_resp_bits_uop_bypassable};
  assign _T_1473 = {fu_units_1_io_resp_bits_uop_prs2_busy,fu_units_1_io_resp_bits_uop_prs3_busy};
  assign _T_1474 = {_T_1473,fu_units_1_io_resp_bits_uop_stale_pdst};
  assign _T_1475 = {_T_1474,_T_1472};
  assign _T_1476 = {_T_1475,_T_1470};
  assign _T_1477 = {fu_units_1_io_resp_bits_uop_pop2,fu_units_1_io_resp_bits_uop_pop3};
  assign _T_1478 = {_T_1477,fu_units_1_io_resp_bits_uop_prs1_busy};
  assign _T_1479 = {fu_units_1_io_resp_bits_uop_brob_idx,fu_units_1_io_resp_bits_uop_pdst};
  assign _T_1480 = {_T_1479,fu_units_1_io_resp_bits_uop_pop1};
  assign _T_1481 = {_T_1480,_T_1478};
  assign _T_1482 = {fu_units_1_io_resp_bits_uop_rob_idx,fu_units_1_io_resp_bits_uop_ldq_idx};
  assign _T_1483 = {_T_1482,fu_units_1_io_resp_bits_uop_stq_idx};
  assign _T_1484 = {fu_units_1_io_resp_bits_uop_fetch_pc_lob,fu_units_1_io_resp_bits_uop_imm_packed};
  assign _T_1485 = {_T_1484,fu_units_1_io_resp_bits_uop_csr_addr};
  assign _T_1486 = {_T_1485,_T_1483};
  assign _T_1487 = {_T_1486,_T_1481};
  assign _T_1488 = {_T_1487,_T_1476};
  assign _T_1489 = {_T_1488,_T_1466};
  assign _T_1490 = {fu_units_1_io_resp_bits_uop_stat_bpd_made_pred,fu_units_1_io_resp_bits_uop_stat_bpd_mispredicted};
  assign _T_1491 = {fu_units_1_io_resp_bits_uop_stat_brjmp_mispredicted,fu_units_1_io_resp_bits_uop_stat_btb_made_pred};
  assign _T_1492 = {_T_1491,fu_units_1_io_resp_bits_uop_stat_btb_mispredicted};
  assign _T_1493 = {_T_1492,_T_1490};
  assign _T_1494 = {fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_u,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr};
  assign _T_1495 = {_T_1494,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_info};
  assign _T_1496 = {fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_way_idx,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_takens};
  assign _T_1497 = {_T_1496,fu_units_1_io_resp_bits_uop_br_prediction_bpd_resp_history};
  assign _T_1498 = {_T_1497,_T_1495};
  assign _T_1499 = {_T_1498,_T_1493};
  assign _T_1500 = {fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_value,fu_units_1_io_resp_bits_uop_br_prediction_bim_resp_entry_idx};
  assign _T_1501 = {fu_units_1_io_resp_bits_uop_br_prediction_bpd_blame,fu_units_1_io_resp_bits_uop_br_prediction_bpd_hit};
  assign _T_1502 = {_T_1501,fu_units_1_io_resp_bits_uop_br_prediction_bpd_taken};
  assign _T_1503 = {_T_1502,_T_1500};
  assign _T_1504 = {fu_units_1_io_resp_bits_uop_br_prediction_btb_blame,fu_units_1_io_resp_bits_uop_br_prediction_btb_hit};
  assign _T_1505 = {_T_1504,fu_units_1_io_resp_bits_uop_br_prediction_btb_taken};
  assign _T_1506 = {fu_units_1_io_resp_bits_uop_is_call,fu_units_1_io_resp_bits_uop_br_mask};
  assign _T_1507 = {_T_1506,fu_units_1_io_resp_bits_uop_br_tag};
  assign _T_1508 = {_T_1507,_T_1505};
  assign _T_1509 = {_T_1508,_T_1503};
  assign _T_1510 = {_T_1509,_T_1499};
  assign _T_1511 = {fu_units_1_io_resp_bits_uop_is_jal,fu_units_1_io_resp_bits_uop_is_ret};
  assign _T_1512 = {fu_units_1_io_resp_bits_uop_allocate_brtag,fu_units_1_io_resp_bits_uop_is_br_or_jmp};
  assign _T_1513 = {_T_1512,fu_units_1_io_resp_bits_uop_is_jump};
  assign _T_1514 = {_T_1513,_T_1511};
  assign _T_1515 = {fu_units_1_io_resp_bits_uop_ctrl_is_load,fu_units_1_io_resp_bits_uop_ctrl_is_sta};
  assign _T_1516 = {_T_1515,fu_units_1_io_resp_bits_uop_ctrl_is_std};
  assign _T_1517 = {fu_units_1_io_resp_bits_uop_ctrl_fcn_dw,fu_units_1_io_resp_bits_uop_ctrl_rf_wen};
  assign _T_1518 = {_T_1517,fu_units_1_io_resp_bits_uop_ctrl_csr_cmd};
  assign _T_1519 = {_T_1518,_T_1516};
  assign _T_1520 = {_T_1519,_T_1514};
  assign _T_1521 = {fu_units_1_io_resp_bits_uop_ctrl_op2_sel,fu_units_1_io_resp_bits_uop_ctrl_imm_sel};
  assign _T_1522 = {_T_1521,fu_units_1_io_resp_bits_uop_ctrl_op_fcn};
  assign _T_1523 = {fu_units_1_io_resp_bits_uop_fu_code,fu_units_1_io_resp_bits_uop_ctrl_br_type};
  assign _T_1524 = {_T_1523,fu_units_1_io_resp_bits_uop_ctrl_op1_sel};
  assign _T_1525 = {_T_1524,_T_1522};
  assign _T_1526 = {fu_units_1_io_resp_bits_uop_inst,fu_units_1_io_resp_bits_uop_pc};
  assign _T_1527 = {_T_1526,fu_units_1_io_resp_bits_uop_iqtype};
  assign _T_1528 = {fu_units_1_io_resp_bits_uop_valid,fu_units_1_io_resp_bits_uop_iw_state};
  assign _T_1529 = {_T_1528,fu_units_1_io_resp_bits_uop_uopc};
  assign _T_1530 = {_T_1529,_T_1527};
  assign _T_1531 = {_T_1530,_T_1525};
  assign _T_1532 = {_T_1531,_T_1520};
  assign _T_1533 = {_T_1532,_T_1510};
  assign _T_1534 = {_T_1533,_T_1489};
  assign _T_1535 = {fu_units_2_io_resp_bits_uop_debug_wdata,fu_units_2_io_resp_bits_uop_debug_events_fetch_seq};
  assign _T_1536 = {fu_units_2_io_resp_bits_uop_xcpt_ae_if,fu_units_2_io_resp_bits_uop_replay_if};
  assign _T_1537 = {_T_1536,fu_units_2_io_resp_bits_uop_xcpt_ma_if};
  assign _T_1538 = {_T_1537,_T_1535};
  assign _T_1539 = {fu_units_2_io_resp_bits_uop_fp_val,fu_units_2_io_resp_bits_uop_fp_single};
  assign _T_1540 = {_T_1539,fu_units_2_io_resp_bits_uop_xcpt_pf_if};
  assign _T_1541 = {fu_units_2_io_resp_bits_uop_lrs1_rtype,fu_units_2_io_resp_bits_uop_lrs2_rtype};
  assign _T_1542 = {_T_1541,fu_units_2_io_resp_bits_uop_frs3_en};
  assign _T_1543 = {_T_1542,_T_1540};
  assign _T_1544 = {_T_1543,_T_1538};
  assign _T_1545 = {fu_units_2_io_resp_bits_uop_ldst_val,fu_units_2_io_resp_bits_uop_dst_rtype};
  assign _T_1546 = {fu_units_2_io_resp_bits_uop_lrs1,fu_units_2_io_resp_bits_uop_lrs2};
  assign _T_1547 = {_T_1546,fu_units_2_io_resp_bits_uop_lrs3};
  assign _T_1548 = {_T_1547,_T_1545};
  assign _T_1549 = {fu_units_2_io_resp_bits_uop_is_unique,fu_units_2_io_resp_bits_uop_flush_on_commit};
  assign _T_1550 = {_T_1549,fu_units_2_io_resp_bits_uop_ldst};
  assign _T_1551 = {fu_units_2_io_resp_bits_uop_is_amo,fu_units_2_io_resp_bits_uop_is_load};
  assign _T_1552 = {_T_1551,fu_units_2_io_resp_bits_uop_is_sys_pc2epc};
  assign _T_1553 = {_T_1552,_T_1550};
  assign _T_1554 = {_T_1553,_T_1548};
  assign _T_1555 = {_T_1554,_T_1544};
  assign _T_1556 = {fu_units_2_io_resp_bits_uop_is_fencei,fu_units_2_io_resp_bits_uop_is_store};
  assign _T_1557 = {fu_units_2_io_resp_bits_uop_mem_cmd,fu_units_2_io_resp_bits_uop_mem_typ};
  assign _T_1558 = {_T_1557,fu_units_2_io_resp_bits_uop_is_fence};
  assign _T_1559 = {_T_1558,_T_1556};
  assign _T_1560 = {fu_units_2_io_resp_bits_uop_exception,fu_units_2_io_resp_bits_uop_exc_cause};
  assign _T_1561 = {_T_1560,fu_units_2_io_resp_bits_uop_bypassable};
  assign _T_1562 = {fu_units_2_io_resp_bits_uop_prs2_busy,fu_units_2_io_resp_bits_uop_prs3_busy};
  assign _T_1563 = {_T_1562,fu_units_2_io_resp_bits_uop_stale_pdst};
  assign _T_1564 = {_T_1563,_T_1561};
  assign _T_1565 = {_T_1564,_T_1559};
  assign _T_1566 = {fu_units_2_io_resp_bits_uop_pop2,fu_units_2_io_resp_bits_uop_pop3};
  assign _T_1567 = {_T_1566,fu_units_2_io_resp_bits_uop_prs1_busy};
  assign _T_1568 = {fu_units_2_io_resp_bits_uop_brob_idx,fu_units_2_io_resp_bits_uop_pdst};
  assign _T_1569 = {_T_1568,fu_units_2_io_resp_bits_uop_pop1};
  assign _T_1570 = {_T_1569,_T_1567};
  assign _T_1571 = {fu_units_2_io_resp_bits_uop_rob_idx,fu_units_2_io_resp_bits_uop_ldq_idx};
  assign _T_1572 = {_T_1571,fu_units_2_io_resp_bits_uop_stq_idx};
  assign _T_1573 = {fu_units_2_io_resp_bits_uop_fetch_pc_lob,fu_units_2_io_resp_bits_uop_imm_packed};
  assign _T_1574 = {_T_1573,fu_units_2_io_resp_bits_uop_csr_addr};
  assign _T_1575 = {_T_1574,_T_1572};
  assign _T_1576 = {_T_1575,_T_1570};
  assign _T_1577 = {_T_1576,_T_1565};
  assign _T_1578 = {_T_1577,_T_1555};
  assign _T_1579 = {fu_units_2_io_resp_bits_uop_stat_bpd_made_pred,fu_units_2_io_resp_bits_uop_stat_bpd_mispredicted};
  assign _T_1580 = {fu_units_2_io_resp_bits_uop_stat_brjmp_mispredicted,fu_units_2_io_resp_bits_uop_stat_btb_made_pred};
  assign _T_1581 = {_T_1580,fu_units_2_io_resp_bits_uop_stat_btb_mispredicted};
  assign _T_1582 = {_T_1581,_T_1579};
  assign _T_1583 = {fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_u,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history_ptr};
  assign _T_1584 = {_T_1583,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_info};
  assign _T_1585 = {fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_way_idx,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_takens};
  assign _T_1586 = {_T_1585,fu_units_2_io_resp_bits_uop_br_prediction_bpd_resp_history};
  assign _T_1587 = {_T_1586,_T_1584};
  assign _T_1588 = {_T_1587,_T_1582};
  assign _T_1589 = {fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_value,fu_units_2_io_resp_bits_uop_br_prediction_bim_resp_entry_idx};
  assign _T_1590 = {fu_units_2_io_resp_bits_uop_br_prediction_bpd_blame,fu_units_2_io_resp_bits_uop_br_prediction_bpd_hit};
  assign _T_1591 = {_T_1590,fu_units_2_io_resp_bits_uop_br_prediction_bpd_taken};
  assign _T_1592 = {_T_1591,_T_1589};
  assign _T_1593 = {fu_units_2_io_resp_bits_uop_br_prediction_btb_blame,fu_units_2_io_resp_bits_uop_br_prediction_btb_hit};
  assign _T_1594 = {_T_1593,fu_units_2_io_resp_bits_uop_br_prediction_btb_taken};
  assign _T_1595 = {fu_units_2_io_resp_bits_uop_is_call,fu_units_2_io_resp_bits_uop_br_mask};
  assign _T_1596 = {_T_1595,fu_units_2_io_resp_bits_uop_br_tag};
  assign _T_1597 = {_T_1596,_T_1594};
  assign _T_1598 = {_T_1597,_T_1592};
  assign _T_1599 = {_T_1598,_T_1588};
  assign _T_1600 = {fu_units_2_io_resp_bits_uop_is_jal,fu_units_2_io_resp_bits_uop_is_ret};
  assign _T_1601 = {fu_units_2_io_resp_bits_uop_allocate_brtag,fu_units_2_io_resp_bits_uop_is_br_or_jmp};
  assign _T_1602 = {_T_1601,fu_units_2_io_resp_bits_uop_is_jump};
  assign _T_1603 = {_T_1602,_T_1600};
  assign _T_1604 = {fu_units_2_io_resp_bits_uop_ctrl_is_load,fu_units_2_io_resp_bits_uop_ctrl_is_sta};
  assign _T_1605 = {_T_1604,fu_units_2_io_resp_bits_uop_ctrl_is_std};
  assign _T_1606 = {fu_units_2_io_resp_bits_uop_ctrl_fcn_dw,fu_units_2_io_resp_bits_uop_ctrl_rf_wen};
  assign _T_1607 = {_T_1606,fu_units_2_io_resp_bits_uop_ctrl_csr_cmd};
  assign _T_1608 = {_T_1607,_T_1605};
  assign _T_1609 = {_T_1608,_T_1603};
  assign _T_1610 = {fu_units_2_io_resp_bits_uop_ctrl_op2_sel,fu_units_2_io_resp_bits_uop_ctrl_imm_sel};
  assign _T_1611 = {_T_1610,fu_units_2_io_resp_bits_uop_ctrl_op_fcn};
  assign _T_1612 = {fu_units_2_io_resp_bits_uop_fu_code,fu_units_2_io_resp_bits_uop_ctrl_br_type};
  assign _T_1613 = {_T_1612,fu_units_2_io_resp_bits_uop_ctrl_op1_sel};
  assign _T_1614 = {_T_1613,_T_1611};
  assign _T_1615 = {fu_units_2_io_resp_bits_uop_inst,fu_units_2_io_resp_bits_uop_pc};
  assign _T_1616 = {_T_1615,fu_units_2_io_resp_bits_uop_iqtype};
  assign _T_1617 = {fu_units_2_io_resp_bits_uop_valid,fu_units_2_io_resp_bits_uop_iw_state};
  assign _T_1618 = {_T_1617,fu_units_2_io_resp_bits_uop_uopc};
  assign _T_1619 = {_T_1618,_T_1616};
  assign _T_1620 = {_T_1619,_T_1614};
  assign _T_1621 = {_T_1620,_T_1609};
  assign _T_1622 = {_T_1621,_T_1599};
  assign _T_1623 = {_T_1622,_T_1578};
  assign _T_1624 = fu_units_1_io_resp_valid ? _T_1534 : _T_1623;
  assign _T_1625 = alu_io_resp_valid ? _T_1445 : _T_1624;
  assign _T_1649 = _T_1637[108:107];
  assign _T_1659 = _T_1637[138];
  assign _T_1660 = _T_1637[139];
  assign _T_1665 = _T_1637[150];
  assign _T_1675 = _T_1637[253:247];
  assign _T_1679 = _T_1637[274:268];
  assign _T_1714 = _T_1637[409];
  assign _T_1728 = fu_units_1_io_resp_valid ? fu_units_1_io_resp_bits_data : fu_units_2_io_resp_bits_data;
  assign _T_1729 = alu_io_resp_valid ? alu_io_resp_bits_data : _T_1728;
  assign _T_1730 = alu_io_resp_bits_uop_imm_packed[19];
  assign _T_1731 = $signed(_T_1730);
  assign _T_1735 = {11{_T_1731}};
  assign _T_1741 = {8{_T_1731}};
  assign _T_1747 = alu_io_resp_bits_uop_imm_packed[8];
  assign _T_1748 = $signed(_T_1747);
  assign _T_1753 = alu_io_resp_bits_uop_imm_packed[18:14];
  assign _T_1754 = $signed(_T_1753);
  assign _T_1758 = alu_io_resp_bits_uop_imm_packed[13:9];
  assign _T_1759 = $signed(_T_1758);
  assign _T_1768 = $unsigned(_T_1748);
  assign _T_1769 = $unsigned(_T_1759);
  assign _T_1770 = $unsigned(_T_1754);
  assign _T_1771 = {_T_1770,_T_1769};
  assign _T_1772 = {_T_1771,_T_1768};
  assign _T_1773 = $unsigned(_T_1731);
  assign _T_1774 = $unsigned(_T_1741);
  assign _T_1775 = {_T_1774,_T_1773};
  assign _T_1776 = $unsigned(_T_1735);
  assign _T_1778 = {_T_1773,_T_1776};
  assign _T_1779 = {_T_1778,_T_1775};
  assign _T_1780 = {_T_1779,_T_1772};
  assign _T_1781 = $signed(_T_1780);
  assign _T_1782 = $unsigned(_T_1781);
  assign _T_1796 = fu_units_1_io_resp_valid + fu_units_2_io_resp_valid;
  assign _GEN_0 = {{1'd0}, alu_io_resp_valid};
  assign _T_1797 = _GEN_0 + _T_1796;
  assign _T_1799 = _T_1797 <= 3'h1;
  assign _T_1801 = muldiv_resp_val == 1'h0;
  assign _T_1802 = _T_1799 & _T_1801;
  assign _T_1809 = _T_1797 <= 3'h2;
  assign _T_1811 = _T_1809 & muldiv_resp_val;
  assign _T_1812 = _T_1802 | _T_1811;
  assign _T_1821 = _T_1812 | reset;
  assign _T_1823 = _T_1821 == 1'h0;
  assign io_fu_types = _T_1230;
  assign io_resp_0_valid = _T_1351;
  assign io_resp_0_bits_uop_ctrl_rf_wen = _T_1631_ctrl_rf_wen;
  assign io_resp_0_bits_uop_ctrl_csr_cmd = alu_io_resp_bits_uop_ctrl_csr_cmd;
  assign io_resp_0_bits_uop_csr_addr = _T_1782[11:0];
  assign io_resp_0_bits_uop_rob_idx = _T_1631_rob_idx;
  assign io_resp_0_bits_uop_pdst = _T_1631_pdst;
  assign io_resp_0_bits_uop_bypassable = _T_1631_bypassable;
  assign io_resp_0_bits_uop_is_store = _T_1631_is_store;
  assign io_resp_0_bits_uop_is_amo = _T_1631_is_amo;
  assign io_resp_0_bits_uop_dst_rtype = _T_1631_dst_rtype;
  assign io_resp_0_bits_data = _T_1729;
  assign io_bypass_valid_0 = alu_io_bypass_valid_0;
  assign io_bypass_valid_1 = alu_io_bypass_valid_1;
  assign io_bypass_valid_2 = alu_io_bypass_valid_2;
  assign io_bypass_uop_0_ctrl_rf_wen = alu_io_bypass_uop_0_ctrl_rf_wen;
  assign io_bypass_uop_0_pdst = alu_io_bypass_uop_0_pdst;
  assign io_bypass_uop_0_dst_rtype = alu_io_bypass_uop_0_dst_rtype;
  assign io_bypass_uop_1_ctrl_rf_wen = alu_io_bypass_uop_1_ctrl_rf_wen;
  assign io_bypass_uop_1_pdst = alu_io_bypass_uop_1_pdst;
  assign io_bypass_uop_1_dst_rtype = alu_io_bypass_uop_1_dst_rtype;
  assign io_bypass_uop_2_ctrl_rf_wen = alu_io_bypass_uop_2_ctrl_rf_wen;
  assign io_bypass_uop_2_pdst = alu_io_bypass_uop_2_pdst;
  assign io_bypass_uop_2_dst_rtype = alu_io_bypass_uop_2_dst_rtype;
  assign io_bypass_data_0 = alu_io_bypass_data_0;
  assign io_bypass_data_1 = alu_io_bypass_data_1;
  assign io_bypass_data_2 = alu_io_bypass_data_2;
  assign io_br_unit_take_pc = alu_io_br_unit_take_pc;
  assign io_br_unit_target = alu_io_br_unit_target;
  assign io_br_unit_brinfo_valid = alu_io_br_unit_brinfo_valid;
  assign io_br_unit_brinfo_mispredict = alu_io_br_unit_brinfo_mispredict;
  assign io_br_unit_brinfo_mask = alu_io_br_unit_brinfo_mask;
  assign io_br_unit_brinfo_tag = alu_io_br_unit_brinfo_tag;
  assign io_br_unit_brinfo_exe_mask = alu_io_br_unit_brinfo_exe_mask;
  assign io_br_unit_brinfo_rob_idx = alu_io_br_unit_brinfo_rob_idx;
  assign io_br_unit_brinfo_ldq_idx = alu_io_br_unit_brinfo_ldq_idx;
  assign io_br_unit_brinfo_stq_idx = alu_io_br_unit_brinfo_stq_idx;
  assign io_br_unit_brinfo_is_jr = alu_io_br_unit_brinfo_is_jr;
  assign io_br_unit_btb_update_valid = alu_io_br_unit_btb_update_valid;
  assign io_br_unit_btb_update_bits_pc = alu_io_br_unit_btb_update_bits_pc;
  assign io_br_unit_btb_update_bits_target = alu_io_br_unit_btb_update_bits_target;
  assign io_br_unit_btb_update_bits_cfi_pc = alu_io_br_unit_btb_update_bits_cfi_pc;
  assign io_br_unit_btb_update_bits_bpd_type = alu_io_br_unit_btb_update_bits_bpd_type;
  assign io_br_unit_btb_update_bits_cfi_type = alu_io_br_unit_btb_update_bits_cfi_type;
  assign io_br_unit_bim_update_valid = alu_io_br_unit_bim_update_valid;
  assign io_br_unit_bim_update_bits_taken = alu_io_br_unit_bim_update_bits_taken;
  assign io_br_unit_bim_update_bits_bim_resp_value = alu_io_br_unit_bim_update_bits_bim_resp_value;
  assign io_br_unit_bim_update_bits_bim_resp_entry_idx = alu_io_br_unit_bim_update_bits_bim_resp_entry_idx;
  assign io_br_unit_bim_update_bits_bim_resp_way_idx = alu_io_br_unit_bim_update_bits_bim_resp_way_idx;
  assign io_br_unit_bpd_update_valid = alu_io_br_unit_bpd_update_valid;
  assign io_br_unit_bpd_update_bits_br_pc = alu_io_br_unit_bpd_update_bits_br_pc;
  assign io_br_unit_bpd_update_bits_brob_idx = alu_io_br_unit_bpd_update_bits_brob_idx;
  assign io_br_unit_bpd_update_bits_mispredict = alu_io_br_unit_bpd_update_bits_mispredict;
  assign io_br_unit_bpd_update_bits_history = alu_io_br_unit_bpd_update_bits_history;
  assign io_br_unit_bpd_update_bits_history_ptr = alu_io_br_unit_bpd_update_bits_history_ptr;
  assign io_br_unit_bpd_update_bits_bpd_predict_val = alu_io_br_unit_bpd_update_bits_bpd_predict_val;
  assign io_br_unit_bpd_update_bits_bpd_mispredict = alu_io_br_unit_bpd_update_bits_bpd_mispredict;
  assign io_br_unit_bpd_update_bits_taken = alu_io_br_unit_bpd_update_bits_taken;
  assign io_br_unit_bpd_update_bits_is_br = alu_io_br_unit_bpd_update_bits_is_br;
  assign io_br_unit_bpd_update_bits_new_pc_same_packet = alu_io_br_unit_bpd_update_bits_new_pc_same_packet;
  assign io_br_unit_xcpt_valid = alu_io_br_unit_xcpt_valid;
  assign io_br_unit_xcpt_bits_uop_br_mask = alu_io_br_unit_xcpt_bits_uop_br_mask;
  assign io_br_unit_xcpt_bits_uop_rob_idx = alu_io_br_unit_xcpt_bits_uop_rob_idx;
  assign io_br_unit_xcpt_bits_badvaddr = alu_io_br_unit_xcpt_bits_badvaddr;
  assign muldiv_busy = _T_1345;
  assign alu_io_req_valid = _T_1243;
  assign alu_io_req_bits_uop_valid = io_req_bits_uop_valid;
  assign alu_io_req_bits_uop_iw_state = io_req_bits_uop_iw_state;
  assign alu_io_req_bits_uop_uopc = io_req_bits_uop_uopc;
  assign alu_io_req_bits_uop_inst = io_req_bits_uop_inst;
  assign alu_io_req_bits_uop_pc = io_req_bits_uop_pc;
  assign alu_io_req_bits_uop_iqtype = io_req_bits_uop_iqtype;
  assign alu_io_req_bits_uop_fu_code = io_req_bits_uop_fu_code;
  assign alu_io_req_bits_uop_ctrl_br_type = io_req_bits_uop_ctrl_br_type;
  assign alu_io_req_bits_uop_ctrl_op1_sel = io_req_bits_uop_ctrl_op1_sel;
  assign alu_io_req_bits_uop_ctrl_op2_sel = io_req_bits_uop_ctrl_op2_sel;
  assign alu_io_req_bits_uop_ctrl_imm_sel = io_req_bits_uop_ctrl_imm_sel;
  assign alu_io_req_bits_uop_ctrl_op_fcn = io_req_bits_uop_ctrl_op_fcn;
  assign alu_io_req_bits_uop_ctrl_fcn_dw = io_req_bits_uop_ctrl_fcn_dw;
  assign alu_io_req_bits_uop_ctrl_rf_wen = io_req_bits_uop_ctrl_rf_wen;
  assign alu_io_req_bits_uop_ctrl_csr_cmd = io_req_bits_uop_ctrl_csr_cmd;
  assign alu_io_req_bits_uop_ctrl_is_load = io_req_bits_uop_ctrl_is_load;
  assign alu_io_req_bits_uop_ctrl_is_sta = io_req_bits_uop_ctrl_is_sta;
  assign alu_io_req_bits_uop_ctrl_is_std = io_req_bits_uop_ctrl_is_std;
  assign alu_io_req_bits_uop_allocate_brtag = io_req_bits_uop_allocate_brtag;
  assign alu_io_req_bits_uop_is_br_or_jmp = io_req_bits_uop_is_br_or_jmp;
  assign alu_io_req_bits_uop_is_jump = io_req_bits_uop_is_jump;
  assign alu_io_req_bits_uop_is_jal = io_req_bits_uop_is_jal;
  assign alu_io_req_bits_uop_is_ret = io_req_bits_uop_is_ret;
  assign alu_io_req_bits_uop_is_call = io_req_bits_uop_is_call;
  assign alu_io_req_bits_uop_br_mask = io_req_bits_uop_br_mask;
  assign alu_io_req_bits_uop_br_tag = io_req_bits_uop_br_tag;
  assign alu_io_req_bits_uop_br_prediction_btb_blame = io_req_bits_uop_br_prediction_btb_blame;
  assign alu_io_req_bits_uop_br_prediction_btb_hit = io_req_bits_uop_br_prediction_btb_hit;
  assign alu_io_req_bits_uop_br_prediction_btb_taken = io_req_bits_uop_br_prediction_btb_taken;
  assign alu_io_req_bits_uop_br_prediction_bpd_blame = io_req_bits_uop_br_prediction_bpd_blame;
  assign alu_io_req_bits_uop_br_prediction_bpd_hit = io_req_bits_uop_br_prediction_bpd_hit;
  assign alu_io_req_bits_uop_br_prediction_bpd_taken = io_req_bits_uop_br_prediction_bpd_taken;
  assign alu_io_req_bits_uop_br_prediction_bim_resp_value = io_req_bits_uop_br_prediction_bim_resp_value;
  assign alu_io_req_bits_uop_br_prediction_bim_resp_entry_idx = io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  assign alu_io_req_bits_uop_br_prediction_bim_resp_way_idx = io_req_bits_uop_br_prediction_bim_resp_way_idx;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_takens = io_req_bits_uop_br_prediction_bpd_resp_takens;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_history = io_req_bits_uop_br_prediction_bpd_resp_history;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_history_u = io_req_bits_uop_br_prediction_bpd_resp_history_u;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_history_ptr = io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  assign alu_io_req_bits_uop_br_prediction_bpd_resp_info = io_req_bits_uop_br_prediction_bpd_resp_info;
  assign alu_io_req_bits_uop_stat_brjmp_mispredicted = io_req_bits_uop_stat_brjmp_mispredicted;
  assign alu_io_req_bits_uop_stat_btb_made_pred = io_req_bits_uop_stat_btb_made_pred;
  assign alu_io_req_bits_uop_stat_btb_mispredicted = io_req_bits_uop_stat_btb_mispredicted;
  assign alu_io_req_bits_uop_stat_bpd_made_pred = io_req_bits_uop_stat_bpd_made_pred;
  assign alu_io_req_bits_uop_stat_bpd_mispredicted = io_req_bits_uop_stat_bpd_mispredicted;
  assign alu_io_req_bits_uop_fetch_pc_lob = io_req_bits_uop_fetch_pc_lob;
  assign alu_io_req_bits_uop_imm_packed = io_req_bits_uop_imm_packed;
  assign alu_io_req_bits_uop_csr_addr = io_req_bits_uop_csr_addr;
  assign alu_io_req_bits_uop_rob_idx = io_req_bits_uop_rob_idx;
  assign alu_io_req_bits_uop_ldq_idx = io_req_bits_uop_ldq_idx;
  assign alu_io_req_bits_uop_stq_idx = io_req_bits_uop_stq_idx;
  assign alu_io_req_bits_uop_brob_idx = io_req_bits_uop_brob_idx;
  assign alu_io_req_bits_uop_pdst = io_req_bits_uop_pdst;
  assign alu_io_req_bits_uop_pop1 = io_req_bits_uop_pop1;
  assign alu_io_req_bits_uop_pop2 = io_req_bits_uop_pop2;
  assign alu_io_req_bits_uop_pop3 = io_req_bits_uop_pop3;
  assign alu_io_req_bits_uop_prs1_busy = io_req_bits_uop_prs1_busy;
  assign alu_io_req_bits_uop_prs2_busy = io_req_bits_uop_prs2_busy;
  assign alu_io_req_bits_uop_prs3_busy = io_req_bits_uop_prs3_busy;
  assign alu_io_req_bits_uop_stale_pdst = io_req_bits_uop_stale_pdst;
  assign alu_io_req_bits_uop_exception = io_req_bits_uop_exception;
  assign alu_io_req_bits_uop_exc_cause = io_req_bits_uop_exc_cause;
  assign alu_io_req_bits_uop_bypassable = io_req_bits_uop_bypassable;
  assign alu_io_req_bits_uop_mem_cmd = io_req_bits_uop_mem_cmd;
  assign alu_io_req_bits_uop_mem_typ = io_req_bits_uop_mem_typ;
  assign alu_io_req_bits_uop_is_fence = io_req_bits_uop_is_fence;
  assign alu_io_req_bits_uop_is_fencei = io_req_bits_uop_is_fencei;
  assign alu_io_req_bits_uop_is_store = io_req_bits_uop_is_store;
  assign alu_io_req_bits_uop_is_amo = io_req_bits_uop_is_amo;
  assign alu_io_req_bits_uop_is_load = io_req_bits_uop_is_load;
  assign alu_io_req_bits_uop_is_sys_pc2epc = io_req_bits_uop_is_sys_pc2epc;
  assign alu_io_req_bits_uop_is_unique = io_req_bits_uop_is_unique;
  assign alu_io_req_bits_uop_flush_on_commit = io_req_bits_uop_flush_on_commit;
  assign alu_io_req_bits_uop_ldst = io_req_bits_uop_ldst;
  assign alu_io_req_bits_uop_lrs1 = io_req_bits_uop_lrs1;
  assign alu_io_req_bits_uop_lrs2 = io_req_bits_uop_lrs2;
  assign alu_io_req_bits_uop_lrs3 = io_req_bits_uop_lrs3;
  assign alu_io_req_bits_uop_ldst_val = io_req_bits_uop_ldst_val;
  assign alu_io_req_bits_uop_dst_rtype = io_req_bits_uop_dst_rtype;
  assign alu_io_req_bits_uop_lrs1_rtype = io_req_bits_uop_lrs1_rtype;
  assign alu_io_req_bits_uop_lrs2_rtype = io_req_bits_uop_lrs2_rtype;
  assign alu_io_req_bits_uop_frs3_en = io_req_bits_uop_frs3_en;
  assign alu_io_req_bits_uop_fp_val = io_req_bits_uop_fp_val;
  assign alu_io_req_bits_uop_fp_single = io_req_bits_uop_fp_single;
  assign alu_io_req_bits_uop_xcpt_pf_if = io_req_bits_uop_xcpt_pf_if;
  assign alu_io_req_bits_uop_xcpt_ae_if = io_req_bits_uop_xcpt_ae_if;
  assign alu_io_req_bits_uop_replay_if = io_req_bits_uop_replay_if;
  assign alu_io_req_bits_uop_xcpt_ma_if = io_req_bits_uop_xcpt_ma_if;
  assign alu_io_req_bits_uop_debug_wdata = io_req_bits_uop_debug_wdata;
  assign alu_io_req_bits_uop_debug_events_fetch_seq = io_req_bits_uop_debug_events_fetch_seq;
  assign alu_io_req_bits_rs1_data = io_req_bits_rs1_data;
  assign alu_io_req_bits_rs2_data = io_req_bits_rs2_data;
  assign alu_io_req_bits_kill = io_req_bits_kill;
  assign alu_io_brinfo_valid = io_brinfo_valid;
  assign alu_io_brinfo_mispredict = io_brinfo_mispredict;
  assign alu_io_brinfo_mask = io_brinfo_mask;
  assign alu_io_get_rob_pc_curr_pc = io_get_rob_pc_curr_pc;
  assign alu_io_get_rob_pc_curr_brob_idx = io_get_rob_pc_curr_brob_idx;
  assign alu_io_get_rob_pc_next_val = io_get_rob_pc_next_val;
  assign alu_io_get_rob_pc_next_pc = io_get_rob_pc_next_pc;
  assign alu_io_get_pred_info_bim_resp_value = io_get_pred_info_bim_resp_value;
  assign alu_io_get_pred_info_bim_resp_entry_idx = io_get_pred_info_bim_resp_entry_idx;
  assign alu_io_get_pred_info_bim_resp_way_idx = io_get_pred_info_bim_resp_way_idx;
  assign alu_io_get_pred_info_bpd_resp_history = io_get_pred_info_bpd_resp_history;
  assign alu_io_get_pred_info_bpd_resp_history_ptr = io_get_pred_info_bpd_resp_history_ptr;
  assign alu_io_status_debug = io_status_debug;
  assign alu_clock = clock;
  assign alu_reset = reset;
  assign fu_units_1_io_req_valid = _T_1245;
  assign fu_units_1_io_req_bits_uop_valid = io_req_bits_uop_valid;
  assign fu_units_1_io_req_bits_uop_iw_state = io_req_bits_uop_iw_state;
  assign fu_units_1_io_req_bits_uop_uopc = io_req_bits_uop_uopc;
  assign fu_units_1_io_req_bits_uop_inst = io_req_bits_uop_inst;
  assign fu_units_1_io_req_bits_uop_pc = io_req_bits_uop_pc;
  assign fu_units_1_io_req_bits_uop_iqtype = io_req_bits_uop_iqtype;
  assign fu_units_1_io_req_bits_uop_fu_code = io_req_bits_uop_fu_code;
  assign fu_units_1_io_req_bits_uop_ctrl_br_type = io_req_bits_uop_ctrl_br_type;
  assign fu_units_1_io_req_bits_uop_ctrl_op1_sel = io_req_bits_uop_ctrl_op1_sel;
  assign fu_units_1_io_req_bits_uop_ctrl_op2_sel = io_req_bits_uop_ctrl_op2_sel;
  assign fu_units_1_io_req_bits_uop_ctrl_imm_sel = io_req_bits_uop_ctrl_imm_sel;
  assign fu_units_1_io_req_bits_uop_ctrl_op_fcn = io_req_bits_uop_ctrl_op_fcn;
  assign fu_units_1_io_req_bits_uop_ctrl_fcn_dw = io_req_bits_uop_ctrl_fcn_dw;
  assign fu_units_1_io_req_bits_uop_ctrl_rf_wen = io_req_bits_uop_ctrl_rf_wen;
  assign fu_units_1_io_req_bits_uop_ctrl_csr_cmd = io_req_bits_uop_ctrl_csr_cmd;
  assign fu_units_1_io_req_bits_uop_ctrl_is_load = io_req_bits_uop_ctrl_is_load;
  assign fu_units_1_io_req_bits_uop_ctrl_is_sta = io_req_bits_uop_ctrl_is_sta;
  assign fu_units_1_io_req_bits_uop_ctrl_is_std = io_req_bits_uop_ctrl_is_std;
  assign fu_units_1_io_req_bits_uop_allocate_brtag = io_req_bits_uop_allocate_brtag;
  assign fu_units_1_io_req_bits_uop_is_br_or_jmp = io_req_bits_uop_is_br_or_jmp;
  assign fu_units_1_io_req_bits_uop_is_jump = io_req_bits_uop_is_jump;
  assign fu_units_1_io_req_bits_uop_is_jal = io_req_bits_uop_is_jal;
  assign fu_units_1_io_req_bits_uop_is_ret = io_req_bits_uop_is_ret;
  assign fu_units_1_io_req_bits_uop_is_call = io_req_bits_uop_is_call;
  assign fu_units_1_io_req_bits_uop_br_mask = io_req_bits_uop_br_mask;
  assign fu_units_1_io_req_bits_uop_br_tag = io_req_bits_uop_br_tag;
  assign fu_units_1_io_req_bits_uop_br_prediction_btb_blame = io_req_bits_uop_br_prediction_btb_blame;
  assign fu_units_1_io_req_bits_uop_br_prediction_btb_hit = io_req_bits_uop_br_prediction_btb_hit;
  assign fu_units_1_io_req_bits_uop_br_prediction_btb_taken = io_req_bits_uop_br_prediction_btb_taken;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_blame = io_req_bits_uop_br_prediction_bpd_blame;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_hit = io_req_bits_uop_br_prediction_bpd_hit;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_taken = io_req_bits_uop_br_prediction_bpd_taken;
  assign fu_units_1_io_req_bits_uop_br_prediction_bim_resp_value = io_req_bits_uop_br_prediction_bim_resp_value;
  assign fu_units_1_io_req_bits_uop_br_prediction_bim_resp_entry_idx = io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  assign fu_units_1_io_req_bits_uop_br_prediction_bim_resp_way_idx = io_req_bits_uop_br_prediction_bim_resp_way_idx;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_takens = io_req_bits_uop_br_prediction_bpd_resp_takens;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history = io_req_bits_uop_br_prediction_bpd_resp_history;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_u = io_req_bits_uop_br_prediction_bpd_resp_history_u;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_history_ptr = io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  assign fu_units_1_io_req_bits_uop_br_prediction_bpd_resp_info = io_req_bits_uop_br_prediction_bpd_resp_info;
  assign fu_units_1_io_req_bits_uop_stat_brjmp_mispredicted = io_req_bits_uop_stat_brjmp_mispredicted;
  assign fu_units_1_io_req_bits_uop_stat_btb_made_pred = io_req_bits_uop_stat_btb_made_pred;
  assign fu_units_1_io_req_bits_uop_stat_btb_mispredicted = io_req_bits_uop_stat_btb_mispredicted;
  assign fu_units_1_io_req_bits_uop_stat_bpd_made_pred = io_req_bits_uop_stat_bpd_made_pred;
  assign fu_units_1_io_req_bits_uop_stat_bpd_mispredicted = io_req_bits_uop_stat_bpd_mispredicted;
  assign fu_units_1_io_req_bits_uop_fetch_pc_lob = io_req_bits_uop_fetch_pc_lob;
  assign fu_units_1_io_req_bits_uop_imm_packed = io_req_bits_uop_imm_packed;
  assign fu_units_1_io_req_bits_uop_csr_addr = io_req_bits_uop_csr_addr;
  assign fu_units_1_io_req_bits_uop_rob_idx = io_req_bits_uop_rob_idx;
  assign fu_units_1_io_req_bits_uop_ldq_idx = io_req_bits_uop_ldq_idx;
  assign fu_units_1_io_req_bits_uop_stq_idx = io_req_bits_uop_stq_idx;
  assign fu_units_1_io_req_bits_uop_brob_idx = io_req_bits_uop_brob_idx;
  assign fu_units_1_io_req_bits_uop_pdst = io_req_bits_uop_pdst;
  assign fu_units_1_io_req_bits_uop_pop1 = io_req_bits_uop_pop1;
  assign fu_units_1_io_req_bits_uop_pop2 = io_req_bits_uop_pop2;
  assign fu_units_1_io_req_bits_uop_pop3 = io_req_bits_uop_pop3;
  assign fu_units_1_io_req_bits_uop_prs1_busy = io_req_bits_uop_prs1_busy;
  assign fu_units_1_io_req_bits_uop_prs2_busy = io_req_bits_uop_prs2_busy;
  assign fu_units_1_io_req_bits_uop_prs3_busy = io_req_bits_uop_prs3_busy;
  assign fu_units_1_io_req_bits_uop_stale_pdst = io_req_bits_uop_stale_pdst;
  assign fu_units_1_io_req_bits_uop_exception = io_req_bits_uop_exception;
  assign fu_units_1_io_req_bits_uop_exc_cause = io_req_bits_uop_exc_cause;
  assign fu_units_1_io_req_bits_uop_bypassable = io_req_bits_uop_bypassable;
  assign fu_units_1_io_req_bits_uop_mem_cmd = io_req_bits_uop_mem_cmd;
  assign fu_units_1_io_req_bits_uop_mem_typ = io_req_bits_uop_mem_typ;
  assign fu_units_1_io_req_bits_uop_is_fence = io_req_bits_uop_is_fence;
  assign fu_units_1_io_req_bits_uop_is_fencei = io_req_bits_uop_is_fencei;
  assign fu_units_1_io_req_bits_uop_is_store = io_req_bits_uop_is_store;
  assign fu_units_1_io_req_bits_uop_is_amo = io_req_bits_uop_is_amo;
  assign fu_units_1_io_req_bits_uop_is_load = io_req_bits_uop_is_load;
  assign fu_units_1_io_req_bits_uop_is_sys_pc2epc = io_req_bits_uop_is_sys_pc2epc;
  assign fu_units_1_io_req_bits_uop_is_unique = io_req_bits_uop_is_unique;
  assign fu_units_1_io_req_bits_uop_flush_on_commit = io_req_bits_uop_flush_on_commit;
  assign fu_units_1_io_req_bits_uop_ldst = io_req_bits_uop_ldst;
  assign fu_units_1_io_req_bits_uop_lrs1 = io_req_bits_uop_lrs1;
  assign fu_units_1_io_req_bits_uop_lrs2 = io_req_bits_uop_lrs2;
  assign fu_units_1_io_req_bits_uop_lrs3 = io_req_bits_uop_lrs3;
  assign fu_units_1_io_req_bits_uop_ldst_val = io_req_bits_uop_ldst_val;
  assign fu_units_1_io_req_bits_uop_dst_rtype = io_req_bits_uop_dst_rtype;
  assign fu_units_1_io_req_bits_uop_lrs1_rtype = io_req_bits_uop_lrs1_rtype;
  assign fu_units_1_io_req_bits_uop_lrs2_rtype = io_req_bits_uop_lrs2_rtype;
  assign fu_units_1_io_req_bits_uop_frs3_en = io_req_bits_uop_frs3_en;
  assign fu_units_1_io_req_bits_uop_fp_val = io_req_bits_uop_fp_val;
  assign fu_units_1_io_req_bits_uop_fp_single = io_req_bits_uop_fp_single;
  assign fu_units_1_io_req_bits_uop_xcpt_pf_if = io_req_bits_uop_xcpt_pf_if;
  assign fu_units_1_io_req_bits_uop_xcpt_ae_if = io_req_bits_uop_xcpt_ae_if;
  assign fu_units_1_io_req_bits_uop_replay_if = io_req_bits_uop_replay_if;
  assign fu_units_1_io_req_bits_uop_xcpt_ma_if = io_req_bits_uop_xcpt_ma_if;
  assign fu_units_1_io_req_bits_uop_debug_wdata = io_req_bits_uop_debug_wdata;
  assign fu_units_1_io_req_bits_uop_debug_events_fetch_seq = io_req_bits_uop_debug_events_fetch_seq;
  assign fu_units_1_io_req_bits_rs1_data = io_req_bits_rs1_data;
  assign fu_units_1_io_req_bits_rs2_data = io_req_bits_rs2_data;
  assign fu_units_1_io_req_bits_kill = io_req_bits_kill;
  assign fu_units_1_io_brinfo_valid = io_brinfo_valid;
  assign fu_units_1_io_brinfo_mispredict = io_brinfo_mispredict;
  assign fu_units_1_io_brinfo_mask = io_brinfo_mask;
  assign fu_units_1_clock = clock;
  assign fu_units_1_reset = reset;
  assign muldiv_resp_val = fu_units_2_io_resp_valid;
  assign fu_units_2_io_req_valid = _T_1333;
  assign fu_units_2_io_req_bits_uop_valid = io_req_bits_uop_valid;
  assign fu_units_2_io_req_bits_uop_iw_state = io_req_bits_uop_iw_state;
  assign fu_units_2_io_req_bits_uop_uopc = io_req_bits_uop_uopc;
  assign fu_units_2_io_req_bits_uop_inst = io_req_bits_uop_inst;
  assign fu_units_2_io_req_bits_uop_pc = io_req_bits_uop_pc;
  assign fu_units_2_io_req_bits_uop_iqtype = io_req_bits_uop_iqtype;
  assign fu_units_2_io_req_bits_uop_fu_code = io_req_bits_uop_fu_code;
  assign fu_units_2_io_req_bits_uop_ctrl_br_type = io_req_bits_uop_ctrl_br_type;
  assign fu_units_2_io_req_bits_uop_ctrl_op1_sel = io_req_bits_uop_ctrl_op1_sel;
  assign fu_units_2_io_req_bits_uop_ctrl_op2_sel = io_req_bits_uop_ctrl_op2_sel;
  assign fu_units_2_io_req_bits_uop_ctrl_imm_sel = io_req_bits_uop_ctrl_imm_sel;
  assign fu_units_2_io_req_bits_uop_ctrl_op_fcn = io_req_bits_uop_ctrl_op_fcn;
  assign fu_units_2_io_req_bits_uop_ctrl_fcn_dw = io_req_bits_uop_ctrl_fcn_dw;
  assign fu_units_2_io_req_bits_uop_ctrl_rf_wen = io_req_bits_uop_ctrl_rf_wen;
  assign fu_units_2_io_req_bits_uop_ctrl_csr_cmd = io_req_bits_uop_ctrl_csr_cmd;
  assign fu_units_2_io_req_bits_uop_ctrl_is_load = io_req_bits_uop_ctrl_is_load;
  assign fu_units_2_io_req_bits_uop_ctrl_is_sta = io_req_bits_uop_ctrl_is_sta;
  assign fu_units_2_io_req_bits_uop_ctrl_is_std = io_req_bits_uop_ctrl_is_std;
  assign fu_units_2_io_req_bits_uop_allocate_brtag = io_req_bits_uop_allocate_brtag;
  assign fu_units_2_io_req_bits_uop_is_br_or_jmp = io_req_bits_uop_is_br_or_jmp;
  assign fu_units_2_io_req_bits_uop_is_jump = io_req_bits_uop_is_jump;
  assign fu_units_2_io_req_bits_uop_is_jal = io_req_bits_uop_is_jal;
  assign fu_units_2_io_req_bits_uop_is_ret = io_req_bits_uop_is_ret;
  assign fu_units_2_io_req_bits_uop_is_call = io_req_bits_uop_is_call;
  assign fu_units_2_io_req_bits_uop_br_mask = io_req_bits_uop_br_mask;
  assign fu_units_2_io_req_bits_uop_br_tag = io_req_bits_uop_br_tag;
  assign fu_units_2_io_req_bits_uop_br_prediction_btb_blame = io_req_bits_uop_br_prediction_btb_blame;
  assign fu_units_2_io_req_bits_uop_br_prediction_btb_hit = io_req_bits_uop_br_prediction_btb_hit;
  assign fu_units_2_io_req_bits_uop_br_prediction_btb_taken = io_req_bits_uop_br_prediction_btb_taken;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_blame = io_req_bits_uop_br_prediction_bpd_blame;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_hit = io_req_bits_uop_br_prediction_bpd_hit;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_taken = io_req_bits_uop_br_prediction_bpd_taken;
  assign fu_units_2_io_req_bits_uop_br_prediction_bim_resp_value = io_req_bits_uop_br_prediction_bim_resp_value;
  assign fu_units_2_io_req_bits_uop_br_prediction_bim_resp_entry_idx = io_req_bits_uop_br_prediction_bim_resp_entry_idx;
  assign fu_units_2_io_req_bits_uop_br_prediction_bim_resp_way_idx = io_req_bits_uop_br_prediction_bim_resp_way_idx;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_takens = io_req_bits_uop_br_prediction_bpd_resp_takens;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history = io_req_bits_uop_br_prediction_bpd_resp_history;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_u = io_req_bits_uop_br_prediction_bpd_resp_history_u;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_history_ptr = io_req_bits_uop_br_prediction_bpd_resp_history_ptr;
  assign fu_units_2_io_req_bits_uop_br_prediction_bpd_resp_info = io_req_bits_uop_br_prediction_bpd_resp_info;
  assign fu_units_2_io_req_bits_uop_stat_brjmp_mispredicted = io_req_bits_uop_stat_brjmp_mispredicted;
  assign fu_units_2_io_req_bits_uop_stat_btb_made_pred = io_req_bits_uop_stat_btb_made_pred;
  assign fu_units_2_io_req_bits_uop_stat_btb_mispredicted = io_req_bits_uop_stat_btb_mispredicted;
  assign fu_units_2_io_req_bits_uop_stat_bpd_made_pred = io_req_bits_uop_stat_bpd_made_pred;
  assign fu_units_2_io_req_bits_uop_stat_bpd_mispredicted = io_req_bits_uop_stat_bpd_mispredicted;
  assign fu_units_2_io_req_bits_uop_fetch_pc_lob = io_req_bits_uop_fetch_pc_lob;
  assign fu_units_2_io_req_bits_uop_imm_packed = io_req_bits_uop_imm_packed;
  assign fu_units_2_io_req_bits_uop_csr_addr = io_req_bits_uop_csr_addr;
  assign fu_units_2_io_req_bits_uop_rob_idx = io_req_bits_uop_rob_idx;
  assign fu_units_2_io_req_bits_uop_ldq_idx = io_req_bits_uop_ldq_idx;
  assign fu_units_2_io_req_bits_uop_stq_idx = io_req_bits_uop_stq_idx;
  assign fu_units_2_io_req_bits_uop_brob_idx = io_req_bits_uop_brob_idx;
  assign fu_units_2_io_req_bits_uop_pdst = io_req_bits_uop_pdst;
  assign fu_units_2_io_req_bits_uop_pop1 = io_req_bits_uop_pop1;
  assign fu_units_2_io_req_bits_uop_pop2 = io_req_bits_uop_pop2;
  assign fu_units_2_io_req_bits_uop_pop3 = io_req_bits_uop_pop3;
  assign fu_units_2_io_req_bits_uop_prs1_busy = io_req_bits_uop_prs1_busy;
  assign fu_units_2_io_req_bits_uop_prs2_busy = io_req_bits_uop_prs2_busy;
  assign fu_units_2_io_req_bits_uop_prs3_busy = io_req_bits_uop_prs3_busy;
  assign fu_units_2_io_req_bits_uop_stale_pdst = io_req_bits_uop_stale_pdst;
  assign fu_units_2_io_req_bits_uop_exception = io_req_bits_uop_exception;
  assign fu_units_2_io_req_bits_uop_exc_cause = io_req_bits_uop_exc_cause;
  assign fu_units_2_io_req_bits_uop_bypassable = io_req_bits_uop_bypassable;
  assign fu_units_2_io_req_bits_uop_mem_cmd = io_req_bits_uop_mem_cmd;
  assign fu_units_2_io_req_bits_uop_mem_typ = io_req_bits_uop_mem_typ;
  assign fu_units_2_io_req_bits_uop_is_fence = io_req_bits_uop_is_fence;
  assign fu_units_2_io_req_bits_uop_is_fencei = io_req_bits_uop_is_fencei;
  assign fu_units_2_io_req_bits_uop_is_store = io_req_bits_uop_is_store;
  assign fu_units_2_io_req_bits_uop_is_amo = io_req_bits_uop_is_amo;
  assign fu_units_2_io_req_bits_uop_is_load = io_req_bits_uop_is_load;
  assign fu_units_2_io_req_bits_uop_is_sys_pc2epc = io_req_bits_uop_is_sys_pc2epc;
  assign fu_units_2_io_req_bits_uop_is_unique = io_req_bits_uop_is_unique;
  assign fu_units_2_io_req_bits_uop_flush_on_commit = io_req_bits_uop_flush_on_commit;
  assign fu_units_2_io_req_bits_uop_ldst = io_req_bits_uop_ldst;
  assign fu_units_2_io_req_bits_uop_lrs1 = io_req_bits_uop_lrs1;
  assign fu_units_2_io_req_bits_uop_lrs2 = io_req_bits_uop_lrs2;
  assign fu_units_2_io_req_bits_uop_lrs3 = io_req_bits_uop_lrs3;
  assign fu_units_2_io_req_bits_uop_ldst_val = io_req_bits_uop_ldst_val;
  assign fu_units_2_io_req_bits_uop_dst_rtype = io_req_bits_uop_dst_rtype;
  assign fu_units_2_io_req_bits_uop_lrs1_rtype = io_req_bits_uop_lrs1_rtype;
  assign fu_units_2_io_req_bits_uop_lrs2_rtype = io_req_bits_uop_lrs2_rtype;
  assign fu_units_2_io_req_bits_uop_frs3_en = io_req_bits_uop_frs3_en;
  assign fu_units_2_io_req_bits_uop_fp_val = io_req_bits_uop_fp_val;
  assign fu_units_2_io_req_bits_uop_fp_single = io_req_bits_uop_fp_single;
  assign fu_units_2_io_req_bits_uop_xcpt_pf_if = io_req_bits_uop_xcpt_pf_if;
  assign fu_units_2_io_req_bits_uop_xcpt_ae_if = io_req_bits_uop_xcpt_ae_if;
  assign fu_units_2_io_req_bits_uop_replay_if = io_req_bits_uop_replay_if;
  assign fu_units_2_io_req_bits_uop_xcpt_ma_if = io_req_bits_uop_xcpt_ma_if;
  assign fu_units_2_io_req_bits_uop_debug_wdata = io_req_bits_uop_debug_wdata;
  assign fu_units_2_io_req_bits_uop_debug_events_fetch_seq = io_req_bits_uop_debug_events_fetch_seq;
  assign fu_units_2_io_req_bits_rs1_data = io_req_bits_rs1_data;
  assign fu_units_2_io_req_bits_rs2_data = io_req_bits_rs2_data;
  assign fu_units_2_io_req_bits_kill = io_req_bits_kill;
  assign fu_units_2_io_resp_ready = _T_1336;
  assign fu_units_2_io_brinfo_valid = io_brinfo_valid;
  assign fu_units_2_io_brinfo_mispredict = io_brinfo_mispredict;
  assign fu_units_2_io_brinfo_mask = io_brinfo_mask;
  assign fu_units_2_clock = clock;
  assign fu_units_2_reset = reset;
  assign _T_1631_ctrl_rf_wen = _T_1714;
  assign _T_1631_rob_idx = _T_1679;
  assign _T_1631_pdst = _T_1675;
  assign _T_1631_bypassable = _T_1665;
  assign _T_1631_is_store = _T_1660;
  assign _T_1631_is_amo = _T_1659;
  assign _T_1631_dst_rtype = _T_1649;
  assign _T_1637 = _T_1625;
  always @(posedge clock) begin
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (1'h0) begin
          $fwrite(32'h80000002,"Assertion failed\n    at execute.scala:310 assert (io.resp(0).ready) // don'yet support back-pressuring this unit.\n");
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (1'h0) begin
          $fatal;
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1823) begin
          $fwrite(32'h80000002,"Assertion failed: Multiple functional units are fighting over the write port.\n    at execute.scala:324 assert ((PopCount(fu_units.map(_.io.resp.valid)) <= UInt(1) && !muldiv_resp_val && !fdiv_resp_val) ||\n");
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1823) begin
          $fatal;
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
  end
endmodule
