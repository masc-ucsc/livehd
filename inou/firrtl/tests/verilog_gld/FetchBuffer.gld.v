module FetchBuffer(
  input         clock,
  input         reset,
  output        io_enq_ready,
  input         io_enq_valid,
  input  [39:0] io_enq_bits_pc,
  input  [39:0] io_enq_bits_next_pc,
  input         io_enq_bits_edge_inst_0,
  input  [31:0] io_enq_bits_insts_0,
  input  [31:0] io_enq_bits_insts_1,
  input  [31:0] io_enq_bits_insts_2,
  input  [31:0] io_enq_bits_insts_3,
  input  [31:0] io_enq_bits_exp_insts_0,
  input  [31:0] io_enq_bits_exp_insts_1,
  input  [31:0] io_enq_bits_exp_insts_2,
  input  [31:0] io_enq_bits_exp_insts_3,
  input         io_enq_bits_sfbs_0,
  input         io_enq_bits_sfbs_1,
  input         io_enq_bits_sfbs_2,
  input         io_enq_bits_sfbs_3,
  input  [7:0]  io_enq_bits_sfb_masks_0,
  input  [7:0]  io_enq_bits_sfb_masks_1,
  input  [7:0]  io_enq_bits_sfb_masks_2,
  input  [7:0]  io_enq_bits_sfb_masks_3,
  input  [3:0]  io_enq_bits_sfb_dests_0,
  input  [3:0]  io_enq_bits_sfb_dests_1,
  input  [3:0]  io_enq_bits_sfb_dests_2,
  input  [3:0]  io_enq_bits_sfb_dests_3,
  input         io_enq_bits_shadowable_mask_0,
  input         io_enq_bits_shadowable_mask_1,
  input         io_enq_bits_shadowable_mask_2,
  input         io_enq_bits_shadowable_mask_3,
  input         io_enq_bits_shadowed_mask_0,
  input         io_enq_bits_shadowed_mask_1,
  input         io_enq_bits_shadowed_mask_2,
  input         io_enq_bits_shadowed_mask_3,
  input         io_enq_bits_cfi_idx_valid,
  input  [1:0]  io_enq_bits_cfi_idx_bits,
  input  [2:0]  io_enq_bits_cfi_type,
  input         io_enq_bits_cfi_is_call,
  input         io_enq_bits_cfi_is_ret,
  input         io_enq_bits_cfi_npc_plus4,
  input  [39:0] io_enq_bits_ras_top,
  input  [3:0]  io_enq_bits_ftq_idx,
  input  [3:0]  io_enq_bits_mask,
  input  [3:0]  io_enq_bits_br_mask,
  input  [63:0] io_enq_bits_ghist_old_history,
  input         io_enq_bits_ghist_current_saw_branch_not_taken,
  input         io_enq_bits_ghist_new_saw_branch_not_taken,
  input         io_enq_bits_ghist_new_saw_branch_taken,
  input  [4:0]  io_enq_bits_ghist_ras_idx,
  input         io_enq_bits_lhist_0,
  input         io_enq_bits_xcpt_pf_if,
  input         io_enq_bits_xcpt_ae_if,
  input         io_enq_bits_bp_debug_if_oh_0,
  input         io_enq_bits_bp_debug_if_oh_1,
  input         io_enq_bits_bp_debug_if_oh_2,
  input         io_enq_bits_bp_debug_if_oh_3,
  input         io_enq_bits_bp_xcpt_if_oh_0,
  input         io_enq_bits_bp_xcpt_if_oh_1,
  input         io_enq_bits_bp_xcpt_if_oh_2,
  input         io_enq_bits_bp_xcpt_if_oh_3,
  input         io_enq_bits_end_half_valid,
  input  [15:0] io_enq_bits_end_half_bits,
  input         io_enq_bits_bpd_meta_0,
  input  [1:0]  io_enq_bits_fsrc,
  input  [1:0]  io_enq_bits_tsrc,
  input         io_deq_ready,
  output        io_deq_valid,
  output        io_deq_bits_uops_0_valid,
  output [6:0]  io_deq_bits_uops_0_bits_uopc,
  output [31:0] io_deq_bits_uops_0_bits_inst,
  output [31:0] io_deq_bits_uops_0_bits_debug_inst,
  output        io_deq_bits_uops_0_bits_is_rvc,
  output [39:0] io_deq_bits_uops_0_bits_debug_pc,
  output [2:0]  io_deq_bits_uops_0_bits_iq_type,
  output [9:0]  io_deq_bits_uops_0_bits_fu_code,
  output [3:0]  io_deq_bits_uops_0_bits_ctrl_br_type,
  output [1:0]  io_deq_bits_uops_0_bits_ctrl_op1_sel,
  output [2:0]  io_deq_bits_uops_0_bits_ctrl_op2_sel,
  output [2:0]  io_deq_bits_uops_0_bits_ctrl_imm_sel,
  output [3:0]  io_deq_bits_uops_0_bits_ctrl_op_fcn,
  output        io_deq_bits_uops_0_bits_ctrl_fcn_dw,
  output [2:0]  io_deq_bits_uops_0_bits_ctrl_csr_cmd,
  output        io_deq_bits_uops_0_bits_ctrl_is_load,
  output        io_deq_bits_uops_0_bits_ctrl_is_sta,
  output        io_deq_bits_uops_0_bits_ctrl_is_std,
  output [1:0]  io_deq_bits_uops_0_bits_iw_state,
  output        io_deq_bits_uops_0_bits_iw_p1_poisoned,
  output        io_deq_bits_uops_0_bits_iw_p2_poisoned,
  output        io_deq_bits_uops_0_bits_is_br,
  output        io_deq_bits_uops_0_bits_is_jalr,
  output        io_deq_bits_uops_0_bits_is_jal,
  output        io_deq_bits_uops_0_bits_is_sfb,
  output [7:0]  io_deq_bits_uops_0_bits_br_mask,
  output [2:0]  io_deq_bits_uops_0_bits_br_tag,
  output [3:0]  io_deq_bits_uops_0_bits_ftq_idx,
  output        io_deq_bits_uops_0_bits_edge_inst,
  output [5:0]  io_deq_bits_uops_0_bits_pc_lob,
  output        io_deq_bits_uops_0_bits_taken,
  output [19:0] io_deq_bits_uops_0_bits_imm_packed,
  output [11:0] io_deq_bits_uops_0_bits_csr_addr,
  output [4:0]  io_deq_bits_uops_0_bits_rob_idx,
  output [2:0]  io_deq_bits_uops_0_bits_ldq_idx,
  output [2:0]  io_deq_bits_uops_0_bits_stq_idx,
  output [1:0]  io_deq_bits_uops_0_bits_rxq_idx,
  output [5:0]  io_deq_bits_uops_0_bits_pdst,
  output [5:0]  io_deq_bits_uops_0_bits_prs1,
  output [5:0]  io_deq_bits_uops_0_bits_prs2,
  output [5:0]  io_deq_bits_uops_0_bits_prs3,
  output [3:0]  io_deq_bits_uops_0_bits_ppred,
  output        io_deq_bits_uops_0_bits_prs1_busy,
  output        io_deq_bits_uops_0_bits_prs2_busy,
  output        io_deq_bits_uops_0_bits_prs3_busy,
  output        io_deq_bits_uops_0_bits_ppred_busy,
  output [5:0]  io_deq_bits_uops_0_bits_stale_pdst,
  output        io_deq_bits_uops_0_bits_exception,
  output [63:0] io_deq_bits_uops_0_bits_exc_cause,
  output        io_deq_bits_uops_0_bits_bypassable,
  output [4:0]  io_deq_bits_uops_0_bits_mem_cmd,
  output [1:0]  io_deq_bits_uops_0_bits_mem_size,
  output        io_deq_bits_uops_0_bits_mem_signed,
  output        io_deq_bits_uops_0_bits_is_fence,
  output        io_deq_bits_uops_0_bits_is_fencei,
  output        io_deq_bits_uops_0_bits_is_amo,
  output        io_deq_bits_uops_0_bits_uses_ldq,
  output        io_deq_bits_uops_0_bits_uses_stq,
  output        io_deq_bits_uops_0_bits_is_sys_pc2epc,
  output        io_deq_bits_uops_0_bits_is_unique,
  output        io_deq_bits_uops_0_bits_flush_on_commit,
  output        io_deq_bits_uops_0_bits_ldst_is_rs1,
  output [5:0]  io_deq_bits_uops_0_bits_ldst,
  output [5:0]  io_deq_bits_uops_0_bits_lrs1,
  output [5:0]  io_deq_bits_uops_0_bits_lrs2,
  output [5:0]  io_deq_bits_uops_0_bits_lrs3,
  output        io_deq_bits_uops_0_bits_ldst_val,
  output [1:0]  io_deq_bits_uops_0_bits_dst_rtype,
  output [1:0]  io_deq_bits_uops_0_bits_lrs1_rtype,
  output [1:0]  io_deq_bits_uops_0_bits_lrs2_rtype,
  output        io_deq_bits_uops_0_bits_frs3_en,
  output        io_deq_bits_uops_0_bits_fp_val,
  output        io_deq_bits_uops_0_bits_fp_single,
  output        io_deq_bits_uops_0_bits_xcpt_pf_if,
  output        io_deq_bits_uops_0_bits_xcpt_ae_if,
  output        io_deq_bits_uops_0_bits_xcpt_ma_if,
  output        io_deq_bits_uops_0_bits_bp_debug_if,
  output        io_deq_bits_uops_0_bits_bp_xcpt_if,
  output [1:0]  io_deq_bits_uops_0_bits_debug_fsrc,
  output [1:0]  io_deq_bits_uops_0_bits_debug_tsrc,
  input         io_clear
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [63:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_16;
  reg [63:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [63:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_35;
  reg [31:0] _RAND_36;
  reg [31:0] _RAND_37;
  reg [31:0] _RAND_38;
  reg [31:0] _RAND_39;
  reg [31:0] _RAND_40;
  reg [31:0] _RAND_41;
  reg [31:0] _RAND_42;
  reg [31:0] _RAND_43;
  reg [31:0] _RAND_44;
  reg [63:0] _RAND_45;
  reg [31:0] _RAND_46;
  reg [31:0] _RAND_47;
  reg [31:0] _RAND_48;
  reg [31:0] _RAND_49;
  reg [31:0] _RAND_50;
  reg [31:0] _RAND_51;
  reg [31:0] _RAND_52;
  reg [31:0] _RAND_53;
  reg [31:0] _RAND_54;
  reg [31:0] _RAND_55;
  reg [31:0] _RAND_56;
  reg [31:0] _RAND_57;
  reg [31:0] _RAND_58;
  reg [63:0] _RAND_59;
  reg [31:0] _RAND_60;
  reg [31:0] _RAND_61;
  reg [31:0] _RAND_62;
  reg [31:0] _RAND_63;
  reg [31:0] _RAND_64;
  reg [31:0] _RAND_65;
  reg [31:0] _RAND_66;
  reg [31:0] _RAND_67;
  reg [31:0] _RAND_68;
  reg [31:0] _RAND_69;
  reg [31:0] _RAND_70;
  reg [31:0] _RAND_71;
  reg [31:0] _RAND_72;
  reg [63:0] _RAND_73;
  reg [31:0] _RAND_74;
  reg [31:0] _RAND_75;
  reg [31:0] _RAND_76;
  reg [31:0] _RAND_77;
  reg [31:0] _RAND_78;
  reg [31:0] _RAND_79;
  reg [31:0] _RAND_80;
  reg [31:0] _RAND_81;
  reg [31:0] _RAND_82;
  reg [31:0] _RAND_83;
  reg [31:0] _RAND_84;
  reg [31:0] _RAND_85;
  reg [31:0] _RAND_86;
  reg [63:0] _RAND_87;
  reg [31:0] _RAND_88;
  reg [31:0] _RAND_89;
  reg [31:0] _RAND_90;
  reg [31:0] _RAND_91;
  reg [31:0] _RAND_92;
  reg [31:0] _RAND_93;
  reg [31:0] _RAND_94;
  reg [31:0] _RAND_95;
  reg [31:0] _RAND_96;
  reg [31:0] _RAND_97;
  reg [31:0] _RAND_98;
  reg [31:0] _RAND_99;
  reg [31:0] _RAND_100;
  reg [63:0] _RAND_101;
  reg [31:0] _RAND_102;
  reg [31:0] _RAND_103;
  reg [31:0] _RAND_104;
  reg [31:0] _RAND_105;
  reg [31:0] _RAND_106;
  reg [31:0] _RAND_107;
  reg [31:0] _RAND_108;
  reg [31:0] _RAND_109;
  reg [31:0] _RAND_110;
  reg [31:0] _RAND_111;
  reg [31:0] _RAND_112;
  reg [31:0] _RAND_113;
  reg [31:0] _RAND_114;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] fb_uop_ram_0_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_0_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_0_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_0_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_0_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_0_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_0_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_1_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_1_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_1_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_1_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_1_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_1_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_1_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_2_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_2_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_2_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_2_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_2_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_2_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_2_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_3_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_3_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_3_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_3_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_3_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_3_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_3_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_4_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_4_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_4_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_4_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_4_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_4_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_4_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_5_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_5_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_5_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_5_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_5_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_5_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_5_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_6_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_6_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_6_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_6_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_6_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_6_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_6_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_7_inst; // @[fetch-buffer.scala 57:16]
  reg [31:0] fb_uop_ram_7_debug_inst; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_is_rvc; // @[fetch-buffer.scala 57:16]
  reg [39:0] fb_uop_ram_7_debug_pc; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_is_sfb; // @[fetch-buffer.scala 57:16]
  reg [3:0] fb_uop_ram_7_ftq_idx; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_edge_inst; // @[fetch-buffer.scala 57:16]
  reg [5:0] fb_uop_ram_7_pc_lob; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_taken; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_xcpt_pf_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_xcpt_ae_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_bp_debug_if; // @[fetch-buffer.scala 57:16]
  reg  fb_uop_ram_7_bp_xcpt_if; // @[fetch-buffer.scala 57:16]
  reg [1:0] fb_uop_ram_7_debug_fsrc; // @[fetch-buffer.scala 57:16]
  reg [7:0] head; // @[fetch-buffer.scala 61:21]
  reg [7:0] tail; // @[fetch-buffer.scala 62:21]
  reg  maybe_full; // @[fetch-buffer.scala 64:27]
  wire [7:0] _T_2 = {tail[6:0],tail[7]}; // @[Cat.scala 29:58]
  wire [7:0] _T_18 = {_T_2[7],_T_2[6],_T_2[5],_T_2[4],_T_2[3],_T_2[2],_T_2[1],_T_2[0]}; // @[fetch-buffer.scala 79:63]
  wire [7:0] _T_21 = {tail[5:0],tail[7:6]}; // @[Cat.scala 29:58]
  wire [7:0] _T_37 = {_T_21[7],_T_21[6],_T_21[5],_T_21[4],_T_21[3],_T_21[2],_T_21[1],_T_21[0]}; // @[fetch-buffer.scala 79:63]
  wire [7:0] _T_40 = {tail[4:0],tail[7:5]}; // @[Cat.scala 29:58]
  wire [7:0] _T_56 = {_T_40[7],_T_40[6],_T_40[5],_T_40[4],_T_40[3],_T_40[2],_T_40[1],_T_40[0]}; // @[fetch-buffer.scala 79:63]
  wire [7:0] _T_57 = head & _T_18; // @[fetch-buffer.scala 79:88]
  wire [7:0] _T_58 = head & _T_37; // @[fetch-buffer.scala 79:88]
  wire [7:0] _T_59 = head & _T_56; // @[fetch-buffer.scala 79:88]
  wire [7:0] _T_60 = _T_57 | _T_58; // @[fetch-buffer.scala 79:104]
  wire [7:0] _T_61 = _T_60 | _T_59; // @[fetch-buffer.scala 79:104]
  wire  might_hit_head = |_T_61; // @[fetch-buffer.scala 79:108]
  wire [7:0] _T_77 = {tail[7],tail[6],tail[5],tail[4],tail[3],tail[2],tail[1],tail[0]}; // @[fetch-buffer.scala 81:29]
  wire [7:0] _T_78 = _T_77 & head; // @[fetch-buffer.scala 81:36]
  wire  at_head = |_T_78; // @[fetch-buffer.scala 81:44]
  wire  do_enq = ~(at_head & maybe_full | might_hit_head); // @[fetch-buffer.scala 82:16]
  wire [39:0] _T_81 = ~io_enq_bits_pc; // @[frontend.scala 162:33]
  wire [39:0] _T_82 = _T_81 | 40'h7; // @[frontend.scala 162:39]
  wire [39:0] _T_83 = ~_T_82; // @[frontend.scala 162:31]
  wire [40:0] _T_84 = {{1'd0}, _T_83}; // @[fetch-buffer.scala 95:43]
  wire  in_mask_0 = io_enq_valid & io_enq_bits_mask[0]; // @[fetch-buffer.scala 98:49]
  wire  in_uops_0_is_sfb = io_enq_bits_sfbs_0 | io_enq_bits_shadowed_mask_0; // @[fetch-buffer.scala 103:56]
  wire [39:0] _T_95 = _T_84[39:0] - 40'h2; // @[fetch-buffer.scala 107:81]
  wire [39:0] in_uops_0_debug_pc = io_enq_bits_edge_inst_0 ? _T_95 : _T_84[39:0]; // @[fetch-buffer.scala 106:41 107:32 100:33]
  wire [39:0] _GEN_1 = io_enq_bits_edge_inst_0 ? _T_84[39:0] : _T_84[39:0]; // @[fetch-buffer.scala 106:41 108:32 101:33]
  wire  in_uops_0_is_rvc = io_enq_bits_insts_0[1:0] != 2'h3; // @[fetch-buffer.scala 115:62]
  wire  in_uops_0_taken = io_enq_bits_cfi_idx_bits == 2'h0 & io_enq_bits_cfi_idx_valid; // @[fetch-buffer.scala 116:69]
  wire [39:0] in_uops_1_debug_pc = _T_83 + 40'h2; // @[fetch-buffer.scala 95:43]
  wire  in_mask_1 = io_enq_valid & io_enq_bits_mask[1]; // @[fetch-buffer.scala 98:49]
  wire  in_uops_1_is_sfb = io_enq_bits_sfbs_1 | io_enq_bits_shadowed_mask_1; // @[fetch-buffer.scala 103:56]
  wire  in_uops_1_is_rvc = io_enq_bits_insts_1[1:0] != 2'h3; // @[fetch-buffer.scala 115:62]
  wire  in_uops_1_taken = io_enq_bits_cfi_idx_bits == 2'h1 & io_enq_bits_cfi_idx_valid; // @[fetch-buffer.scala 116:69]
  wire [39:0] in_uops_2_debug_pc = _T_83 + 40'h4; // @[fetch-buffer.scala 95:43]
  wire  in_mask_2 = io_enq_valid & io_enq_bits_mask[2]; // @[fetch-buffer.scala 98:49]
  wire  in_uops_2_is_sfb = io_enq_bits_sfbs_2 | io_enq_bits_shadowed_mask_2; // @[fetch-buffer.scala 103:56]
  wire  in_uops_2_is_rvc = io_enq_bits_insts_2[1:0] != 2'h3; // @[fetch-buffer.scala 115:62]
  wire  in_uops_2_taken = io_enq_bits_cfi_idx_bits == 2'h2 & io_enq_bits_cfi_idx_valid; // @[fetch-buffer.scala 116:69]
  wire [39:0] in_uops_3_debug_pc = _T_83 + 40'h6; // @[fetch-buffer.scala 95:43]
  wire  in_mask_3 = io_enq_valid & io_enq_bits_mask[3]; // @[fetch-buffer.scala 98:49]
  wire  in_uops_3_is_sfb = io_enq_bits_sfbs_3 | io_enq_bits_shadowed_mask_3; // @[fetch-buffer.scala 103:56]
  wire  in_uops_3_is_rvc = io_enq_bits_insts_3[1:0] != 2'h3; // @[fetch-buffer.scala 115:62]
  wire  in_uops_3_taken = io_enq_bits_cfi_idx_bits == 2'h3 & io_enq_bits_cfi_idx_valid; // @[fetch-buffer.scala 116:69]
  wire [7:0] enq_idxs_1 = in_mask_0 ? _T_2 : tail; // @[fetch-buffer.scala 138:18]
  wire [7:0] _T_147 = {enq_idxs_1[6:0],enq_idxs_1[7]}; // @[Cat.scala 29:58]
  wire [7:0] enq_idxs_2 = in_mask_1 ? _T_147 : enq_idxs_1; // @[fetch-buffer.scala 138:18]
  wire [7:0] _T_151 = {enq_idxs_2[6:0],enq_idxs_2[7]}; // @[Cat.scala 29:58]
  wire [7:0] enq_idxs_3 = in_mask_2 ? _T_151 : enq_idxs_2; // @[fetch-buffer.scala 138:18]
  wire [7:0] _T_155 = {enq_idxs_3[6:0],enq_idxs_3[7]}; // @[Cat.scala 29:58]
  wire [5:0] in_uops_0_pc_lob = _GEN_1[5:0]; // @[fetch-buffer.scala 88:21]
  wire [5:0] in_uops_1_pc_lob = in_uops_1_debug_pc[5:0]; // @[fetch-buffer.scala 88:21 101:33]
  wire [5:0] in_uops_2_pc_lob = in_uops_2_debug_pc[5:0]; // @[fetch-buffer.scala 88:21 101:33]
  wire [5:0] in_uops_3_pc_lob = in_uops_3_debug_pc[5:0]; // @[fetch-buffer.scala 88:21 101:33]
  wire  _T_255 = head[0] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire  _T_259 = head[1] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire  _T_263 = head[2] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire  _T_267 = head[3] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire  _T_271 = head[4] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire  _T_275 = head[5] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire  _T_279 = head[6] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire  _T_283 = head[7] & ~maybe_full; // @[fetch-buffer.scala 155:45]
  wire [7:0] _T_291 = {_T_283,_T_279,_T_275,_T_271,_T_267,_T_263,_T_259,_T_255}; // @[fetch-buffer.scala 155:90]
  wire [7:0] tail_collisions = _T_291 & tail; // @[fetch-buffer.scala 155:97]
  wire  slot_will_hit_tail = tail_collisions[0] | tail_collisions[1] | tail_collisions[2] | tail_collisions[3] |
    tail_collisions[4] | tail_collisions[5] | tail_collisions[6] | tail_collisions[7]; // @[fetch-buffer.scala 156:112]
  wire  will_hit_tail = |slot_will_hit_tail; // @[fetch-buffer.scala 157:42]
  wire  do_deq = io_deq_ready & ~will_hit_tail; // @[fetch-buffer.scala 159:29]
  wire [1:0] _T_307 = {{1'd0}, slot_will_hit_tail}; // @[util.scala 384:30]
  wire  deq_valids_0 = ~_T_307[0]; // @[fetch-buffer.scala 161:21]
  wire [139:0] _T_355 = {96'h0,33'h0,2'h0,fb_uop_ram_0_xcpt_pf_if,fb_uop_ram_0_xcpt_ae_if,1'h0,fb_uop_ram_0_bp_debug_if,
    fb_uop_ram_0_bp_xcpt_if,fb_uop_ram_0_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_374 = {3'h0,fb_uop_ram_0_is_sfb,8'h0,3'h0,fb_uop_ram_0_ftq_idx,fb_uop_ram_0_edge_inst,
    fb_uop_ram_0_pc_lob,fb_uop_ram_0_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_395 = {7'h0,fb_uop_ram_0_inst,fb_uop_ram_0_debug_inst,fb_uop_ram_0_is_rvc,fb_uop_ram_0_debug_pc,22'h0,18'h0
    ,_T_374,_T_355}; // @[Mux.scala 27:72]
  wire [387:0] _T_396 = head[0] ? _T_395 : 388'h0; // @[Mux.scala 27:72]
  wire [139:0] _T_434 = {96'h0,33'h0,2'h0,fb_uop_ram_1_xcpt_pf_if,fb_uop_ram_1_xcpt_ae_if,1'h0,fb_uop_ram_1_bp_debug_if,
    fb_uop_ram_1_bp_xcpt_if,fb_uop_ram_1_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_453 = {3'h0,fb_uop_ram_1_is_sfb,8'h0,3'h0,fb_uop_ram_1_ftq_idx,fb_uop_ram_1_edge_inst,
    fb_uop_ram_1_pc_lob,fb_uop_ram_1_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_474 = {7'h0,fb_uop_ram_1_inst,fb_uop_ram_1_debug_inst,fb_uop_ram_1_is_rvc,fb_uop_ram_1_debug_pc,22'h0,18'h0
    ,_T_453,_T_434}; // @[Mux.scala 27:72]
  wire [387:0] _T_475 = head[1] ? _T_474 : 388'h0; // @[Mux.scala 27:72]
  wire [139:0] _T_513 = {96'h0,33'h0,2'h0,fb_uop_ram_2_xcpt_pf_if,fb_uop_ram_2_xcpt_ae_if,1'h0,fb_uop_ram_2_bp_debug_if,
    fb_uop_ram_2_bp_xcpt_if,fb_uop_ram_2_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_532 = {3'h0,fb_uop_ram_2_is_sfb,8'h0,3'h0,fb_uop_ram_2_ftq_idx,fb_uop_ram_2_edge_inst,
    fb_uop_ram_2_pc_lob,fb_uop_ram_2_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_553 = {7'h0,fb_uop_ram_2_inst,fb_uop_ram_2_debug_inst,fb_uop_ram_2_is_rvc,fb_uop_ram_2_debug_pc,22'h0,18'h0
    ,_T_532,_T_513}; // @[Mux.scala 27:72]
  wire [387:0] _T_554 = head[2] ? _T_553 : 388'h0; // @[Mux.scala 27:72]
  wire [139:0] _T_592 = {96'h0,33'h0,2'h0,fb_uop_ram_3_xcpt_pf_if,fb_uop_ram_3_xcpt_ae_if,1'h0,fb_uop_ram_3_bp_debug_if,
    fb_uop_ram_3_bp_xcpt_if,fb_uop_ram_3_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_611 = {3'h0,fb_uop_ram_3_is_sfb,8'h0,3'h0,fb_uop_ram_3_ftq_idx,fb_uop_ram_3_edge_inst,
    fb_uop_ram_3_pc_lob,fb_uop_ram_3_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_632 = {7'h0,fb_uop_ram_3_inst,fb_uop_ram_3_debug_inst,fb_uop_ram_3_is_rvc,fb_uop_ram_3_debug_pc,22'h0,18'h0
    ,_T_611,_T_592}; // @[Mux.scala 27:72]
  wire [387:0] _T_633 = head[3] ? _T_632 : 388'h0; // @[Mux.scala 27:72]
  wire [139:0] _T_671 = {96'h0,33'h0,2'h0,fb_uop_ram_4_xcpt_pf_if,fb_uop_ram_4_xcpt_ae_if,1'h0,fb_uop_ram_4_bp_debug_if,
    fb_uop_ram_4_bp_xcpt_if,fb_uop_ram_4_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_690 = {3'h0,fb_uop_ram_4_is_sfb,8'h0,3'h0,fb_uop_ram_4_ftq_idx,fb_uop_ram_4_edge_inst,
    fb_uop_ram_4_pc_lob,fb_uop_ram_4_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_711 = {7'h0,fb_uop_ram_4_inst,fb_uop_ram_4_debug_inst,fb_uop_ram_4_is_rvc,fb_uop_ram_4_debug_pc,22'h0,18'h0
    ,_T_690,_T_671}; // @[Mux.scala 27:72]
  wire [387:0] _T_712 = head[4] ? _T_711 : 388'h0; // @[Mux.scala 27:72]
  wire [139:0] _T_750 = {96'h0,33'h0,2'h0,fb_uop_ram_5_xcpt_pf_if,fb_uop_ram_5_xcpt_ae_if,1'h0,fb_uop_ram_5_bp_debug_if,
    fb_uop_ram_5_bp_xcpt_if,fb_uop_ram_5_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_769 = {3'h0,fb_uop_ram_5_is_sfb,8'h0,3'h0,fb_uop_ram_5_ftq_idx,fb_uop_ram_5_edge_inst,
    fb_uop_ram_5_pc_lob,fb_uop_ram_5_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_790 = {7'h0,fb_uop_ram_5_inst,fb_uop_ram_5_debug_inst,fb_uop_ram_5_is_rvc,fb_uop_ram_5_debug_pc,22'h0,18'h0
    ,_T_769,_T_750}; // @[Mux.scala 27:72]
  wire [387:0] _T_791 = head[5] ? _T_790 : 388'h0; // @[Mux.scala 27:72]
  wire [139:0] _T_829 = {96'h0,33'h0,2'h0,fb_uop_ram_6_xcpt_pf_if,fb_uop_ram_6_xcpt_ae_if,1'h0,fb_uop_ram_6_bp_debug_if,
    fb_uop_ram_6_bp_xcpt_if,fb_uop_ram_6_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_848 = {3'h0,fb_uop_ram_6_is_sfb,8'h0,3'h0,fb_uop_ram_6_ftq_idx,fb_uop_ram_6_edge_inst,
    fb_uop_ram_6_pc_lob,fb_uop_ram_6_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_869 = {7'h0,fb_uop_ram_6_inst,fb_uop_ram_6_debug_inst,fb_uop_ram_6_is_rvc,fb_uop_ram_6_debug_pc,22'h0,18'h0
    ,_T_848,_T_829}; // @[Mux.scala 27:72]
  wire [387:0] _T_870 = head[6] ? _T_869 : 388'h0; // @[Mux.scala 27:72]
  wire [139:0] _T_908 = {96'h0,33'h0,2'h0,fb_uop_ram_7_xcpt_pf_if,fb_uop_ram_7_xcpt_ae_if,1'h0,fb_uop_ram_7_bp_debug_if,
    fb_uop_ram_7_bp_xcpt_if,fb_uop_ram_7_debug_fsrc,2'h0}; // @[Mux.scala 27:72]
  wire [95:0] _T_927 = {3'h0,fb_uop_ram_7_is_sfb,8'h0,3'h0,fb_uop_ram_7_ftq_idx,fb_uop_ram_7_edge_inst,
    fb_uop_ram_7_pc_lob,fb_uop_ram_7_taken,69'h0}; // @[Mux.scala 27:72]
  wire [387:0] _T_948 = {7'h0,fb_uop_ram_7_inst,fb_uop_ram_7_debug_inst,fb_uop_ram_7_is_rvc,fb_uop_ram_7_debug_pc,22'h0,18'h0
    ,_T_927,_T_908}; // @[Mux.scala 27:72]
  wire [387:0] _T_949 = head[7] ? _T_948 : 388'h0; // @[Mux.scala 27:72]
  wire [387:0] _T_950 = _T_396 | _T_475; // @[Mux.scala 27:72]
  wire [387:0] _T_951 = _T_950 | _T_554; // @[Mux.scala 27:72]
  wire [387:0] _T_952 = _T_951 | _T_633; // @[Mux.scala 27:72]
  wire [387:0] _T_953 = _T_952 | _T_712; // @[Mux.scala 27:72]
  wire [387:0] _T_954 = _T_953 | _T_791; // @[Mux.scala 27:72]
  wire [387:0] _T_955 = _T_954 | _T_870; // @[Mux.scala 27:72]
  wire [387:0] _T_956 = _T_955 | _T_949; // @[Mux.scala 27:72]
  wire  _GEN_2531 = in_mask_0 | in_mask_1 | in_mask_2 | in_mask_3 | maybe_full; // @[fetch-buffer.scala 178:33 179:18 64:27]
  wire [7:0] _T_1043 = {head[6:0],head[7]}; // @[Cat.scala 29:58]
  assign io_enq_ready = ~(at_head & maybe_full | might_hit_head); // @[fetch-buffer.scala 82:16]
  assign io_deq_valid = ~_T_307[0]; // @[fetch-buffer.scala 161:21]
  assign io_deq_bits_uops_0_valid = reset ? 1'h0 : deq_valids_0; // @[fetch-buffer.scala 195:23 196:41 168:72]
  assign io_deq_bits_uops_0_bits_uopc = _T_956[387:381]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_inst = _T_956[380:349]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_debug_inst = _T_956[348:317]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_rvc = _T_956[316]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_debug_pc = _T_956[315:276]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_iq_type = _T_956[275:273]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_fu_code = _T_956[272:263]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_br_type = _T_956[262:259]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_op1_sel = _T_956[258:257]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_op2_sel = _T_956[256:254]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_imm_sel = _T_956[253:251]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_op_fcn = _T_956[250:247]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_fcn_dw = _T_956[246]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_csr_cmd = _T_956[245:243]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_is_load = _T_956[242]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_is_sta = _T_956[241]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ctrl_is_std = _T_956[240]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_iw_state = _T_956[239:238]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_iw_p1_poisoned = _T_956[237]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_iw_p2_poisoned = _T_956[236]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_br = _T_956[235]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_jalr = _T_956[234]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_jal = _T_956[233]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_sfb = _T_956[232]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_br_mask = _T_956[231:224]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_br_tag = _T_956[223:221]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ftq_idx = _T_956[220:217]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_edge_inst = _T_956[216]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_pc_lob = _T_956[215:210]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_taken = _T_956[209]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_imm_packed = _T_956[208:189]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_csr_addr = _T_956[188:177]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_rob_idx = _T_956[176:172]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ldq_idx = _T_956[171:169]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_stq_idx = _T_956[168:166]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_rxq_idx = _T_956[165:164]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_pdst = _T_956[163:158]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_prs1 = _T_956[157:152]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_prs2 = _T_956[151:146]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_prs3 = _T_956[145:140]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ppred = _T_956[139:136]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_prs1_busy = _T_956[135]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_prs2_busy = _T_956[134]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_prs3_busy = _T_956[133]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ppred_busy = _T_956[132]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_stale_pdst = _T_956[131:126]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_exception = _T_956[125]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_exc_cause = _T_956[124:61]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_bypassable = _T_956[60]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_mem_cmd = _T_956[59:55]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_mem_size = _T_956[54:53]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_mem_signed = _T_956[52]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_fence = _T_956[51]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_fencei = _T_956[50]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_amo = _T_956[49]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_uses_ldq = _T_956[48]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_uses_stq = _T_956[47]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_sys_pc2epc = _T_956[46]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_is_unique = _T_956[45]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_flush_on_commit = _T_956[44]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ldst_is_rs1 = _T_956[43]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ldst = _T_956[42:37]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_lrs1 = _T_956[36:31]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_lrs2 = _T_956[30:25]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_lrs3 = _T_956[24:19]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_ldst_val = _T_956[18]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_dst_rtype = _T_956[17:16]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_lrs1_rtype = _T_956[15:14]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_lrs2_rtype = _T_956[13:12]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_frs3_en = _T_956[11]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_fp_val = _T_956[10]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_fp_single = _T_956[9]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_xcpt_pf_if = _T_956[8]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_xcpt_ae_if = _T_956[7]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_xcpt_ma_if = _T_956[6]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_bp_debug_if = _T_956[5]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_bp_xcpt_if = _T_956[4]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_debug_fsrc = _T_956[3:2]; // @[Mux.scala 27:72]
  assign io_deq_bits_uops_0_bits_debug_tsrc = _T_956[1:0]; // @[Mux.scala 27:72]
  always @(posedge clock) begin
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[0]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_0_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[1]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_1_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[2]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_2_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[3]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_3_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[4]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_4_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[5]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_5_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[6]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_6_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_inst <= io_enq_bits_exp_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_inst <= io_enq_bits_exp_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_inst <= io_enq_bits_exp_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_inst <= io_enq_bits_exp_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_inst <= io_enq_bits_insts_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_inst <= io_enq_bits_insts_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_inst <= io_enq_bits_insts_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_inst <= io_enq_bits_insts_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_rvc <= in_uops_3_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_rvc <= in_uops_2_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_rvc <= in_uops_1_is_rvc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_rvc <= in_uops_0_is_rvc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_pc <= in_uops_3_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_pc <= in_uops_2_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_pc <= in_uops_1_debug_pc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_pc <= in_uops_0_debug_pc; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_sfb <= in_uops_3_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_sfb <= in_uops_2_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_sfb <= in_uops_1_is_sfb; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_is_sfb <= in_uops_0_is_sfb; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_ftq_idx <= io_enq_bits_ftq_idx; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_edge_inst <= 1'h0; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_edge_inst <= io_enq_bits_edge_inst_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_pc_lob <= in_uops_3_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_pc_lob <= in_uops_2_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_pc_lob <= in_uops_1_pc_lob; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_pc_lob <= in_uops_0_pc_lob; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_taken <= in_uops_3_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_taken <= in_uops_2_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_taken <= in_uops_1_taken; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_taken <= in_uops_0_taken; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_pf_if <= io_enq_bits_xcpt_pf_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_xcpt_ae_if <= io_enq_bits_xcpt_ae_if; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_debug_if <= io_enq_bits_bp_debug_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_debug_if <= io_enq_bits_bp_debug_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_debug_if <= io_enq_bits_bp_debug_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_debug_if <= io_enq_bits_bp_debug_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_3; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_2; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_1; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_bp_xcpt_if <= io_enq_bits_bp_xcpt_if_oh_0; // @[fetch-buffer.scala 145:16]
    end
    if (do_enq & in_mask_3 & enq_idxs_3[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_2 & enq_idxs_2[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_1 & enq_idxs_1[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end else if (do_enq & in_mask_0 & tail[7]) begin // @[fetch-buffer.scala 144:53]
      fb_uop_ram_7_debug_fsrc <= io_enq_bits_fsrc; // @[fetch-buffer.scala 145:16]
    end
    if (reset) begin // @[fetch-buffer.scala 61:21]
      head <= 8'h1; // @[fetch-buffer.scala 61:21]
    end else if (io_clear) begin // @[fetch-buffer.scala 188:19]
      head <= 8'h1; // @[fetch-buffer.scala 189:10]
    end else if (do_deq) begin // @[fetch-buffer.scala 183:17]
      head <= _T_1043; // @[fetch-buffer.scala 184:10]
    end
    if (reset) begin // @[fetch-buffer.scala 62:21]
      tail <= 8'h1; // @[fetch-buffer.scala 62:21]
    end else if (io_clear) begin // @[fetch-buffer.scala 188:19]
      tail <= 8'h1; // @[fetch-buffer.scala 190:10]
    end else if (do_enq) begin // @[fetch-buffer.scala 176:17]
      if (in_mask_3) begin // @[fetch-buffer.scala 138:18]
        tail <= _T_155;
      end else begin
        tail <= enq_idxs_3;
      end
    end
    if (reset) begin // @[fetch-buffer.scala 64:27]
      maybe_full <= 1'h0; // @[fetch-buffer.scala 64:27]
    end else if (io_clear) begin // @[fetch-buffer.scala 188:19]
      maybe_full <= 1'h0; // @[fetch-buffer.scala 191:16]
    end else if (do_deq) begin // @[fetch-buffer.scala 183:17]
      maybe_full <= 1'h0; // @[fetch-buffer.scala 185:16]
    end else if (do_enq) begin // @[fetch-buffer.scala 176:17]
      maybe_full <= _GEN_2531;
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
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  fb_uop_ram_0_inst = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  fb_uop_ram_0_debug_inst = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  fb_uop_ram_0_is_rvc = _RAND_2[0:0];
  _RAND_3 = {2{`RANDOM}};
  fb_uop_ram_0_debug_pc = _RAND_3[39:0];
  _RAND_4 = {1{`RANDOM}};
  fb_uop_ram_0_is_sfb = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  fb_uop_ram_0_ftq_idx = _RAND_5[3:0];
  _RAND_6 = {1{`RANDOM}};
  fb_uop_ram_0_edge_inst = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  fb_uop_ram_0_pc_lob = _RAND_7[5:0];
  _RAND_8 = {1{`RANDOM}};
  fb_uop_ram_0_taken = _RAND_8[0:0];
  _RAND_9 = {1{`RANDOM}};
  fb_uop_ram_0_xcpt_pf_if = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  fb_uop_ram_0_xcpt_ae_if = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  fb_uop_ram_0_bp_debug_if = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  fb_uop_ram_0_bp_xcpt_if = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  fb_uop_ram_0_debug_fsrc = _RAND_13[1:0];
  _RAND_14 = {1{`RANDOM}};
  fb_uop_ram_1_inst = _RAND_14[31:0];
  _RAND_15 = {1{`RANDOM}};
  fb_uop_ram_1_debug_inst = _RAND_15[31:0];
  _RAND_16 = {1{`RANDOM}};
  fb_uop_ram_1_is_rvc = _RAND_16[0:0];
  _RAND_17 = {2{`RANDOM}};
  fb_uop_ram_1_debug_pc = _RAND_17[39:0];
  _RAND_18 = {1{`RANDOM}};
  fb_uop_ram_1_is_sfb = _RAND_18[0:0];
  _RAND_19 = {1{`RANDOM}};
  fb_uop_ram_1_ftq_idx = _RAND_19[3:0];
  _RAND_20 = {1{`RANDOM}};
  fb_uop_ram_1_edge_inst = _RAND_20[0:0];
  _RAND_21 = {1{`RANDOM}};
  fb_uop_ram_1_pc_lob = _RAND_21[5:0];
  _RAND_22 = {1{`RANDOM}};
  fb_uop_ram_1_taken = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  fb_uop_ram_1_xcpt_pf_if = _RAND_23[0:0];
  _RAND_24 = {1{`RANDOM}};
  fb_uop_ram_1_xcpt_ae_if = _RAND_24[0:0];
  _RAND_25 = {1{`RANDOM}};
  fb_uop_ram_1_bp_debug_if = _RAND_25[0:0];
  _RAND_26 = {1{`RANDOM}};
  fb_uop_ram_1_bp_xcpt_if = _RAND_26[0:0];
  _RAND_27 = {1{`RANDOM}};
  fb_uop_ram_1_debug_fsrc = _RAND_27[1:0];
  _RAND_28 = {1{`RANDOM}};
  fb_uop_ram_2_inst = _RAND_28[31:0];
  _RAND_29 = {1{`RANDOM}};
  fb_uop_ram_2_debug_inst = _RAND_29[31:0];
  _RAND_30 = {1{`RANDOM}};
  fb_uop_ram_2_is_rvc = _RAND_30[0:0];
  _RAND_31 = {2{`RANDOM}};
  fb_uop_ram_2_debug_pc = _RAND_31[39:0];
  _RAND_32 = {1{`RANDOM}};
  fb_uop_ram_2_is_sfb = _RAND_32[0:0];
  _RAND_33 = {1{`RANDOM}};
  fb_uop_ram_2_ftq_idx = _RAND_33[3:0];
  _RAND_34 = {1{`RANDOM}};
  fb_uop_ram_2_edge_inst = _RAND_34[0:0];
  _RAND_35 = {1{`RANDOM}};
  fb_uop_ram_2_pc_lob = _RAND_35[5:0];
  _RAND_36 = {1{`RANDOM}};
  fb_uop_ram_2_taken = _RAND_36[0:0];
  _RAND_37 = {1{`RANDOM}};
  fb_uop_ram_2_xcpt_pf_if = _RAND_37[0:0];
  _RAND_38 = {1{`RANDOM}};
  fb_uop_ram_2_xcpt_ae_if = _RAND_38[0:0];
  _RAND_39 = {1{`RANDOM}};
  fb_uop_ram_2_bp_debug_if = _RAND_39[0:0];
  _RAND_40 = {1{`RANDOM}};
  fb_uop_ram_2_bp_xcpt_if = _RAND_40[0:0];
  _RAND_41 = {1{`RANDOM}};
  fb_uop_ram_2_debug_fsrc = _RAND_41[1:0];
  _RAND_42 = {1{`RANDOM}};
  fb_uop_ram_3_inst = _RAND_42[31:0];
  _RAND_43 = {1{`RANDOM}};
  fb_uop_ram_3_debug_inst = _RAND_43[31:0];
  _RAND_44 = {1{`RANDOM}};
  fb_uop_ram_3_is_rvc = _RAND_44[0:0];
  _RAND_45 = {2{`RANDOM}};
  fb_uop_ram_3_debug_pc = _RAND_45[39:0];
  _RAND_46 = {1{`RANDOM}};
  fb_uop_ram_3_is_sfb = _RAND_46[0:0];
  _RAND_47 = {1{`RANDOM}};
  fb_uop_ram_3_ftq_idx = _RAND_47[3:0];
  _RAND_48 = {1{`RANDOM}};
  fb_uop_ram_3_edge_inst = _RAND_48[0:0];
  _RAND_49 = {1{`RANDOM}};
  fb_uop_ram_3_pc_lob = _RAND_49[5:0];
  _RAND_50 = {1{`RANDOM}};
  fb_uop_ram_3_taken = _RAND_50[0:0];
  _RAND_51 = {1{`RANDOM}};
  fb_uop_ram_3_xcpt_pf_if = _RAND_51[0:0];
  _RAND_52 = {1{`RANDOM}};
  fb_uop_ram_3_xcpt_ae_if = _RAND_52[0:0];
  _RAND_53 = {1{`RANDOM}};
  fb_uop_ram_3_bp_debug_if = _RAND_53[0:0];
  _RAND_54 = {1{`RANDOM}};
  fb_uop_ram_3_bp_xcpt_if = _RAND_54[0:0];
  _RAND_55 = {1{`RANDOM}};
  fb_uop_ram_3_debug_fsrc = _RAND_55[1:0];
  _RAND_56 = {1{`RANDOM}};
  fb_uop_ram_4_inst = _RAND_56[31:0];
  _RAND_57 = {1{`RANDOM}};
  fb_uop_ram_4_debug_inst = _RAND_57[31:0];
  _RAND_58 = {1{`RANDOM}};
  fb_uop_ram_4_is_rvc = _RAND_58[0:0];
  _RAND_59 = {2{`RANDOM}};
  fb_uop_ram_4_debug_pc = _RAND_59[39:0];
  _RAND_60 = {1{`RANDOM}};
  fb_uop_ram_4_is_sfb = _RAND_60[0:0];
  _RAND_61 = {1{`RANDOM}};
  fb_uop_ram_4_ftq_idx = _RAND_61[3:0];
  _RAND_62 = {1{`RANDOM}};
  fb_uop_ram_4_edge_inst = _RAND_62[0:0];
  _RAND_63 = {1{`RANDOM}};
  fb_uop_ram_4_pc_lob = _RAND_63[5:0];
  _RAND_64 = {1{`RANDOM}};
  fb_uop_ram_4_taken = _RAND_64[0:0];
  _RAND_65 = {1{`RANDOM}};
  fb_uop_ram_4_xcpt_pf_if = _RAND_65[0:0];
  _RAND_66 = {1{`RANDOM}};
  fb_uop_ram_4_xcpt_ae_if = _RAND_66[0:0];
  _RAND_67 = {1{`RANDOM}};
  fb_uop_ram_4_bp_debug_if = _RAND_67[0:0];
  _RAND_68 = {1{`RANDOM}};
  fb_uop_ram_4_bp_xcpt_if = _RAND_68[0:0];
  _RAND_69 = {1{`RANDOM}};
  fb_uop_ram_4_debug_fsrc = _RAND_69[1:0];
  _RAND_70 = {1{`RANDOM}};
  fb_uop_ram_5_inst = _RAND_70[31:0];
  _RAND_71 = {1{`RANDOM}};
  fb_uop_ram_5_debug_inst = _RAND_71[31:0];
  _RAND_72 = {1{`RANDOM}};
  fb_uop_ram_5_is_rvc = _RAND_72[0:0];
  _RAND_73 = {2{`RANDOM}};
  fb_uop_ram_5_debug_pc = _RAND_73[39:0];
  _RAND_74 = {1{`RANDOM}};
  fb_uop_ram_5_is_sfb = _RAND_74[0:0];
  _RAND_75 = {1{`RANDOM}};
  fb_uop_ram_5_ftq_idx = _RAND_75[3:0];
  _RAND_76 = {1{`RANDOM}};
  fb_uop_ram_5_edge_inst = _RAND_76[0:0];
  _RAND_77 = {1{`RANDOM}};
  fb_uop_ram_5_pc_lob = _RAND_77[5:0];
  _RAND_78 = {1{`RANDOM}};
  fb_uop_ram_5_taken = _RAND_78[0:0];
  _RAND_79 = {1{`RANDOM}};
  fb_uop_ram_5_xcpt_pf_if = _RAND_79[0:0];
  _RAND_80 = {1{`RANDOM}};
  fb_uop_ram_5_xcpt_ae_if = _RAND_80[0:0];
  _RAND_81 = {1{`RANDOM}};
  fb_uop_ram_5_bp_debug_if = _RAND_81[0:0];
  _RAND_82 = {1{`RANDOM}};
  fb_uop_ram_5_bp_xcpt_if = _RAND_82[0:0];
  _RAND_83 = {1{`RANDOM}};
  fb_uop_ram_5_debug_fsrc = _RAND_83[1:0];
  _RAND_84 = {1{`RANDOM}};
  fb_uop_ram_6_inst = _RAND_84[31:0];
  _RAND_85 = {1{`RANDOM}};
  fb_uop_ram_6_debug_inst = _RAND_85[31:0];
  _RAND_86 = {1{`RANDOM}};
  fb_uop_ram_6_is_rvc = _RAND_86[0:0];
  _RAND_87 = {2{`RANDOM}};
  fb_uop_ram_6_debug_pc = _RAND_87[39:0];
  _RAND_88 = {1{`RANDOM}};
  fb_uop_ram_6_is_sfb = _RAND_88[0:0];
  _RAND_89 = {1{`RANDOM}};
  fb_uop_ram_6_ftq_idx = _RAND_89[3:0];
  _RAND_90 = {1{`RANDOM}};
  fb_uop_ram_6_edge_inst = _RAND_90[0:0];
  _RAND_91 = {1{`RANDOM}};
  fb_uop_ram_6_pc_lob = _RAND_91[5:0];
  _RAND_92 = {1{`RANDOM}};
  fb_uop_ram_6_taken = _RAND_92[0:0];
  _RAND_93 = {1{`RANDOM}};
  fb_uop_ram_6_xcpt_pf_if = _RAND_93[0:0];
  _RAND_94 = {1{`RANDOM}};
  fb_uop_ram_6_xcpt_ae_if = _RAND_94[0:0];
  _RAND_95 = {1{`RANDOM}};
  fb_uop_ram_6_bp_debug_if = _RAND_95[0:0];
  _RAND_96 = {1{`RANDOM}};
  fb_uop_ram_6_bp_xcpt_if = _RAND_96[0:0];
  _RAND_97 = {1{`RANDOM}};
  fb_uop_ram_6_debug_fsrc = _RAND_97[1:0];
  _RAND_98 = {1{`RANDOM}};
  fb_uop_ram_7_inst = _RAND_98[31:0];
  _RAND_99 = {1{`RANDOM}};
  fb_uop_ram_7_debug_inst = _RAND_99[31:0];
  _RAND_100 = {1{`RANDOM}};
  fb_uop_ram_7_is_rvc = _RAND_100[0:0];
  _RAND_101 = {2{`RANDOM}};
  fb_uop_ram_7_debug_pc = _RAND_101[39:0];
  _RAND_102 = {1{`RANDOM}};
  fb_uop_ram_7_is_sfb = _RAND_102[0:0];
  _RAND_103 = {1{`RANDOM}};
  fb_uop_ram_7_ftq_idx = _RAND_103[3:0];
  _RAND_104 = {1{`RANDOM}};
  fb_uop_ram_7_edge_inst = _RAND_104[0:0];
  _RAND_105 = {1{`RANDOM}};
  fb_uop_ram_7_pc_lob = _RAND_105[5:0];
  _RAND_106 = {1{`RANDOM}};
  fb_uop_ram_7_taken = _RAND_106[0:0];
  _RAND_107 = {1{`RANDOM}};
  fb_uop_ram_7_xcpt_pf_if = _RAND_107[0:0];
  _RAND_108 = {1{`RANDOM}};
  fb_uop_ram_7_xcpt_ae_if = _RAND_108[0:0];
  _RAND_109 = {1{`RANDOM}};
  fb_uop_ram_7_bp_debug_if = _RAND_109[0:0];
  _RAND_110 = {1{`RANDOM}};
  fb_uop_ram_7_bp_xcpt_if = _RAND_110[0:0];
  _RAND_111 = {1{`RANDOM}};
  fb_uop_ram_7_debug_fsrc = _RAND_111[1:0];
  _RAND_112 = {1{`RANDOM}};
  head = _RAND_112[7:0];
  _RAND_113 = {1{`RANDOM}};
  tail = _RAND_113[7:0];
  _RAND_114 = {1{`RANDOM}};
  maybe_full = _RAND_114[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
