module FetchTargetQueue(
  input          clock,
  input          reset,
  output         io_enq_ready,
  input          io_enq_valid,
  input  [39:0]  io_enq_bits_pc,
  input  [39:0]  io_enq_bits_next_pc,
  input          io_enq_bits_edge_inst_0,
  input  [31:0]  io_enq_bits_insts_0,
  input  [31:0]  io_enq_bits_insts_1,
  input  [31:0]  io_enq_bits_insts_2,
  input  [31:0]  io_enq_bits_insts_3,
  input  [31:0]  io_enq_bits_exp_insts_0,
  input  [31:0]  io_enq_bits_exp_insts_1,
  input  [31:0]  io_enq_bits_exp_insts_2,
  input  [31:0]  io_enq_bits_exp_insts_3,
  input          io_enq_bits_sfbs_0,
  input          io_enq_bits_sfbs_1,
  input          io_enq_bits_sfbs_2,
  input          io_enq_bits_sfbs_3,
  input  [7:0]   io_enq_bits_sfb_masks_0,
  input  [7:0]   io_enq_bits_sfb_masks_1,
  input  [7:0]   io_enq_bits_sfb_masks_2,
  input  [7:0]   io_enq_bits_sfb_masks_3,
  input  [3:0]   io_enq_bits_sfb_dests_0,
  input  [3:0]   io_enq_bits_sfb_dests_1,
  input  [3:0]   io_enq_bits_sfb_dests_2,
  input  [3:0]   io_enq_bits_sfb_dests_3,
  input          io_enq_bits_shadowable_mask_0,
  input          io_enq_bits_shadowable_mask_1,
  input          io_enq_bits_shadowable_mask_2,
  input          io_enq_bits_shadowable_mask_3,
  input          io_enq_bits_shadowed_mask_0,
  input          io_enq_bits_shadowed_mask_1,
  input          io_enq_bits_shadowed_mask_2,
  input          io_enq_bits_shadowed_mask_3,
  input          io_enq_bits_cfi_idx_valid,
  input  [1:0]   io_enq_bits_cfi_idx_bits,
  input  [2:0]   io_enq_bits_cfi_type,
  input          io_enq_bits_cfi_is_call,
  input          io_enq_bits_cfi_is_ret,
  input          io_enq_bits_cfi_npc_plus4,
  input  [39:0]  io_enq_bits_ras_top,
  input  [3:0]   io_enq_bits_ftq_idx,
  input  [3:0]   io_enq_bits_mask,
  input  [3:0]   io_enq_bits_br_mask,
  input  [63:0]  io_enq_bits_ghist_old_history,
  input          io_enq_bits_ghist_current_saw_branch_not_taken,
  input          io_enq_bits_ghist_new_saw_branch_not_taken,
  input          io_enq_bits_ghist_new_saw_branch_taken,
  input  [4:0]   io_enq_bits_ghist_ras_idx,
  input          io_enq_bits_lhist_0,
  input          io_enq_bits_xcpt_pf_if,
  input          io_enq_bits_xcpt_ae_if,
  input          io_enq_bits_bp_debug_if_oh_0,
  input          io_enq_bits_bp_debug_if_oh_1,
  input          io_enq_bits_bp_debug_if_oh_2,
  input          io_enq_bits_bp_debug_if_oh_3,
  input          io_enq_bits_bp_xcpt_if_oh_0,
  input          io_enq_bits_bp_xcpt_if_oh_1,
  input          io_enq_bits_bp_xcpt_if_oh_2,
  input          io_enq_bits_bp_xcpt_if_oh_3,
  input          io_enq_bits_end_half_valid,
  input  [15:0]  io_enq_bits_end_half_bits,
  input          io_enq_bits_bpd_meta_0,
  input  [1:0]   io_enq_bits_fsrc,
  input  [1:0]   io_enq_bits_tsrc,
  output [3:0]   io_enq_idx,
  input          io_deq_valid,
  input  [3:0]   io_deq_bits,
  input  [3:0]   io_get_ftq_pc_0_ftq_idx,
  output         io_get_ftq_pc_0_entry_cfi_idx_valid,
  output [1:0]   io_get_ftq_pc_0_entry_cfi_idx_bits,
  output         io_get_ftq_pc_0_entry_cfi_taken,
  output         io_get_ftq_pc_0_entry_cfi_mispredicted,
  output [2:0]   io_get_ftq_pc_0_entry_cfi_type,
  output [3:0]   io_get_ftq_pc_0_entry_br_mask,
  output         io_get_ftq_pc_0_entry_cfi_is_call,
  output         io_get_ftq_pc_0_entry_cfi_is_ret,
  output         io_get_ftq_pc_0_entry_cfi_npc_plus4,
  output [39:0]  io_get_ftq_pc_0_entry_ras_top,
  output [4:0]   io_get_ftq_pc_0_entry_ras_idx,
  output         io_get_ftq_pc_0_entry_start_bank,
  output [63:0]  io_get_ftq_pc_0_ghist_old_history,
  output         io_get_ftq_pc_0_ghist_current_saw_branch_not_taken,
  output         io_get_ftq_pc_0_ghist_new_saw_branch_not_taken,
  output         io_get_ftq_pc_0_ghist_new_saw_branch_taken,
  output [4:0]   io_get_ftq_pc_0_ghist_ras_idx,
  output [39:0]  io_get_ftq_pc_0_pc,
  output [39:0]  io_get_ftq_pc_0_com_pc,
  output         io_get_ftq_pc_0_next_val,
  output [39:0]  io_get_ftq_pc_0_next_pc,
  input  [3:0]   io_get_ftq_pc_1_ftq_idx,
  output         io_get_ftq_pc_1_entry_cfi_idx_valid,
  output [1:0]   io_get_ftq_pc_1_entry_cfi_idx_bits,
  output         io_get_ftq_pc_1_entry_cfi_taken,
  output         io_get_ftq_pc_1_entry_cfi_mispredicted,
  output [2:0]   io_get_ftq_pc_1_entry_cfi_type,
  output [3:0]   io_get_ftq_pc_1_entry_br_mask,
  output         io_get_ftq_pc_1_entry_cfi_is_call,
  output         io_get_ftq_pc_1_entry_cfi_is_ret,
  output         io_get_ftq_pc_1_entry_cfi_npc_plus4,
  output [39:0]  io_get_ftq_pc_1_entry_ras_top,
  output [4:0]   io_get_ftq_pc_1_entry_ras_idx,
  output         io_get_ftq_pc_1_entry_start_bank,
  output [63:0]  io_get_ftq_pc_1_ghist_old_history,
  output         io_get_ftq_pc_1_ghist_current_saw_branch_not_taken,
  output         io_get_ftq_pc_1_ghist_new_saw_branch_not_taken,
  output         io_get_ftq_pc_1_ghist_new_saw_branch_taken,
  output [4:0]   io_get_ftq_pc_1_ghist_ras_idx,
  output [39:0]  io_get_ftq_pc_1_pc,
  output [39:0]  io_get_ftq_pc_1_com_pc,
  output         io_get_ftq_pc_1_next_val,
  output [39:0]  io_get_ftq_pc_1_next_pc,
  input  [3:0]   io_debug_ftq_idx_0,
  output [39:0]  io_debug_fetch_pc_0,
  input          io_redirect_valid,
  input  [3:0]   io_redirect_bits,
  input  [7:0]   io_brupdate_b1_resolve_mask,
  input  [7:0]   io_brupdate_b1_mispredict_mask,
  input  [6:0]   io_brupdate_b2_uop_uopc,
  input  [31:0]  io_brupdate_b2_uop_inst,
  input  [31:0]  io_brupdate_b2_uop_debug_inst,
  input          io_brupdate_b2_uop_is_rvc,
  input  [39:0]  io_brupdate_b2_uop_debug_pc,
  input  [2:0]   io_brupdate_b2_uop_iq_type,
  input  [9:0]   io_brupdate_b2_uop_fu_code,
  input  [3:0]   io_brupdate_b2_uop_ctrl_br_type,
  input  [1:0]   io_brupdate_b2_uop_ctrl_op1_sel,
  input  [2:0]   io_brupdate_b2_uop_ctrl_op2_sel,
  input  [2:0]   io_brupdate_b2_uop_ctrl_imm_sel,
  input  [3:0]   io_brupdate_b2_uop_ctrl_op_fcn,
  input          io_brupdate_b2_uop_ctrl_fcn_dw,
  input  [2:0]   io_brupdate_b2_uop_ctrl_csr_cmd,
  input          io_brupdate_b2_uop_ctrl_is_load,
  input          io_brupdate_b2_uop_ctrl_is_sta,
  input          io_brupdate_b2_uop_ctrl_is_std,
  input  [1:0]   io_brupdate_b2_uop_iw_state,
  input          io_brupdate_b2_uop_iw_p1_poisoned,
  input          io_brupdate_b2_uop_iw_p2_poisoned,
  input          io_brupdate_b2_uop_is_br,
  input          io_brupdate_b2_uop_is_jalr,
  input          io_brupdate_b2_uop_is_jal,
  input          io_brupdate_b2_uop_is_sfb,
  input  [7:0]   io_brupdate_b2_uop_br_mask,
  input  [2:0]   io_brupdate_b2_uop_br_tag,
  input  [3:0]   io_brupdate_b2_uop_ftq_idx,
  input          io_brupdate_b2_uop_edge_inst,
  input  [5:0]   io_brupdate_b2_uop_pc_lob,
  input          io_brupdate_b2_uop_taken,
  input  [19:0]  io_brupdate_b2_uop_imm_packed,
  input  [11:0]  io_brupdate_b2_uop_csr_addr,
  input  [4:0]   io_brupdate_b2_uop_rob_idx,
  input  [2:0]   io_brupdate_b2_uop_ldq_idx,
  input  [2:0]   io_brupdate_b2_uop_stq_idx,
  input  [1:0]   io_brupdate_b2_uop_rxq_idx,
  input  [5:0]   io_brupdate_b2_uop_pdst,
  input  [5:0]   io_brupdate_b2_uop_prs1,
  input  [5:0]   io_brupdate_b2_uop_prs2,
  input  [5:0]   io_brupdate_b2_uop_prs3,
  input  [3:0]   io_brupdate_b2_uop_ppred,
  input          io_brupdate_b2_uop_prs1_busy,
  input          io_brupdate_b2_uop_prs2_busy,
  input          io_brupdate_b2_uop_prs3_busy,
  input          io_brupdate_b2_uop_ppred_busy,
  input  [5:0]   io_brupdate_b2_uop_stale_pdst,
  input          io_brupdate_b2_uop_exception,
  input  [63:0]  io_brupdate_b2_uop_exc_cause,
  input          io_brupdate_b2_uop_bypassable,
  input  [4:0]   io_brupdate_b2_uop_mem_cmd,
  input  [1:0]   io_brupdate_b2_uop_mem_size,
  input          io_brupdate_b2_uop_mem_signed,
  input          io_brupdate_b2_uop_is_fence,
  input          io_brupdate_b2_uop_is_fencei,
  input          io_brupdate_b2_uop_is_amo,
  input          io_brupdate_b2_uop_uses_ldq,
  input          io_brupdate_b2_uop_uses_stq,
  input          io_brupdate_b2_uop_is_sys_pc2epc,
  input          io_brupdate_b2_uop_is_unique,
  input          io_brupdate_b2_uop_flush_on_commit,
  input          io_brupdate_b2_uop_ldst_is_rs1,
  input  [5:0]   io_brupdate_b2_uop_ldst,
  input  [5:0]   io_brupdate_b2_uop_lrs1,
  input  [5:0]   io_brupdate_b2_uop_lrs2,
  input  [5:0]   io_brupdate_b2_uop_lrs3,
  input          io_brupdate_b2_uop_ldst_val,
  input  [1:0]   io_brupdate_b2_uop_dst_rtype,
  input  [1:0]   io_brupdate_b2_uop_lrs1_rtype,
  input  [1:0]   io_brupdate_b2_uop_lrs2_rtype,
  input          io_brupdate_b2_uop_frs3_en,
  input          io_brupdate_b2_uop_fp_val,
  input          io_brupdate_b2_uop_fp_single,
  input          io_brupdate_b2_uop_xcpt_pf_if,
  input          io_brupdate_b2_uop_xcpt_ae_if,
  input          io_brupdate_b2_uop_xcpt_ma_if,
  input          io_brupdate_b2_uop_bp_debug_if,
  input          io_brupdate_b2_uop_bp_xcpt_if,
  input  [1:0]   io_brupdate_b2_uop_debug_fsrc,
  input  [1:0]   io_brupdate_b2_uop_debug_tsrc,
  input          io_brupdate_b2_valid,
  input          io_brupdate_b2_mispredict,
  input          io_brupdate_b2_taken,
  input  [2:0]   io_brupdate_b2_cfi_type,
  input  [1:0]   io_brupdate_b2_pc_sel,
  input  [39:0]  io_brupdate_b2_jalr_target,
  input          io_brupdate_b2_target_offset,
  output         io_bpdupdate_valid,
  output         io_bpdupdate_bits_is_mispredict_update,
  output         io_bpdupdate_bits_is_repair_update,
  output [3:0]   io_bpdupdate_bits_btb_mispredicts,
  output [39:0]  io_bpdupdate_bits_pc,
  output [3:0]   io_bpdupdate_bits_br_mask,
  output         io_bpdupdate_bits_cfi_idx_valid,
  output [1:0]   io_bpdupdate_bits_cfi_idx_bits,
  output         io_bpdupdate_bits_cfi_taken,
  output         io_bpdupdate_bits_cfi_mispredicted,
  output         io_bpdupdate_bits_cfi_is_br,
  output         io_bpdupdate_bits_cfi_is_jal,
  output         io_bpdupdate_bits_cfi_is_jalr,
  output [63:0]  io_bpdupdate_bits_ghist_old_history,
  output         io_bpdupdate_bits_ghist_current_saw_branch_not_taken,
  output         io_bpdupdate_bits_ghist_new_saw_branch_not_taken,
  output         io_bpdupdate_bits_ghist_new_saw_branch_taken,
  output [4:0]   io_bpdupdate_bits_ghist_ras_idx,
  output         io_bpdupdate_bits_lhist_0,
  output [39:0]  io_bpdupdate_bits_target,
  output [119:0] io_bpdupdate_bits_meta_0,
  output         io_ras_update,
  output [4:0]   io_ras_update_idx,
  output [39:0]  io_ras_update_pc
);
`ifdef RANDOMIZE_MEM_INIT
  reg [127:0] _RAND_0;
  reg [63:0] _RAND_3;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_15;
  reg [63:0] _RAND_18;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_30;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_35;
  reg [63:0] _RAND_36;
  reg [63:0] _RAND_37;
  reg [63:0] _RAND_38;
  reg [63:0] _RAND_39;
  reg [63:0] _RAND_40;
  reg [63:0] _RAND_41;
  reg [63:0] _RAND_42;
  reg [63:0] _RAND_43;
  reg [63:0] _RAND_44;
  reg [63:0] _RAND_45;
  reg [63:0] _RAND_46;
  reg [63:0] _RAND_47;
  reg [63:0] _RAND_48;
  reg [63:0] _RAND_49;
  reg [63:0] _RAND_50;
  reg [63:0] _RAND_51;
  reg [31:0] _RAND_52;
  reg [31:0] _RAND_53;
  reg [31:0] _RAND_54;
  reg [31:0] _RAND_55;
  reg [31:0] _RAND_56;
  reg [31:0] _RAND_57;
  reg [31:0] _RAND_58;
  reg [31:0] _RAND_59;
  reg [31:0] _RAND_60;
  reg [63:0] _RAND_61;
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
  reg [63:0] _RAND_85;
  reg [31:0] _RAND_86;
  reg [31:0] _RAND_87;
  reg [31:0] _RAND_88;
  reg [31:0] _RAND_89;
  reg [31:0] _RAND_90;
  reg [31:0] _RAND_91;
  reg [31:0] _RAND_92;
  reg [31:0] _RAND_93;
  reg [31:0] _RAND_94;
  reg [31:0] _RAND_95;
  reg [31:0] _RAND_96;
  reg [63:0] _RAND_97;
  reg [31:0] _RAND_98;
  reg [31:0] _RAND_99;
  reg [31:0] _RAND_100;
  reg [31:0] _RAND_101;
  reg [31:0] _RAND_102;
  reg [31:0] _RAND_103;
  reg [31:0] _RAND_104;
  reg [31:0] _RAND_105;
  reg [31:0] _RAND_106;
  reg [31:0] _RAND_107;
  reg [31:0] _RAND_108;
  reg [63:0] _RAND_109;
  reg [31:0] _RAND_110;
  reg [31:0] _RAND_111;
  reg [31:0] _RAND_112;
  reg [31:0] _RAND_113;
  reg [31:0] _RAND_114;
  reg [31:0] _RAND_115;
  reg [31:0] _RAND_116;
  reg [31:0] _RAND_117;
  reg [31:0] _RAND_118;
  reg [31:0] _RAND_119;
  reg [31:0] _RAND_120;
  reg [63:0] _RAND_121;
  reg [31:0] _RAND_122;
  reg [31:0] _RAND_123;
  reg [31:0] _RAND_124;
  reg [31:0] _RAND_125;
  reg [31:0] _RAND_126;
  reg [31:0] _RAND_127;
  reg [31:0] _RAND_128;
  reg [31:0] _RAND_129;
  reg [31:0] _RAND_130;
  reg [31:0] _RAND_131;
  reg [31:0] _RAND_132;
  reg [63:0] _RAND_133;
  reg [31:0] _RAND_134;
  reg [31:0] _RAND_135;
  reg [31:0] _RAND_136;
  reg [31:0] _RAND_137;
  reg [31:0] _RAND_138;
  reg [31:0] _RAND_139;
  reg [31:0] _RAND_140;
  reg [31:0] _RAND_141;
  reg [31:0] _RAND_142;
  reg [31:0] _RAND_143;
  reg [31:0] _RAND_144;
  reg [63:0] _RAND_145;
  reg [31:0] _RAND_146;
  reg [31:0] _RAND_147;
  reg [31:0] _RAND_148;
  reg [31:0] _RAND_149;
  reg [31:0] _RAND_150;
  reg [31:0] _RAND_151;
  reg [31:0] _RAND_152;
  reg [31:0] _RAND_153;
  reg [31:0] _RAND_154;
  reg [31:0] _RAND_155;
  reg [31:0] _RAND_156;
  reg [63:0] _RAND_157;
  reg [31:0] _RAND_158;
  reg [31:0] _RAND_159;
  reg [31:0] _RAND_160;
  reg [31:0] _RAND_161;
  reg [31:0] _RAND_162;
  reg [31:0] _RAND_163;
  reg [31:0] _RAND_164;
  reg [31:0] _RAND_165;
  reg [31:0] _RAND_166;
  reg [31:0] _RAND_167;
  reg [31:0] _RAND_168;
  reg [63:0] _RAND_169;
  reg [31:0] _RAND_170;
  reg [31:0] _RAND_171;
  reg [31:0] _RAND_172;
  reg [31:0] _RAND_173;
  reg [31:0] _RAND_174;
  reg [31:0] _RAND_175;
  reg [31:0] _RAND_176;
  reg [31:0] _RAND_177;
  reg [31:0] _RAND_178;
  reg [31:0] _RAND_179;
  reg [31:0] _RAND_180;
  reg [63:0] _RAND_181;
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
  reg [63:0] _RAND_193;
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
  reg [63:0] _RAND_205;
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
  reg [63:0] _RAND_217;
  reg [31:0] _RAND_218;
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
  reg [63:0] _RAND_229;
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
  reg [63:0] _RAND_241;
  reg [31:0] _RAND_242;
  reg [31:0] _RAND_243;
  reg [63:0] _RAND_244;
  reg [31:0] _RAND_245;
  reg [31:0] _RAND_246;
  reg [31:0] _RAND_247;
  reg [31:0] _RAND_248;
  reg [31:0] _RAND_249;
  reg [31:0] _RAND_250;
  reg [31:0] _RAND_251;
  reg [31:0] _RAND_252;
  reg [31:0] _RAND_253;
  reg [31:0] _RAND_254;
  reg [63:0] _RAND_255;
  reg [31:0] _RAND_256;
  reg [31:0] _RAND_257;
  reg [31:0] _RAND_258;
  reg [31:0] _RAND_259;
  reg [31:0] _RAND_260;
  reg [63:0] _RAND_261;
  reg [31:0] _RAND_262;
  reg [31:0] _RAND_263;
  reg [31:0] _RAND_264;
  reg [31:0] _RAND_265;
  reg [31:0] _RAND_266;
  reg [31:0] _RAND_267;
  reg [63:0] _RAND_268;
  reg [63:0] _RAND_269;
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
  reg [63:0] _RAND_309;
  reg [31:0] _RAND_310;
  reg [31:0] _RAND_311;
  reg [63:0] _RAND_312;
  reg [63:0] _RAND_313;
  reg [31:0] _RAND_314;
  reg [63:0] _RAND_315;
  reg [31:0] _RAND_316;
  reg [31:0] _RAND_317;
  reg [31:0] _RAND_318;
  reg [31:0] _RAND_319;
  reg [31:0] _RAND_320;
  reg [31:0] _RAND_321;
  reg [31:0] _RAND_322;
  reg [31:0] _RAND_323;
  reg [31:0] _RAND_324;
  reg [63:0] _RAND_325;
  reg [31:0] _RAND_326;
  reg [31:0] _RAND_327;
  reg [63:0] _RAND_328;
  reg [63:0] _RAND_329;
  reg [31:0] _RAND_330;
  reg [63:0] _RAND_331;
  reg [63:0] _RAND_332;
`endif // RANDOMIZE_REG_INIT
  reg [119:0] meta_0 [0:15]; // @[fetch-target-queue.scala 142:29]
  wire  meta_0_bpd_meta_en; // @[fetch-target-queue.scala 142:29]
  wire [3:0] meta_0_bpd_meta_addr; // @[fetch-target-queue.scala 142:29]
  wire [119:0] meta_0_bpd_meta_data; // @[fetch-target-queue.scala 142:29]
  wire [119:0] meta_0__T_56_data; // @[fetch-target-queue.scala 142:29]
  wire [3:0] meta_0__T_56_addr; // @[fetch-target-queue.scala 142:29]
  wire  meta_0__T_56_mask; // @[fetch-target-queue.scala 142:29]
  wire  meta_0__T_56_en; // @[fetch-target-queue.scala 142:29]
  reg  meta_0_bpd_meta_en_pipe_0;
  reg [3:0] meta_0_bpd_meta_addr_pipe_0;
  reg [63:0] ghist_0_old_history [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_old_history_bpd_ghist_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_old_history_bpd_ghist_addr; // @[fetch-target-queue.scala 144:43]
  wire [63:0] ghist_0_old_history_bpd_ghist_data; // @[fetch-target-queue.scala 144:43]
  wire [63:0] ghist_0_old_history__T_54_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_old_history__T_54_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_old_history__T_54_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_old_history__T_54_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_0_old_history_bpd_ghist_en_pipe_0;
  reg [3:0] ghist_0_old_history_bpd_ghist_addr_pipe_0;
  reg  ghist_0_current_saw_branch_not_taken [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_current_saw_branch_not_taken_bpd_ghist_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_current_saw_branch_not_taken_bpd_ghist_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_current_saw_branch_not_taken_bpd_ghist_data; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_current_saw_branch_not_taken__T_54_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_current_saw_branch_not_taken__T_54_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_current_saw_branch_not_taken__T_54_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_current_saw_branch_not_taken__T_54_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_0_current_saw_branch_not_taken_bpd_ghist_en_pipe_0;
  reg [3:0] ghist_0_current_saw_branch_not_taken_bpd_ghist_addr_pipe_0;
  reg  ghist_0_new_saw_branch_not_taken [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_not_taken_bpd_ghist_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_new_saw_branch_not_taken_bpd_ghist_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_not_taken_bpd_ghist_data; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_not_taken__T_54_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_new_saw_branch_not_taken__T_54_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_not_taken__T_54_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_not_taken__T_54_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_0_new_saw_branch_not_taken_bpd_ghist_en_pipe_0;
  reg [3:0] ghist_0_new_saw_branch_not_taken_bpd_ghist_addr_pipe_0;
  reg  ghist_0_new_saw_branch_taken [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_taken_bpd_ghist_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_new_saw_branch_taken_bpd_ghist_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_taken_bpd_ghist_data; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_taken__T_54_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_new_saw_branch_taken__T_54_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_taken__T_54_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_new_saw_branch_taken__T_54_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_0_new_saw_branch_taken_bpd_ghist_en_pipe_0;
  reg [3:0] ghist_0_new_saw_branch_taken_bpd_ghist_addr_pipe_0;
  reg [4:0] ghist_0_ras_idx [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_ras_idx_bpd_ghist_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_ras_idx_bpd_ghist_addr; // @[fetch-target-queue.scala 144:43]
  wire [4:0] ghist_0_ras_idx_bpd_ghist_data; // @[fetch-target-queue.scala 144:43]
  wire [4:0] ghist_0_ras_idx__T_54_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_0_ras_idx__T_54_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_ras_idx__T_54_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_0_ras_idx__T_54_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_0_ras_idx_bpd_ghist_en_pipe_0;
  reg [3:0] ghist_0_ras_idx_bpd_ghist_addr_pipe_0;
  reg [63:0] ghist_1_old_history [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_old_history__T_190_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_old_history__T_190_addr; // @[fetch-target-queue.scala 144:43]
  wire [63:0] ghist_1_old_history__T_190_data; // @[fetch-target-queue.scala 144:43]
  wire [63:0] ghist_1_old_history__T_55_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_old_history__T_55_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_old_history__T_55_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_old_history__T_55_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_1_old_history__T_190_en_pipe_0;
  reg [3:0] ghist_1_old_history__T_190_addr_pipe_0;
  reg  ghist_1_current_saw_branch_not_taken [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_current_saw_branch_not_taken__T_190_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_current_saw_branch_not_taken__T_190_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_current_saw_branch_not_taken__T_190_data; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_current_saw_branch_not_taken__T_55_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_current_saw_branch_not_taken__T_55_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_current_saw_branch_not_taken__T_55_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_current_saw_branch_not_taken__T_55_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_1_current_saw_branch_not_taken__T_190_en_pipe_0;
  reg [3:0] ghist_1_current_saw_branch_not_taken__T_190_addr_pipe_0;
  reg  ghist_1_new_saw_branch_not_taken [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_not_taken__T_190_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_new_saw_branch_not_taken__T_190_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_not_taken__T_190_data; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_not_taken__T_55_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_new_saw_branch_not_taken__T_55_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_not_taken__T_55_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_not_taken__T_55_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_1_new_saw_branch_not_taken__T_190_en_pipe_0;
  reg [3:0] ghist_1_new_saw_branch_not_taken__T_190_addr_pipe_0;
  reg  ghist_1_new_saw_branch_taken [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_taken__T_190_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_new_saw_branch_taken__T_190_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_taken__T_190_data; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_taken__T_55_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_new_saw_branch_taken__T_55_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_taken__T_55_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_new_saw_branch_taken__T_55_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_1_new_saw_branch_taken__T_190_en_pipe_0;
  reg [3:0] ghist_1_new_saw_branch_taken__T_190_addr_pipe_0;
  reg [4:0] ghist_1_ras_idx [0:15]; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_ras_idx__T_190_en; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_ras_idx__T_190_addr; // @[fetch-target-queue.scala 144:43]
  wire [4:0] ghist_1_ras_idx__T_190_data; // @[fetch-target-queue.scala 144:43]
  wire [4:0] ghist_1_ras_idx__T_55_data; // @[fetch-target-queue.scala 144:43]
  wire [3:0] ghist_1_ras_idx__T_55_addr; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_ras_idx__T_55_mask; // @[fetch-target-queue.scala 144:43]
  wire  ghist_1_ras_idx__T_55_en; // @[fetch-target-queue.scala 144:43]
  reg  ghist_1_ras_idx__T_190_en_pipe_0;
  reg [3:0] ghist_1_ras_idx__T_190_addr_pipe_0;
  reg [3:0] bpd_ptr; // @[fetch-target-queue.scala 133:27]
  reg [3:0] deq_ptr; // @[fetch-target-queue.scala 134:27]
  reg [3:0] enq_ptr; // @[fetch-target-queue.scala 135:27]
  wire [3:0] _T_1 = enq_ptr + 4'h1; // @[util.scala 203:14]
  wire [3:0] _T_4 = _T_1 + 4'h1; // @[util.scala 203:14]
  wire  _T_10 = _T_1 == bpd_ptr; // @[fetch-target-queue.scala 138:46]
  wire  full = _T_4 == bpd_ptr | _T_10; // @[fetch-target-queue.scala 137:81]
  reg [39:0] pcs_0; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_1; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_2; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_3; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_4; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_5; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_6; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_7; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_8; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_9; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_10; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_11; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_12; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_13; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_14; // @[fetch-target-queue.scala 141:21]
  reg [39:0] pcs_15; // @[fetch-target-queue.scala 141:21]
  reg  ram_0_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_0_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_0_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_0_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_0_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_0_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_0_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_0_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_0_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_0_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_0_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_0_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_1_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_1_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_1_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_1_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_1_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_1_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_1_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_1_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_1_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_1_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_1_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_1_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_2_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_2_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_2_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_2_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_2_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_2_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_2_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_2_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_2_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_2_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_2_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_2_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_3_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_3_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_3_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_3_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_3_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_3_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_3_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_3_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_3_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_3_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_3_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_3_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_4_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_4_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_4_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_4_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_4_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_4_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_4_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_4_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_4_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_4_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_4_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_4_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_5_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_5_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_5_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_5_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_5_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_5_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_5_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_5_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_5_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_5_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_5_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_5_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_6_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_6_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_6_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_6_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_6_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_6_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_6_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_6_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_6_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_6_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_6_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_6_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_7_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_7_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_7_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_7_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_7_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_7_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_7_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_7_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_7_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_7_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_7_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_7_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_8_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_8_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_8_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_8_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_8_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_8_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_8_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_8_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_8_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_8_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_8_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_8_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_9_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_9_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_9_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_9_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_9_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_9_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_9_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_9_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_9_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_9_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_9_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_9_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_10_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_10_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_10_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_10_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_10_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_10_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_10_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_10_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_10_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_10_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_10_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_10_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_11_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_11_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_11_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_11_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_11_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_11_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_11_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_11_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_11_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_11_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_11_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_11_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_12_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_12_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_12_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_12_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_12_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_12_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_12_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_12_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_12_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_12_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_12_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_12_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_13_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_13_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_13_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_13_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_13_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_13_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_13_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_13_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_13_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_13_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_13_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_13_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_14_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_14_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_14_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_14_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_14_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_14_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_14_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_14_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_14_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_14_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_14_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_14_start_bank; // @[fetch-target-queue.scala 143:21]
  reg  ram_15_cfi_idx_valid; // @[fetch-target-queue.scala 143:21]
  reg [1:0] ram_15_cfi_idx_bits; // @[fetch-target-queue.scala 143:21]
  reg  ram_15_cfi_taken; // @[fetch-target-queue.scala 143:21]
  reg  ram_15_cfi_mispredicted; // @[fetch-target-queue.scala 143:21]
  reg [2:0] ram_15_cfi_type; // @[fetch-target-queue.scala 143:21]
  reg [3:0] ram_15_br_mask; // @[fetch-target-queue.scala 143:21]
  reg  ram_15_cfi_is_call; // @[fetch-target-queue.scala 143:21]
  reg  ram_15_cfi_is_ret; // @[fetch-target-queue.scala 143:21]
  reg  ram_15_cfi_npc_plus4; // @[fetch-target-queue.scala 143:21]
  reg [39:0] ram_15_ras_top; // @[fetch-target-queue.scala 143:21]
  reg [4:0] ram_15_ras_idx; // @[fetch-target-queue.scala 143:21]
  reg  ram_15_start_bank; // @[fetch-target-queue.scala 143:21]
  wire  do_enq = io_enq_ready & io_enq_valid; // @[Decoupled.scala 40:37]
  reg [63:0] prev_ghist_old_history; // @[fetch-target-queue.scala 155:27]
  reg  prev_ghist_current_saw_branch_not_taken; // @[fetch-target-queue.scala 155:27]
  reg [4:0] prev_ghist_ras_idx; // @[fetch-target-queue.scala 155:27]
  reg  prev_entry_cfi_idx_valid; // @[fetch-target-queue.scala 156:27]
  reg [1:0] prev_entry_cfi_idx_bits; // @[fetch-target-queue.scala 156:27]
  reg  prev_entry_cfi_taken; // @[fetch-target-queue.scala 156:27]
  reg [3:0] prev_entry_br_mask; // @[fetch-target-queue.scala 156:27]
  reg  prev_entry_cfi_is_call; // @[fetch-target-queue.scala 156:27]
  reg  prev_entry_cfi_is_ret; // @[fetch-target-queue.scala 156:27]
  wire [3:0] _T_14 = io_enq_bits_br_mask & io_enq_bits_mask; // @[fetch-target-queue.scala 175:52]
  wire [3:0] _T_15 = prev_entry_br_mask >> prev_entry_cfi_idx_bits; // @[fetch-target-queue.scala 183:27]
  wire [3:0] _T_18 = 4'h1 << prev_entry_cfi_idx_bits; // @[OneHot.scala 58:35]
  wire [3:0] _T_21 = {{1'd0}, _T_18[3:1]}; // @[util.scala 373:29]
  wire [3:0] _T_22 = {{2'd0}, _T_18[3:2]}; // @[util.scala 373:29]
  wire [3:0] _T_23 = {{3'd0}, _T_18[3]}; // @[util.scala 373:29]
  wire [3:0] _T_24 = _T_18 | _T_21; // @[util.scala 373:45]
  wire [3:0] _T_25 = _T_24 | _T_22; // @[util.scala 373:45]
  wire [3:0] _T_26 = _T_25 | _T_23; // @[util.scala 373:45]
  wire  _T_27 = _T_15[0] & prev_entry_cfi_taken; // @[frontend.scala 92:84]
  wire [3:0] _T_28 = _T_15[0] & prev_entry_cfi_taken ? _T_18 : 4'h0; // @[frontend.scala 92:73]
  wire [3:0] _T_29 = ~_T_28; // @[frontend.scala 92:69]
  wire [3:0] _T_30 = _T_26 & _T_29; // @[frontend.scala 92:67]
  wire [3:0] _T_32 = prev_entry_cfi_idx_valid ? _T_30 : 4'hf; // @[frontend.scala 91:44]
  wire [3:0] _T_33 = prev_entry_br_mask & _T_32; // @[frontend.scala 91:39]
  wire  _T_35 = _T_33 != 4'h0 | prev_ghist_current_saw_branch_not_taken; // @[frontend.scala 99:61]
  wire [64:0] _T_38 = {prev_ghist_old_history, 1'h0}; // @[frontend.scala 100:91]
  wire [64:0] _T_39 = _T_38 | 65'h1; // @[frontend.scala 100:96]
  wire [64:0] _T_41 = _T_35 ? _T_38 : {{1'd0}, prev_ghist_old_history}; // @[frontend.scala 101:37]
  wire [64:0] _T_42 = _T_27 & prev_entry_cfi_idx_valid ? _T_39 : _T_41; // @[frontend.scala 100:37]
  wire [4:0] _T_45 = prev_ghist_ras_idx + 5'h1; // @[util.scala 203:14]
  wire [4:0] _T_49 = prev_ghist_ras_idx - 5'h1; // @[util.scala 220:14]
  wire [4:0] _T_51 = prev_entry_cfi_idx_valid & prev_entry_cfi_is_ret ? _T_49 : prev_ghist_ras_idx; // @[frontend.scala 126:31]
  wire [4:0] _T_52 = prev_entry_cfi_idx_valid & prev_entry_cfi_is_call ? _T_45 : _T_51; // @[frontend.scala 125:31]
  wire [63:0] _T_53_old_history = io_enq_bits_ghist_current_saw_branch_not_taken ? io_enq_bits_ghist_old_history : _T_42
    [63:0]; // @[fetch-target-queue.scala 178:24]
  wire [4:0] _T_53_ras_idx = io_enq_bits_ghist_current_saw_branch_not_taken ? io_enq_bits_ghist_ras_idx : _T_52; // @[fetch-target-queue.scala 178:24]
  wire  _GEN_16 = 4'h0 == enq_ptr ? 1'h0 : ram_0_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_17 = 4'h1 == enq_ptr ? 1'h0 : ram_1_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_18 = 4'h2 == enq_ptr ? 1'h0 : ram_2_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_19 = 4'h3 == enq_ptr ? 1'h0 : ram_3_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_20 = 4'h4 == enq_ptr ? 1'h0 : ram_4_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_21 = 4'h5 == enq_ptr ? 1'h0 : ram_5_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_22 = 4'h6 == enq_ptr ? 1'h0 : ram_6_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_23 = 4'h7 == enq_ptr ? 1'h0 : ram_7_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_24 = 4'h8 == enq_ptr ? 1'h0 : ram_8_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_25 = 4'h9 == enq_ptr ? 1'h0 : ram_9_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_26 = 4'ha == enq_ptr ? 1'h0 : ram_10_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_27 = 4'hb == enq_ptr ? 1'h0 : ram_11_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_28 = 4'hc == enq_ptr ? 1'h0 : ram_12_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_29 = 4'hd == enq_ptr ? 1'h0 : ram_13_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_30 = 4'he == enq_ptr ? 1'h0 : ram_14_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_31 = 4'hf == enq_ptr ? 1'h0 : ram_15_start_bank; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_32 = 4'h0 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_0_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_33 = 4'h1 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_1_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_34 = 4'h2 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_2_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_35 = 4'h3 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_3_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_36 = 4'h4 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_4_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_37 = 4'h5 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_5_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_38 = 4'h6 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_6_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_39 = 4'h7 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_7_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_40 = 4'h8 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_8_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_41 = 4'h9 == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_9_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_42 = 4'ha == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_10_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_43 = 4'hb == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_11_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_44 = 4'hc == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_12_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_45 = 4'hd == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_13_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_46 = 4'he == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_14_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [4:0] _GEN_47 = 4'hf == enq_ptr ? io_enq_bits_ghist_ras_idx : ram_15_ras_idx; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_48 = 4'h0 == enq_ptr ? io_enq_bits_ras_top : ram_0_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_49 = 4'h1 == enq_ptr ? io_enq_bits_ras_top : ram_1_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_50 = 4'h2 == enq_ptr ? io_enq_bits_ras_top : ram_2_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_51 = 4'h3 == enq_ptr ? io_enq_bits_ras_top : ram_3_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_52 = 4'h4 == enq_ptr ? io_enq_bits_ras_top : ram_4_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_53 = 4'h5 == enq_ptr ? io_enq_bits_ras_top : ram_5_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_54 = 4'h6 == enq_ptr ? io_enq_bits_ras_top : ram_6_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_55 = 4'h7 == enq_ptr ? io_enq_bits_ras_top : ram_7_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_56 = 4'h8 == enq_ptr ? io_enq_bits_ras_top : ram_8_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_57 = 4'h9 == enq_ptr ? io_enq_bits_ras_top : ram_9_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_58 = 4'ha == enq_ptr ? io_enq_bits_ras_top : ram_10_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_59 = 4'hb == enq_ptr ? io_enq_bits_ras_top : ram_11_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_60 = 4'hc == enq_ptr ? io_enq_bits_ras_top : ram_12_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_61 = 4'hd == enq_ptr ? io_enq_bits_ras_top : ram_13_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_62 = 4'he == enq_ptr ? io_enq_bits_ras_top : ram_14_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [39:0] _GEN_63 = 4'hf == enq_ptr ? io_enq_bits_ras_top : ram_15_ras_top; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_64 = 4'h0 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_0_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_65 = 4'h1 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_1_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_66 = 4'h2 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_2_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_67 = 4'h3 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_3_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_68 = 4'h4 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_4_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_69 = 4'h5 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_5_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_70 = 4'h6 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_6_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_71 = 4'h7 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_7_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_72 = 4'h8 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_8_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_73 = 4'h9 == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_9_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_74 = 4'ha == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_10_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_75 = 4'hb == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_11_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_76 = 4'hc == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_12_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_77 = 4'hd == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_13_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_78 = 4'he == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_14_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_79 = 4'hf == enq_ptr ? io_enq_bits_cfi_npc_plus4 : ram_15_cfi_npc_plus4; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_80 = 4'h0 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_0_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_81 = 4'h1 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_1_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_82 = 4'h2 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_2_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_83 = 4'h3 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_3_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_84 = 4'h4 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_4_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_85 = 4'h5 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_5_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_86 = 4'h6 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_6_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_87 = 4'h7 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_7_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_88 = 4'h8 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_8_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_89 = 4'h9 == enq_ptr ? io_enq_bits_cfi_is_ret : ram_9_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_90 = 4'ha == enq_ptr ? io_enq_bits_cfi_is_ret : ram_10_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_91 = 4'hb == enq_ptr ? io_enq_bits_cfi_is_ret : ram_11_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_92 = 4'hc == enq_ptr ? io_enq_bits_cfi_is_ret : ram_12_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_93 = 4'hd == enq_ptr ? io_enq_bits_cfi_is_ret : ram_13_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_94 = 4'he == enq_ptr ? io_enq_bits_cfi_is_ret : ram_14_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_95 = 4'hf == enq_ptr ? io_enq_bits_cfi_is_ret : ram_15_cfi_is_ret; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_96 = 4'h0 == enq_ptr ? io_enq_bits_cfi_is_call : ram_0_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_97 = 4'h1 == enq_ptr ? io_enq_bits_cfi_is_call : ram_1_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_98 = 4'h2 == enq_ptr ? io_enq_bits_cfi_is_call : ram_2_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_99 = 4'h3 == enq_ptr ? io_enq_bits_cfi_is_call : ram_3_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_100 = 4'h4 == enq_ptr ? io_enq_bits_cfi_is_call : ram_4_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_101 = 4'h5 == enq_ptr ? io_enq_bits_cfi_is_call : ram_5_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_102 = 4'h6 == enq_ptr ? io_enq_bits_cfi_is_call : ram_6_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_103 = 4'h7 == enq_ptr ? io_enq_bits_cfi_is_call : ram_7_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_104 = 4'h8 == enq_ptr ? io_enq_bits_cfi_is_call : ram_8_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_105 = 4'h9 == enq_ptr ? io_enq_bits_cfi_is_call : ram_9_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_106 = 4'ha == enq_ptr ? io_enq_bits_cfi_is_call : ram_10_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_107 = 4'hb == enq_ptr ? io_enq_bits_cfi_is_call : ram_11_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_108 = 4'hc == enq_ptr ? io_enq_bits_cfi_is_call : ram_12_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_109 = 4'hd == enq_ptr ? io_enq_bits_cfi_is_call : ram_13_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_110 = 4'he == enq_ptr ? io_enq_bits_cfi_is_call : ram_14_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_111 = 4'hf == enq_ptr ? io_enq_bits_cfi_is_call : ram_15_cfi_is_call; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_112 = 4'h0 == enq_ptr ? _T_14 : ram_0_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_113 = 4'h1 == enq_ptr ? _T_14 : ram_1_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_114 = 4'h2 == enq_ptr ? _T_14 : ram_2_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_115 = 4'h3 == enq_ptr ? _T_14 : ram_3_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_116 = 4'h4 == enq_ptr ? _T_14 : ram_4_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_117 = 4'h5 == enq_ptr ? _T_14 : ram_5_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_118 = 4'h6 == enq_ptr ? _T_14 : ram_6_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_119 = 4'h7 == enq_ptr ? _T_14 : ram_7_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_120 = 4'h8 == enq_ptr ? _T_14 : ram_8_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_121 = 4'h9 == enq_ptr ? _T_14 : ram_9_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_122 = 4'ha == enq_ptr ? _T_14 : ram_10_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_123 = 4'hb == enq_ptr ? _T_14 : ram_11_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_124 = 4'hc == enq_ptr ? _T_14 : ram_12_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_125 = 4'hd == enq_ptr ? _T_14 : ram_13_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_126 = 4'he == enq_ptr ? _T_14 : ram_14_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [3:0] _GEN_127 = 4'hf == enq_ptr ? _T_14 : ram_15_br_mask; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_128 = 4'h0 == enq_ptr ? io_enq_bits_cfi_type : ram_0_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_129 = 4'h1 == enq_ptr ? io_enq_bits_cfi_type : ram_1_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_130 = 4'h2 == enq_ptr ? io_enq_bits_cfi_type : ram_2_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_131 = 4'h3 == enq_ptr ? io_enq_bits_cfi_type : ram_3_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_132 = 4'h4 == enq_ptr ? io_enq_bits_cfi_type : ram_4_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_133 = 4'h5 == enq_ptr ? io_enq_bits_cfi_type : ram_5_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_134 = 4'h6 == enq_ptr ? io_enq_bits_cfi_type : ram_6_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_135 = 4'h7 == enq_ptr ? io_enq_bits_cfi_type : ram_7_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_136 = 4'h8 == enq_ptr ? io_enq_bits_cfi_type : ram_8_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_137 = 4'h9 == enq_ptr ? io_enq_bits_cfi_type : ram_9_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_138 = 4'ha == enq_ptr ? io_enq_bits_cfi_type : ram_10_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_139 = 4'hb == enq_ptr ? io_enq_bits_cfi_type : ram_11_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_140 = 4'hc == enq_ptr ? io_enq_bits_cfi_type : ram_12_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_141 = 4'hd == enq_ptr ? io_enq_bits_cfi_type : ram_13_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_142 = 4'he == enq_ptr ? io_enq_bits_cfi_type : ram_14_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [2:0] _GEN_143 = 4'hf == enq_ptr ? io_enq_bits_cfi_type : ram_15_cfi_type; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_144 = 4'h0 == enq_ptr ? 1'h0 : ram_0_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_145 = 4'h1 == enq_ptr ? 1'h0 : ram_1_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_146 = 4'h2 == enq_ptr ? 1'h0 : ram_2_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_147 = 4'h3 == enq_ptr ? 1'h0 : ram_3_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_148 = 4'h4 == enq_ptr ? 1'h0 : ram_4_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_149 = 4'h5 == enq_ptr ? 1'h0 : ram_5_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_150 = 4'h6 == enq_ptr ? 1'h0 : ram_6_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_151 = 4'h7 == enq_ptr ? 1'h0 : ram_7_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_152 = 4'h8 == enq_ptr ? 1'h0 : ram_8_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_153 = 4'h9 == enq_ptr ? 1'h0 : ram_9_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_154 = 4'ha == enq_ptr ? 1'h0 : ram_10_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_155 = 4'hb == enq_ptr ? 1'h0 : ram_11_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_156 = 4'hc == enq_ptr ? 1'h0 : ram_12_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_157 = 4'hd == enq_ptr ? 1'h0 : ram_13_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_158 = 4'he == enq_ptr ? 1'h0 : ram_14_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_159 = 4'hf == enq_ptr ? 1'h0 : ram_15_cfi_mispredicted; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_160 = 4'h0 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_0_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_161 = 4'h1 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_1_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_162 = 4'h2 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_2_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_163 = 4'h3 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_3_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_164 = 4'h4 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_4_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_165 = 4'h5 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_5_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_166 = 4'h6 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_6_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_167 = 4'h7 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_7_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_168 = 4'h8 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_8_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_169 = 4'h9 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_9_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_170 = 4'ha == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_10_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_171 = 4'hb == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_11_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_172 = 4'hc == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_12_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_173 = 4'hd == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_13_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_174 = 4'he == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_14_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_175 = 4'hf == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_15_cfi_taken; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_176 = 4'h0 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_0_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_177 = 4'h1 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_1_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_178 = 4'h2 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_2_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_179 = 4'h3 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_3_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_180 = 4'h4 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_4_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_181 = 4'h5 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_5_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_182 = 4'h6 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_6_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_183 = 4'h7 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_7_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_184 = 4'h8 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_8_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_185 = 4'h9 == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_9_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_186 = 4'ha == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_10_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_187 = 4'hb == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_11_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_188 = 4'hc == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_12_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_189 = 4'hd == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_13_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_190 = 4'he == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_14_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire [1:0] _GEN_191 = 4'hf == enq_ptr ? io_enq_bits_cfi_idx_bits : ram_15_cfi_idx_bits; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_192 = 4'h0 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_0_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_193 = 4'h1 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_1_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_194 = 4'h2 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_2_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_195 = 4'h3 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_3_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_196 = 4'h4 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_4_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_197 = 4'h5 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_5_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_198 = 4'h6 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_6_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_199 = 4'h7 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_7_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_200 = 4'h8 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_8_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_201 = 4'h9 == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_9_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_202 = 4'ha == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_10_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_203 = 4'hb == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_11_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_204 = 4'hc == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_12_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_205 = 4'hd == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_13_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_206 = 4'he == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_14_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_207 = 4'hf == enq_ptr ? io_enq_bits_cfi_idx_valid : ram_15_cfi_idx_valid; // @[fetch-target-queue.scala 195:{18,18} 143:21]
  wire  _GEN_234 = do_enq ? _GEN_16 : ram_0_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_235 = do_enq ? _GEN_17 : ram_1_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_236 = do_enq ? _GEN_18 : ram_2_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_237 = do_enq ? _GEN_19 : ram_3_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_238 = do_enq ? _GEN_20 : ram_4_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_239 = do_enq ? _GEN_21 : ram_5_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_240 = do_enq ? _GEN_22 : ram_6_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_241 = do_enq ? _GEN_23 : ram_7_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_242 = do_enq ? _GEN_24 : ram_8_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_243 = do_enq ? _GEN_25 : ram_9_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_244 = do_enq ? _GEN_26 : ram_10_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_245 = do_enq ? _GEN_27 : ram_11_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_246 = do_enq ? _GEN_28 : ram_12_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_247 = do_enq ? _GEN_29 : ram_13_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_248 = do_enq ? _GEN_30 : ram_14_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_249 = do_enq ? _GEN_31 : ram_15_start_bank; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_250 = do_enq ? _GEN_32 : ram_0_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_251 = do_enq ? _GEN_33 : ram_1_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_252 = do_enq ? _GEN_34 : ram_2_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_253 = do_enq ? _GEN_35 : ram_3_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_254 = do_enq ? _GEN_36 : ram_4_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_255 = do_enq ? _GEN_37 : ram_5_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_256 = do_enq ? _GEN_38 : ram_6_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_257 = do_enq ? _GEN_39 : ram_7_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_258 = do_enq ? _GEN_40 : ram_8_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_259 = do_enq ? _GEN_41 : ram_9_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_260 = do_enq ? _GEN_42 : ram_10_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_261 = do_enq ? _GEN_43 : ram_11_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_262 = do_enq ? _GEN_44 : ram_12_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_263 = do_enq ? _GEN_45 : ram_13_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_264 = do_enq ? _GEN_46 : ram_14_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [4:0] _GEN_265 = do_enq ? _GEN_47 : ram_15_ras_idx; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_266 = do_enq ? _GEN_48 : ram_0_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_267 = do_enq ? _GEN_49 : ram_1_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_268 = do_enq ? _GEN_50 : ram_2_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_269 = do_enq ? _GEN_51 : ram_3_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_270 = do_enq ? _GEN_52 : ram_4_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_271 = do_enq ? _GEN_53 : ram_5_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_272 = do_enq ? _GEN_54 : ram_6_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_273 = do_enq ? _GEN_55 : ram_7_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_274 = do_enq ? _GEN_56 : ram_8_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_275 = do_enq ? _GEN_57 : ram_9_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_276 = do_enq ? _GEN_58 : ram_10_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_277 = do_enq ? _GEN_59 : ram_11_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_278 = do_enq ? _GEN_60 : ram_12_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_279 = do_enq ? _GEN_61 : ram_13_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_280 = do_enq ? _GEN_62 : ram_14_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire [39:0] _GEN_281 = do_enq ? _GEN_63 : ram_15_ras_top; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_282 = do_enq ? _GEN_64 : ram_0_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_283 = do_enq ? _GEN_65 : ram_1_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_284 = do_enq ? _GEN_66 : ram_2_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_285 = do_enq ? _GEN_67 : ram_3_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_286 = do_enq ? _GEN_68 : ram_4_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_287 = do_enq ? _GEN_69 : ram_5_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_288 = do_enq ? _GEN_70 : ram_6_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_289 = do_enq ? _GEN_71 : ram_7_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_290 = do_enq ? _GEN_72 : ram_8_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_291 = do_enq ? _GEN_73 : ram_9_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_292 = do_enq ? _GEN_74 : ram_10_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_293 = do_enq ? _GEN_75 : ram_11_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_294 = do_enq ? _GEN_76 : ram_12_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_295 = do_enq ? _GEN_77 : ram_13_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_296 = do_enq ? _GEN_78 : ram_14_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_297 = do_enq ? _GEN_79 : ram_15_cfi_npc_plus4; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_298 = do_enq ? _GEN_80 : ram_0_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_299 = do_enq ? _GEN_81 : ram_1_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_300 = do_enq ? _GEN_82 : ram_2_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_301 = do_enq ? _GEN_83 : ram_3_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_302 = do_enq ? _GEN_84 : ram_4_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_303 = do_enq ? _GEN_85 : ram_5_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_304 = do_enq ? _GEN_86 : ram_6_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_305 = do_enq ? _GEN_87 : ram_7_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_306 = do_enq ? _GEN_88 : ram_8_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_307 = do_enq ? _GEN_89 : ram_9_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_308 = do_enq ? _GEN_90 : ram_10_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_309 = do_enq ? _GEN_91 : ram_11_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_310 = do_enq ? _GEN_92 : ram_12_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_311 = do_enq ? _GEN_93 : ram_13_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_312 = do_enq ? _GEN_94 : ram_14_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_313 = do_enq ? _GEN_95 : ram_15_cfi_is_ret; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_314 = do_enq ? _GEN_96 : ram_0_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_315 = do_enq ? _GEN_97 : ram_1_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_316 = do_enq ? _GEN_98 : ram_2_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_317 = do_enq ? _GEN_99 : ram_3_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_318 = do_enq ? _GEN_100 : ram_4_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_319 = do_enq ? _GEN_101 : ram_5_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_320 = do_enq ? _GEN_102 : ram_6_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_321 = do_enq ? _GEN_103 : ram_7_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_322 = do_enq ? _GEN_104 : ram_8_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_323 = do_enq ? _GEN_105 : ram_9_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_324 = do_enq ? _GEN_106 : ram_10_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_325 = do_enq ? _GEN_107 : ram_11_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_326 = do_enq ? _GEN_108 : ram_12_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_327 = do_enq ? _GEN_109 : ram_13_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_328 = do_enq ? _GEN_110 : ram_14_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_329 = do_enq ? _GEN_111 : ram_15_cfi_is_call; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_330 = do_enq ? _GEN_112 : ram_0_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_331 = do_enq ? _GEN_113 : ram_1_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_332 = do_enq ? _GEN_114 : ram_2_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_333 = do_enq ? _GEN_115 : ram_3_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_334 = do_enq ? _GEN_116 : ram_4_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_335 = do_enq ? _GEN_117 : ram_5_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_336 = do_enq ? _GEN_118 : ram_6_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_337 = do_enq ? _GEN_119 : ram_7_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_338 = do_enq ? _GEN_120 : ram_8_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_339 = do_enq ? _GEN_121 : ram_9_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_340 = do_enq ? _GEN_122 : ram_10_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_341 = do_enq ? _GEN_123 : ram_11_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_342 = do_enq ? _GEN_124 : ram_12_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_343 = do_enq ? _GEN_125 : ram_13_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_344 = do_enq ? _GEN_126 : ram_14_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [3:0] _GEN_345 = do_enq ? _GEN_127 : ram_15_br_mask; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_346 = do_enq ? _GEN_128 : ram_0_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_347 = do_enq ? _GEN_129 : ram_1_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_348 = do_enq ? _GEN_130 : ram_2_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_349 = do_enq ? _GEN_131 : ram_3_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_350 = do_enq ? _GEN_132 : ram_4_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_351 = do_enq ? _GEN_133 : ram_5_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_352 = do_enq ? _GEN_134 : ram_6_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_353 = do_enq ? _GEN_135 : ram_7_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_354 = do_enq ? _GEN_136 : ram_8_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_355 = do_enq ? _GEN_137 : ram_9_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_356 = do_enq ? _GEN_138 : ram_10_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_357 = do_enq ? _GEN_139 : ram_11_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_358 = do_enq ? _GEN_140 : ram_12_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_359 = do_enq ? _GEN_141 : ram_13_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_360 = do_enq ? _GEN_142 : ram_14_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire [2:0] _GEN_361 = do_enq ? _GEN_143 : ram_15_cfi_type; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_362 = do_enq ? _GEN_144 : ram_0_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_363 = do_enq ? _GEN_145 : ram_1_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_364 = do_enq ? _GEN_146 : ram_2_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_365 = do_enq ? _GEN_147 : ram_3_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_366 = do_enq ? _GEN_148 : ram_4_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_367 = do_enq ? _GEN_149 : ram_5_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_368 = do_enq ? _GEN_150 : ram_6_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_369 = do_enq ? _GEN_151 : ram_7_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_370 = do_enq ? _GEN_152 : ram_8_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_371 = do_enq ? _GEN_153 : ram_9_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_372 = do_enq ? _GEN_154 : ram_10_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_373 = do_enq ? _GEN_155 : ram_11_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_374 = do_enq ? _GEN_156 : ram_12_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_375 = do_enq ? _GEN_157 : ram_13_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_376 = do_enq ? _GEN_158 : ram_14_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_377 = do_enq ? _GEN_159 : ram_15_cfi_mispredicted; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_378 = do_enq ? _GEN_160 : ram_0_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_379 = do_enq ? _GEN_161 : ram_1_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_380 = do_enq ? _GEN_162 : ram_2_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_381 = do_enq ? _GEN_163 : ram_3_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_382 = do_enq ? _GEN_164 : ram_4_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_383 = do_enq ? _GEN_165 : ram_5_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_384 = do_enq ? _GEN_166 : ram_6_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_385 = do_enq ? _GEN_167 : ram_7_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_386 = do_enq ? _GEN_168 : ram_8_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_387 = do_enq ? _GEN_169 : ram_9_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_388 = do_enq ? _GEN_170 : ram_10_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_389 = do_enq ? _GEN_171 : ram_11_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_390 = do_enq ? _GEN_172 : ram_12_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_391 = do_enq ? _GEN_173 : ram_13_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_392 = do_enq ? _GEN_174 : ram_14_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_393 = do_enq ? _GEN_175 : ram_15_cfi_taken; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_394 = do_enq ? _GEN_176 : ram_0_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_395 = do_enq ? _GEN_177 : ram_1_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_396 = do_enq ? _GEN_178 : ram_2_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_397 = do_enq ? _GEN_179 : ram_3_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_398 = do_enq ? _GEN_180 : ram_4_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_399 = do_enq ? _GEN_181 : ram_5_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_400 = do_enq ? _GEN_182 : ram_6_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_401 = do_enq ? _GEN_183 : ram_7_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_402 = do_enq ? _GEN_184 : ram_8_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_403 = do_enq ? _GEN_185 : ram_9_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_404 = do_enq ? _GEN_186 : ram_10_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_405 = do_enq ? _GEN_187 : ram_11_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_406 = do_enq ? _GEN_188 : ram_12_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_407 = do_enq ? _GEN_189 : ram_13_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_408 = do_enq ? _GEN_190 : ram_14_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire [1:0] _GEN_409 = do_enq ? _GEN_191 : ram_15_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_410 = do_enq ? _GEN_192 : ram_0_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_411 = do_enq ? _GEN_193 : ram_1_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_412 = do_enq ? _GEN_194 : ram_2_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_413 = do_enq ? _GEN_195 : ram_3_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_414 = do_enq ? _GEN_196 : ram_4_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_415 = do_enq ? _GEN_197 : ram_5_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_416 = do_enq ? _GEN_198 : ram_6_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_417 = do_enq ? _GEN_199 : ram_7_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_418 = do_enq ? _GEN_200 : ram_8_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_419 = do_enq ? _GEN_201 : ram_9_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_420 = do_enq ? _GEN_202 : ram_10_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_421 = do_enq ? _GEN_203 : ram_11_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_422 = do_enq ? _GEN_204 : ram_12_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_423 = do_enq ? _GEN_205 : ram_13_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_424 = do_enq ? _GEN_206 : ram_14_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_425 = do_enq ? _GEN_207 : ram_15_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 143:21]
  wire  _GEN_431 = do_enq ? io_enq_bits_cfi_is_ret : prev_entry_cfi_is_ret; // @[fetch-target-queue.scala 158:17 198:16 156:27]
  wire  _GEN_432 = do_enq ? io_enq_bits_cfi_is_call : prev_entry_cfi_is_call; // @[fetch-target-queue.scala 158:17 198:16 156:27]
  wire [3:0] _GEN_433 = do_enq ? _T_14 : prev_entry_br_mask; // @[fetch-target-queue.scala 158:17 198:16 156:27]
  wire  _GEN_436 = do_enq ? io_enq_bits_cfi_idx_valid : prev_entry_cfi_taken; // @[fetch-target-queue.scala 158:17 198:16 156:27]
  wire [1:0] _GEN_437 = do_enq ? io_enq_bits_cfi_idx_bits : prev_entry_cfi_idx_bits; // @[fetch-target-queue.scala 158:17 198:16 156:27]
  wire  _GEN_438 = do_enq ? io_enq_bits_cfi_idx_valid : prev_entry_cfi_idx_valid; // @[fetch-target-queue.scala 158:17 198:16 156:27]
  wire [4:0] _GEN_439 = do_enq ? _T_53_ras_idx : prev_ghist_ras_idx; // @[fetch-target-queue.scala 158:17 199:16 155:27]
  wire  _GEN_442 = do_enq ? io_enq_bits_ghist_current_saw_branch_not_taken : prev_ghist_current_saw_branch_not_taken; // @[fetch-target-queue.scala 158:17 199:16 155:27]
  wire [63:0] _GEN_443 = do_enq ? _T_53_old_history : prev_ghist_old_history; // @[fetch-target-queue.scala 158:17 199:16 155:27]
  wire [3:0] _GEN_445 = io_deq_valid ? io_deq_bits : deq_ptr; // @[fetch-target-queue.scala 209:23 210:13 134:27]
  reg  first_empty; // @[fetch-target-queue.scala 214:28]
  reg  _T_60; // @[fetch-target-queue.scala 222:31]
  reg [39:0] _T_61; // @[fetch-target-queue.scala 223:31]
  reg [4:0] _T_62; // @[fetch-target-queue.scala 224:31]
  reg  bpd_update_mispredict; // @[fetch-target-queue.scala 226:38]
  reg  bpd_update_repair; // @[fetch-target-queue.scala 227:34]
  reg [3:0] bpd_repair_idx; // @[fetch-target-queue.scala 228:27]
  reg [3:0] bpd_end_idx; // @[fetch-target-queue.scala 229:24]
  reg [39:0] bpd_repair_pc; // @[fetch-target-queue.scala 230:26]
  wire [3:0] _T_64 = bpd_update_repair | bpd_update_mispredict ? bpd_repair_idx : bpd_ptr; // @[fetch-target-queue.scala 233:8]
  wire [3:0] bpd_idx = io_redirect_valid ? io_redirect_bits : _T_64; // @[fetch-target-queue.scala 232:20]
  reg  bpd_entry_cfi_idx_valid; // @[fetch-target-queue.scala 234:26]
  reg [1:0] bpd_entry_cfi_idx_bits; // @[fetch-target-queue.scala 234:26]
  reg  bpd_entry_cfi_taken; // @[fetch-target-queue.scala 234:26]
  reg  bpd_entry_cfi_mispredicted; // @[fetch-target-queue.scala 234:26]
  reg [2:0] bpd_entry_cfi_type; // @[fetch-target-queue.scala 234:26]
  reg [3:0] bpd_entry_br_mask; // @[fetch-target-queue.scala 234:26]
  wire [3:0] _GEN_543 = 4'h1 == bpd_idx ? ram_1_br_mask : ram_0_br_mask; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_544 = 4'h2 == bpd_idx ? ram_2_br_mask : _GEN_543; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_545 = 4'h3 == bpd_idx ? ram_3_br_mask : _GEN_544; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_546 = 4'h4 == bpd_idx ? ram_4_br_mask : _GEN_545; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_547 = 4'h5 == bpd_idx ? ram_5_br_mask : _GEN_546; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_548 = 4'h6 == bpd_idx ? ram_6_br_mask : _GEN_547; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_549 = 4'h7 == bpd_idx ? ram_7_br_mask : _GEN_548; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_550 = 4'h8 == bpd_idx ? ram_8_br_mask : _GEN_549; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_551 = 4'h9 == bpd_idx ? ram_9_br_mask : _GEN_550; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_552 = 4'ha == bpd_idx ? ram_10_br_mask : _GEN_551; // @[fetch-target-queue.scala 234:{26,26}]
  wire [3:0] _GEN_553 = 4'hb == bpd_idx ? ram_11_br_mask : _GEN_552; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_559 = 4'h1 == bpd_idx ? ram_1_cfi_type : ram_0_cfi_type; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_560 = 4'h2 == bpd_idx ? ram_2_cfi_type : _GEN_559; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_561 = 4'h3 == bpd_idx ? ram_3_cfi_type : _GEN_560; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_562 = 4'h4 == bpd_idx ? ram_4_cfi_type : _GEN_561; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_563 = 4'h5 == bpd_idx ? ram_5_cfi_type : _GEN_562; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_564 = 4'h6 == bpd_idx ? ram_6_cfi_type : _GEN_563; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_565 = 4'h7 == bpd_idx ? ram_7_cfi_type : _GEN_564; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_566 = 4'h8 == bpd_idx ? ram_8_cfi_type : _GEN_565; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_567 = 4'h9 == bpd_idx ? ram_9_cfi_type : _GEN_566; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_568 = 4'ha == bpd_idx ? ram_10_cfi_type : _GEN_567; // @[fetch-target-queue.scala 234:{26,26}]
  wire [2:0] _GEN_569 = 4'hb == bpd_idx ? ram_11_cfi_type : _GEN_568; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_575 = 4'h1 == bpd_idx ? ram_1_cfi_mispredicted : ram_0_cfi_mispredicted; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_576 = 4'h2 == bpd_idx ? ram_2_cfi_mispredicted : _GEN_575; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_577 = 4'h3 == bpd_idx ? ram_3_cfi_mispredicted : _GEN_576; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_578 = 4'h4 == bpd_idx ? ram_4_cfi_mispredicted : _GEN_577; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_579 = 4'h5 == bpd_idx ? ram_5_cfi_mispredicted : _GEN_578; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_580 = 4'h6 == bpd_idx ? ram_6_cfi_mispredicted : _GEN_579; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_581 = 4'h7 == bpd_idx ? ram_7_cfi_mispredicted : _GEN_580; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_582 = 4'h8 == bpd_idx ? ram_8_cfi_mispredicted : _GEN_581; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_583 = 4'h9 == bpd_idx ? ram_9_cfi_mispredicted : _GEN_582; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_584 = 4'ha == bpd_idx ? ram_10_cfi_mispredicted : _GEN_583; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_585 = 4'hb == bpd_idx ? ram_11_cfi_mispredicted : _GEN_584; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_591 = 4'h1 == bpd_idx ? ram_1_cfi_taken : ram_0_cfi_taken; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_592 = 4'h2 == bpd_idx ? ram_2_cfi_taken : _GEN_591; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_593 = 4'h3 == bpd_idx ? ram_3_cfi_taken : _GEN_592; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_594 = 4'h4 == bpd_idx ? ram_4_cfi_taken : _GEN_593; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_595 = 4'h5 == bpd_idx ? ram_5_cfi_taken : _GEN_594; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_596 = 4'h6 == bpd_idx ? ram_6_cfi_taken : _GEN_595; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_597 = 4'h7 == bpd_idx ? ram_7_cfi_taken : _GEN_596; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_598 = 4'h8 == bpd_idx ? ram_8_cfi_taken : _GEN_597; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_599 = 4'h9 == bpd_idx ? ram_9_cfi_taken : _GEN_598; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_600 = 4'ha == bpd_idx ? ram_10_cfi_taken : _GEN_599; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_601 = 4'hb == bpd_idx ? ram_11_cfi_taken : _GEN_600; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_607 = 4'h1 == bpd_idx ? ram_1_cfi_idx_bits : ram_0_cfi_idx_bits; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_608 = 4'h2 == bpd_idx ? ram_2_cfi_idx_bits : _GEN_607; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_609 = 4'h3 == bpd_idx ? ram_3_cfi_idx_bits : _GEN_608; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_610 = 4'h4 == bpd_idx ? ram_4_cfi_idx_bits : _GEN_609; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_611 = 4'h5 == bpd_idx ? ram_5_cfi_idx_bits : _GEN_610; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_612 = 4'h6 == bpd_idx ? ram_6_cfi_idx_bits : _GEN_611; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_613 = 4'h7 == bpd_idx ? ram_7_cfi_idx_bits : _GEN_612; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_614 = 4'h8 == bpd_idx ? ram_8_cfi_idx_bits : _GEN_613; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_615 = 4'h9 == bpd_idx ? ram_9_cfi_idx_bits : _GEN_614; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_616 = 4'ha == bpd_idx ? ram_10_cfi_idx_bits : _GEN_615; // @[fetch-target-queue.scala 234:{26,26}]
  wire [1:0] _GEN_617 = 4'hb == bpd_idx ? ram_11_cfi_idx_bits : _GEN_616; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_623 = 4'h1 == bpd_idx ? ram_1_cfi_idx_valid : ram_0_cfi_idx_valid; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_624 = 4'h2 == bpd_idx ? ram_2_cfi_idx_valid : _GEN_623; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_625 = 4'h3 == bpd_idx ? ram_3_cfi_idx_valid : _GEN_624; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_626 = 4'h4 == bpd_idx ? ram_4_cfi_idx_valid : _GEN_625; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_627 = 4'h5 == bpd_idx ? ram_5_cfi_idx_valid : _GEN_626; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_628 = 4'h6 == bpd_idx ? ram_6_cfi_idx_valid : _GEN_627; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_629 = 4'h7 == bpd_idx ? ram_7_cfi_idx_valid : _GEN_628; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_630 = 4'h8 == bpd_idx ? ram_8_cfi_idx_valid : _GEN_629; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_631 = 4'h9 == bpd_idx ? ram_9_cfi_idx_valid : _GEN_630; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_632 = 4'ha == bpd_idx ? ram_10_cfi_idx_valid : _GEN_631; // @[fetch-target-queue.scala 234:{26,26}]
  wire  _GEN_633 = 4'hb == bpd_idx ? ram_11_cfi_idx_valid : _GEN_632; // @[fetch-target-queue.scala 234:{26,26}]
  reg [39:0] bpd_pc; // @[fetch-target-queue.scala 242:26]
  wire [39:0] _GEN_644 = 4'h1 == bpd_idx ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_645 = 4'h2 == bpd_idx ? pcs_2 : _GEN_644; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_646 = 4'h3 == bpd_idx ? pcs_3 : _GEN_645; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_647 = 4'h4 == bpd_idx ? pcs_4 : _GEN_646; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_648 = 4'h5 == bpd_idx ? pcs_5 : _GEN_647; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_649 = 4'h6 == bpd_idx ? pcs_6 : _GEN_648; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_650 = 4'h7 == bpd_idx ? pcs_7 : _GEN_649; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_651 = 4'h8 == bpd_idx ? pcs_8 : _GEN_650; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_652 = 4'h9 == bpd_idx ? pcs_9 : _GEN_651; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_653 = 4'ha == bpd_idx ? pcs_10 : _GEN_652; // @[fetch-target-queue.scala 242:{26,26}]
  wire [39:0] _GEN_654 = 4'hb == bpd_idx ? pcs_11 : _GEN_653; // @[fetch-target-queue.scala 242:{26,26}]
  wire [3:0] _T_72 = bpd_idx + 4'h1; // @[util.scala 203:14]
  reg [39:0] bpd_target; // @[fetch-target-queue.scala 243:27]
  wire [39:0] _GEN_660 = 4'h1 == _T_72 ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_661 = 4'h2 == _T_72 ? pcs_2 : _GEN_660; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_662 = 4'h3 == _T_72 ? pcs_3 : _GEN_661; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_663 = 4'h4 == _T_72 ? pcs_4 : _GEN_662; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_664 = 4'h5 == _T_72 ? pcs_5 : _GEN_663; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_665 = 4'h6 == _T_72 ? pcs_6 : _GEN_664; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_666 = 4'h7 == _T_72 ? pcs_7 : _GEN_665; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_667 = 4'h8 == _T_72 ? pcs_8 : _GEN_666; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_668 = 4'h9 == _T_72 ? pcs_9 : _GEN_667; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_669 = 4'ha == _T_72 ? pcs_10 : _GEN_668; // @[fetch-target-queue.scala 243:{27,27}]
  wire [39:0] _GEN_670 = 4'hb == _T_72 ? pcs_11 : _GEN_669; // @[fetch-target-queue.scala 243:{27,27}]
  reg  _T_74; // @[fetch-target-queue.scala 248:23]
  reg [3:0] _T_75; // @[fetch-target-queue.scala 250:37]
  reg [3:0] _T_76; // @[fetch-target-queue.scala 251:37]
  wire [3:0] _T_78 = bpd_repair_idx + 4'h1; // @[util.scala 203:14]
  reg  _T_80; // @[fetch-target-queue.scala 256:44]
  wire  _T_92 = bpd_pc == bpd_repair_pc; // @[fetch-target-queue.scala 262:14]
  wire  _T_93 = _T_78 == bpd_end_idx | _T_92; // @[fetch-target-queue.scala 261:64]
  wire  _GEN_675 = _T_93 ? 1'h0 : bpd_update_repair; // @[fetch-target-queue.scala 262:34 263:25 227:34]
  wire [3:0] _GEN_676 = bpd_update_repair ? _T_78 : bpd_repair_idx; // @[fetch-target-queue.scala 228:27 259:35 260:27]
  wire  _GEN_677 = bpd_update_repair ? _GEN_675 : bpd_update_repair; // @[fetch-target-queue.scala 227:34 259:35]
  wire  _GEN_680 = bpd_update_repair & _T_80 ? bpd_update_repair : _GEN_677; // @[fetch-target-queue.scala 227:34 256:69]
  wire  _GEN_681 = bpd_update_mispredict ? 1'h0 : bpd_update_mispredict; // @[fetch-target-queue.scala 252:39 253:27 226:38]
  wire  _GEN_682 = bpd_update_mispredict | _GEN_680; // @[fetch-target-queue.scala 252:39 254:27]
  wire  _GEN_685 = _T_74 | _GEN_681; // @[fetch-target-queue.scala 248:52 249:27]
  wire  _T_95 = ~bpd_update_repair; // @[fetch-target-queue.scala 270:31]
  wire  _T_96 = ~bpd_update_mispredict & _T_95; // @[fetch-target-queue.scala 269:54]
  wire  _T_97 = bpd_ptr != deq_ptr; // @[fetch-target-queue.scala 271:40]
  wire  _T_98 = _T_96 & _T_97; // @[fetch-target-queue.scala 270:50]
  wire [3:0] _T_100 = bpd_ptr + 4'h1; // @[util.scala 203:14]
  wire  _T_102 = enq_ptr != _T_100; // @[fetch-target-queue.scala 272:40]
  wire  _T_103 = _T_98 & _T_102; // @[fetch-target-queue.scala 271:52]
  wire  _T_104 = ~io_brupdate_b2_mispredict; // @[fetch-target-queue.scala 273:31]
  wire  _T_105 = _T_103 & _T_104; // @[fetch-target-queue.scala 272:74]
  wire  _T_106 = ~io_redirect_valid; // @[fetch-target-queue.scala 274:31]
  wire  _T_107 = _T_105 & _T_106; // @[fetch-target-queue.scala 273:58]
  reg  _T_108; // @[fetch-target-queue.scala 274:61]
  wire  do_commit_update = _T_107 & ~_T_108; // @[fetch-target-queue.scala 274:50]
  reg  _T_112; // @[fetch-target-queue.scala 278:16]
  wire  _T_113 = bpd_pc != bpd_repair_pc; // @[fetch-target-queue.scala 280:31]
  wire  _T_116 = bpd_entry_cfi_idx_valid | bpd_entry_br_mask != 4'h0; // @[fetch-target-queue.scala 283:53]
  wire  _T_117 = ~first_empty & _T_116; // @[fetch-target-queue.scala 282:41]
  reg  _T_118; // @[fetch-target-queue.scala 284:37]
  wire  _T_121 = ~(_T_118 & ~_T_113); // @[fetch-target-queue.scala 284:28]
  wire  _T_122 = _T_117 & _T_121; // @[fetch-target-queue.scala 283:83]
  reg  _T_123; // @[fetch-target-queue.scala 285:54]
  reg  _T_124; // @[fetch-target-queue.scala 286:54]
  wire [3:0] _T_125 = 4'h1 << bpd_entry_cfi_idx_bits; // @[OneHot.scala 58:35]
  wire [3:0] _T_127 = {{1'd0}, _T_125[3:1]}; // @[util.scala 373:29]
  wire [3:0] _T_128 = {{2'd0}, _T_125[3:2]}; // @[util.scala 373:29]
  wire [3:0] _T_129 = {{3'd0}, _T_125[3]}; // @[util.scala 373:29]
  wire [3:0] _T_130 = _T_125 | _T_127; // @[util.scala 373:45]
  wire [3:0] _T_131 = _T_130 | _T_128; // @[util.scala 373:45]
  wire [3:0] _T_132 = _T_131 | _T_129; // @[util.scala 373:45]
  wire [3:0] _T_133 = _T_132 & bpd_entry_br_mask; // @[fetch-target-queue.scala 290:36]
  wire [3:0] _T_135 = bpd_entry_br_mask >> bpd_entry_cfi_idx_bits; // @[fetch-target-queue.scala 295:54]
  wire  _GEN_715 = _T_112 ? 1'h0 : first_empty; // @[fetch-target-queue.scala 278:80 301:17 214:28]
  reg  _T_145; // @[fetch-target-queue.scala 308:26]
  wire  _GEN_718 = 4'h1 == io_redirect_bits ? ram_1_start_bank : ram_0_start_bank; // @[]
  wire  _GEN_719 = 4'h2 == io_redirect_bits ? ram_2_start_bank : _GEN_718; // @[]
  wire  _GEN_720 = 4'h3 == io_redirect_bits ? ram_3_start_bank : _GEN_719; // @[]
  wire  _GEN_721 = 4'h4 == io_redirect_bits ? ram_4_start_bank : _GEN_720; // @[]
  wire  _GEN_722 = 4'h5 == io_redirect_bits ? ram_5_start_bank : _GEN_721; // @[]
  wire  _GEN_723 = 4'h6 == io_redirect_bits ? ram_6_start_bank : _GEN_722; // @[]
  wire  _GEN_724 = 4'h7 == io_redirect_bits ? ram_7_start_bank : _GEN_723; // @[]
  wire  _GEN_725 = 4'h8 == io_redirect_bits ? ram_8_start_bank : _GEN_724; // @[]
  wire  _GEN_726 = 4'h9 == io_redirect_bits ? ram_9_start_bank : _GEN_725; // @[]
  wire  _GEN_727 = 4'ha == io_redirect_bits ? ram_10_start_bank : _GEN_726; // @[]
  wire  _GEN_728 = 4'hb == io_redirect_bits ? ram_11_start_bank : _GEN_727; // @[]
  wire  _GEN_729 = 4'hc == io_redirect_bits ? ram_12_start_bank : _GEN_728; // @[]
  wire  _GEN_730 = 4'hd == io_redirect_bits ? ram_13_start_bank : _GEN_729; // @[]
  wire  _GEN_731 = 4'he == io_redirect_bits ? ram_14_start_bank : _GEN_730; // @[]
  wire  redirect_new_entry_start_bank = 4'hf == io_redirect_bits ? ram_15_start_bank : _GEN_731; // @[]
  wire [4:0] _GEN_734 = 4'h1 == io_redirect_bits ? ram_1_ras_idx : ram_0_ras_idx; // @[]
  wire [4:0] _GEN_735 = 4'h2 == io_redirect_bits ? ram_2_ras_idx : _GEN_734; // @[]
  wire [4:0] _GEN_736 = 4'h3 == io_redirect_bits ? ram_3_ras_idx : _GEN_735; // @[]
  wire [4:0] _GEN_737 = 4'h4 == io_redirect_bits ? ram_4_ras_idx : _GEN_736; // @[]
  wire [4:0] _GEN_738 = 4'h5 == io_redirect_bits ? ram_5_ras_idx : _GEN_737; // @[]
  wire [4:0] _GEN_739 = 4'h6 == io_redirect_bits ? ram_6_ras_idx : _GEN_738; // @[]
  wire [4:0] _GEN_740 = 4'h7 == io_redirect_bits ? ram_7_ras_idx : _GEN_739; // @[]
  wire [4:0] _GEN_741 = 4'h8 == io_redirect_bits ? ram_8_ras_idx : _GEN_740; // @[]
  wire [4:0] _GEN_742 = 4'h9 == io_redirect_bits ? ram_9_ras_idx : _GEN_741; // @[]
  wire [4:0] _GEN_743 = 4'ha == io_redirect_bits ? ram_10_ras_idx : _GEN_742; // @[]
  wire [4:0] _GEN_744 = 4'hb == io_redirect_bits ? ram_11_ras_idx : _GEN_743; // @[]
  wire [4:0] _GEN_745 = 4'hc == io_redirect_bits ? ram_12_ras_idx : _GEN_744; // @[]
  wire [39:0] _GEN_750 = 4'h1 == io_redirect_bits ? ram_1_ras_top : ram_0_ras_top; // @[]
  wire [39:0] _GEN_751 = 4'h2 == io_redirect_bits ? ram_2_ras_top : _GEN_750; // @[]
  wire [39:0] _GEN_752 = 4'h3 == io_redirect_bits ? ram_3_ras_top : _GEN_751; // @[]
  wire [39:0] _GEN_753 = 4'h4 == io_redirect_bits ? ram_4_ras_top : _GEN_752; // @[]
  wire [39:0] _GEN_754 = 4'h5 == io_redirect_bits ? ram_5_ras_top : _GEN_753; // @[]
  wire [39:0] _GEN_755 = 4'h6 == io_redirect_bits ? ram_6_ras_top : _GEN_754; // @[]
  wire [39:0] _GEN_756 = 4'h7 == io_redirect_bits ? ram_7_ras_top : _GEN_755; // @[]
  wire [39:0] _GEN_757 = 4'h8 == io_redirect_bits ? ram_8_ras_top : _GEN_756; // @[]
  wire [39:0] _GEN_758 = 4'h9 == io_redirect_bits ? ram_9_ras_top : _GEN_757; // @[]
  wire [39:0] _GEN_759 = 4'ha == io_redirect_bits ? ram_10_ras_top : _GEN_758; // @[]
  wire [39:0] _GEN_760 = 4'hb == io_redirect_bits ? ram_11_ras_top : _GEN_759; // @[]
  wire [39:0] _GEN_761 = 4'hc == io_redirect_bits ? ram_12_ras_top : _GEN_760; // @[]
  wire  _GEN_766 = 4'h1 == io_redirect_bits ? ram_1_cfi_npc_plus4 : ram_0_cfi_npc_plus4; // @[]
  wire  _GEN_767 = 4'h2 == io_redirect_bits ? ram_2_cfi_npc_plus4 : _GEN_766; // @[]
  wire  _GEN_768 = 4'h3 == io_redirect_bits ? ram_3_cfi_npc_plus4 : _GEN_767; // @[]
  wire  _GEN_769 = 4'h4 == io_redirect_bits ? ram_4_cfi_npc_plus4 : _GEN_768; // @[]
  wire  _GEN_770 = 4'h5 == io_redirect_bits ? ram_5_cfi_npc_plus4 : _GEN_769; // @[]
  wire  _GEN_771 = 4'h6 == io_redirect_bits ? ram_6_cfi_npc_plus4 : _GEN_770; // @[]
  wire  _GEN_772 = 4'h7 == io_redirect_bits ? ram_7_cfi_npc_plus4 : _GEN_771; // @[]
  wire  _GEN_773 = 4'h8 == io_redirect_bits ? ram_8_cfi_npc_plus4 : _GEN_772; // @[]
  wire  _GEN_774 = 4'h9 == io_redirect_bits ? ram_9_cfi_npc_plus4 : _GEN_773; // @[]
  wire  _GEN_775 = 4'ha == io_redirect_bits ? ram_10_cfi_npc_plus4 : _GEN_774; // @[]
  wire  _GEN_776 = 4'hb == io_redirect_bits ? ram_11_cfi_npc_plus4 : _GEN_775; // @[]
  wire  _GEN_782 = 4'h1 == io_redirect_bits ? ram_1_cfi_is_ret : ram_0_cfi_is_ret; // @[]
  wire  _GEN_783 = 4'h2 == io_redirect_bits ? ram_2_cfi_is_ret : _GEN_782; // @[]
  wire  _GEN_784 = 4'h3 == io_redirect_bits ? ram_3_cfi_is_ret : _GEN_783; // @[]
  wire  _GEN_785 = 4'h4 == io_redirect_bits ? ram_4_cfi_is_ret : _GEN_784; // @[]
  wire  _GEN_786 = 4'h5 == io_redirect_bits ? ram_5_cfi_is_ret : _GEN_785; // @[]
  wire  _GEN_787 = 4'h6 == io_redirect_bits ? ram_6_cfi_is_ret : _GEN_786; // @[]
  wire  _GEN_788 = 4'h7 == io_redirect_bits ? ram_7_cfi_is_ret : _GEN_787; // @[]
  wire  _GEN_789 = 4'h8 == io_redirect_bits ? ram_8_cfi_is_ret : _GEN_788; // @[]
  wire  _GEN_790 = 4'h9 == io_redirect_bits ? ram_9_cfi_is_ret : _GEN_789; // @[]
  wire  _GEN_791 = 4'ha == io_redirect_bits ? ram_10_cfi_is_ret : _GEN_790; // @[]
  wire  _GEN_792 = 4'hb == io_redirect_bits ? ram_11_cfi_is_ret : _GEN_791; // @[]
  wire  _GEN_793 = 4'hc == io_redirect_bits ? ram_12_cfi_is_ret : _GEN_792; // @[]
  wire  _GEN_794 = 4'hd == io_redirect_bits ? ram_13_cfi_is_ret : _GEN_793; // @[]
  wire  _GEN_795 = 4'he == io_redirect_bits ? ram_14_cfi_is_ret : _GEN_794; // @[]
  wire  _GEN_796 = 4'hf == io_redirect_bits ? ram_15_cfi_is_ret : _GEN_795; // @[]
  wire  _GEN_798 = 4'h1 == io_redirect_bits ? ram_1_cfi_is_call : ram_0_cfi_is_call; // @[]
  wire  _GEN_799 = 4'h2 == io_redirect_bits ? ram_2_cfi_is_call : _GEN_798; // @[]
  wire  _GEN_800 = 4'h3 == io_redirect_bits ? ram_3_cfi_is_call : _GEN_799; // @[]
  wire  _GEN_801 = 4'h4 == io_redirect_bits ? ram_4_cfi_is_call : _GEN_800; // @[]
  wire  _GEN_802 = 4'h5 == io_redirect_bits ? ram_5_cfi_is_call : _GEN_801; // @[]
  wire  _GEN_803 = 4'h6 == io_redirect_bits ? ram_6_cfi_is_call : _GEN_802; // @[]
  wire  _GEN_804 = 4'h7 == io_redirect_bits ? ram_7_cfi_is_call : _GEN_803; // @[]
  wire  _GEN_805 = 4'h8 == io_redirect_bits ? ram_8_cfi_is_call : _GEN_804; // @[]
  wire  _GEN_806 = 4'h9 == io_redirect_bits ? ram_9_cfi_is_call : _GEN_805; // @[]
  wire  _GEN_807 = 4'ha == io_redirect_bits ? ram_10_cfi_is_call : _GEN_806; // @[]
  wire  _GEN_808 = 4'hb == io_redirect_bits ? ram_11_cfi_is_call : _GEN_807; // @[]
  wire  _GEN_809 = 4'hc == io_redirect_bits ? ram_12_cfi_is_call : _GEN_808; // @[]
  wire  _GEN_810 = 4'hd == io_redirect_bits ? ram_13_cfi_is_call : _GEN_809; // @[]
  wire  _GEN_811 = 4'he == io_redirect_bits ? ram_14_cfi_is_call : _GEN_810; // @[]
  wire  _GEN_812 = 4'hf == io_redirect_bits ? ram_15_cfi_is_call : _GEN_811; // @[]
  wire [3:0] _GEN_814 = 4'h1 == io_redirect_bits ? ram_1_br_mask : ram_0_br_mask; // @[]
  wire [3:0] _GEN_815 = 4'h2 == io_redirect_bits ? ram_2_br_mask : _GEN_814; // @[]
  wire [3:0] _GEN_816 = 4'h3 == io_redirect_bits ? ram_3_br_mask : _GEN_815; // @[]
  wire [3:0] _GEN_817 = 4'h4 == io_redirect_bits ? ram_4_br_mask : _GEN_816; // @[]
  wire [3:0] _GEN_818 = 4'h5 == io_redirect_bits ? ram_5_br_mask : _GEN_817; // @[]
  wire [3:0] _GEN_819 = 4'h6 == io_redirect_bits ? ram_6_br_mask : _GEN_818; // @[]
  wire [3:0] _GEN_820 = 4'h7 == io_redirect_bits ? ram_7_br_mask : _GEN_819; // @[]
  wire [3:0] _GEN_821 = 4'h8 == io_redirect_bits ? ram_8_br_mask : _GEN_820; // @[]
  wire [3:0] _GEN_822 = 4'h9 == io_redirect_bits ? ram_9_br_mask : _GEN_821; // @[]
  wire [3:0] _GEN_823 = 4'ha == io_redirect_bits ? ram_10_br_mask : _GEN_822; // @[]
  wire [3:0] _GEN_824 = 4'hb == io_redirect_bits ? ram_11_br_mask : _GEN_823; // @[]
  wire [2:0] _GEN_830 = 4'h1 == io_redirect_bits ? ram_1_cfi_type : ram_0_cfi_type; // @[]
  wire [2:0] _GEN_831 = 4'h2 == io_redirect_bits ? ram_2_cfi_type : _GEN_830; // @[]
  wire [2:0] _GEN_832 = 4'h3 == io_redirect_bits ? ram_3_cfi_type : _GEN_831; // @[]
  wire [2:0] _GEN_833 = 4'h4 == io_redirect_bits ? ram_4_cfi_type : _GEN_832; // @[]
  wire [2:0] _GEN_834 = 4'h5 == io_redirect_bits ? ram_5_cfi_type : _GEN_833; // @[]
  wire [2:0] _GEN_835 = 4'h6 == io_redirect_bits ? ram_6_cfi_type : _GEN_834; // @[]
  wire [2:0] _GEN_836 = 4'h7 == io_redirect_bits ? ram_7_cfi_type : _GEN_835; // @[]
  wire [2:0] _GEN_837 = 4'h8 == io_redirect_bits ? ram_8_cfi_type : _GEN_836; // @[]
  wire [2:0] _GEN_838 = 4'h9 == io_redirect_bits ? ram_9_cfi_type : _GEN_837; // @[]
  wire [2:0] _GEN_839 = 4'ha == io_redirect_bits ? ram_10_cfi_type : _GEN_838; // @[]
  wire [2:0] _GEN_840 = 4'hb == io_redirect_bits ? ram_11_cfi_type : _GEN_839; // @[]
  wire  _GEN_846 = 4'h1 == io_redirect_bits ? ram_1_cfi_mispredicted : ram_0_cfi_mispredicted; // @[]
  wire  _GEN_847 = 4'h2 == io_redirect_bits ? ram_2_cfi_mispredicted : _GEN_846; // @[]
  wire  _GEN_848 = 4'h3 == io_redirect_bits ? ram_3_cfi_mispredicted : _GEN_847; // @[]
  wire  _GEN_849 = 4'h4 == io_redirect_bits ? ram_4_cfi_mispredicted : _GEN_848; // @[]
  wire  _GEN_850 = 4'h5 == io_redirect_bits ? ram_5_cfi_mispredicted : _GEN_849; // @[]
  wire  _GEN_851 = 4'h6 == io_redirect_bits ? ram_6_cfi_mispredicted : _GEN_850; // @[]
  wire  _GEN_852 = 4'h7 == io_redirect_bits ? ram_7_cfi_mispredicted : _GEN_851; // @[]
  wire  _GEN_853 = 4'h8 == io_redirect_bits ? ram_8_cfi_mispredicted : _GEN_852; // @[]
  wire  _GEN_854 = 4'h9 == io_redirect_bits ? ram_9_cfi_mispredicted : _GEN_853; // @[]
  wire  _GEN_855 = 4'ha == io_redirect_bits ? ram_10_cfi_mispredicted : _GEN_854; // @[]
  wire  _GEN_856 = 4'hb == io_redirect_bits ? ram_11_cfi_mispredicted : _GEN_855; // @[]
  wire  _GEN_857 = 4'hc == io_redirect_bits ? ram_12_cfi_mispredicted : _GEN_856; // @[]
  wire  _GEN_858 = 4'hd == io_redirect_bits ? ram_13_cfi_mispredicted : _GEN_857; // @[]
  wire  _GEN_859 = 4'he == io_redirect_bits ? ram_14_cfi_mispredicted : _GEN_858; // @[]
  wire  _GEN_860 = 4'hf == io_redirect_bits ? ram_15_cfi_mispredicted : _GEN_859; // @[]
  wire  _GEN_862 = 4'h1 == io_redirect_bits ? ram_1_cfi_taken : ram_0_cfi_taken; // @[]
  wire  _GEN_863 = 4'h2 == io_redirect_bits ? ram_2_cfi_taken : _GEN_862; // @[]
  wire  _GEN_864 = 4'h3 == io_redirect_bits ? ram_3_cfi_taken : _GEN_863; // @[]
  wire  _GEN_865 = 4'h4 == io_redirect_bits ? ram_4_cfi_taken : _GEN_864; // @[]
  wire  _GEN_866 = 4'h5 == io_redirect_bits ? ram_5_cfi_taken : _GEN_865; // @[]
  wire  _GEN_867 = 4'h6 == io_redirect_bits ? ram_6_cfi_taken : _GEN_866; // @[]
  wire  _GEN_868 = 4'h7 == io_redirect_bits ? ram_7_cfi_taken : _GEN_867; // @[]
  wire  _GEN_869 = 4'h8 == io_redirect_bits ? ram_8_cfi_taken : _GEN_868; // @[]
  wire  _GEN_870 = 4'h9 == io_redirect_bits ? ram_9_cfi_taken : _GEN_869; // @[]
  wire  _GEN_871 = 4'ha == io_redirect_bits ? ram_10_cfi_taken : _GEN_870; // @[]
  wire  _GEN_872 = 4'hb == io_redirect_bits ? ram_11_cfi_taken : _GEN_871; // @[]
  wire  _GEN_873 = 4'hc == io_redirect_bits ? ram_12_cfi_taken : _GEN_872; // @[]
  wire  _GEN_874 = 4'hd == io_redirect_bits ? ram_13_cfi_taken : _GEN_873; // @[]
  wire  _GEN_875 = 4'he == io_redirect_bits ? ram_14_cfi_taken : _GEN_874; // @[]
  wire  _GEN_876 = 4'hf == io_redirect_bits ? ram_15_cfi_taken : _GEN_875; // @[]
  wire [1:0] _GEN_878 = 4'h1 == io_redirect_bits ? ram_1_cfi_idx_bits : ram_0_cfi_idx_bits; // @[]
  wire [1:0] _GEN_879 = 4'h2 == io_redirect_bits ? ram_2_cfi_idx_bits : _GEN_878; // @[]
  wire [1:0] _GEN_880 = 4'h3 == io_redirect_bits ? ram_3_cfi_idx_bits : _GEN_879; // @[]
  wire [1:0] _GEN_881 = 4'h4 == io_redirect_bits ? ram_4_cfi_idx_bits : _GEN_880; // @[]
  wire [1:0] _GEN_882 = 4'h5 == io_redirect_bits ? ram_5_cfi_idx_bits : _GEN_881; // @[]
  wire [1:0] _GEN_883 = 4'h6 == io_redirect_bits ? ram_6_cfi_idx_bits : _GEN_882; // @[]
  wire [1:0] _GEN_884 = 4'h7 == io_redirect_bits ? ram_7_cfi_idx_bits : _GEN_883; // @[]
  wire [1:0] _GEN_885 = 4'h8 == io_redirect_bits ? ram_8_cfi_idx_bits : _GEN_884; // @[]
  wire [1:0] _GEN_886 = 4'h9 == io_redirect_bits ? ram_9_cfi_idx_bits : _GEN_885; // @[]
  wire [1:0] _GEN_887 = 4'ha == io_redirect_bits ? ram_10_cfi_idx_bits : _GEN_886; // @[]
  wire [1:0] _GEN_888 = 4'hb == io_redirect_bits ? ram_11_cfi_idx_bits : _GEN_887; // @[]
  wire [1:0] _GEN_889 = 4'hc == io_redirect_bits ? ram_12_cfi_idx_bits : _GEN_888; // @[]
  wire [1:0] _GEN_890 = 4'hd == io_redirect_bits ? ram_13_cfi_idx_bits : _GEN_889; // @[]
  wire [1:0] _GEN_891 = 4'he == io_redirect_bits ? ram_14_cfi_idx_bits : _GEN_890; // @[]
  wire [1:0] _GEN_892 = 4'hf == io_redirect_bits ? ram_15_cfi_idx_bits : _GEN_891; // @[]
  wire  _GEN_894 = 4'h1 == io_redirect_bits ? ram_1_cfi_idx_valid : ram_0_cfi_idx_valid; // @[]
  wire  _GEN_895 = 4'h2 == io_redirect_bits ? ram_2_cfi_idx_valid : _GEN_894; // @[]
  wire  _GEN_896 = 4'h3 == io_redirect_bits ? ram_3_cfi_idx_valid : _GEN_895; // @[]
  wire  _GEN_897 = 4'h4 == io_redirect_bits ? ram_4_cfi_idx_valid : _GEN_896; // @[]
  wire  _GEN_898 = 4'h5 == io_redirect_bits ? ram_5_cfi_idx_valid : _GEN_897; // @[]
  wire  _GEN_899 = 4'h6 == io_redirect_bits ? ram_6_cfi_idx_valid : _GEN_898; // @[]
  wire  _GEN_900 = 4'h7 == io_redirect_bits ? ram_7_cfi_idx_valid : _GEN_899; // @[]
  wire  _GEN_901 = 4'h8 == io_redirect_bits ? ram_8_cfi_idx_valid : _GEN_900; // @[]
  wire  _GEN_902 = 4'h9 == io_redirect_bits ? ram_9_cfi_idx_valid : _GEN_901; // @[]
  wire  _GEN_903 = 4'ha == io_redirect_bits ? ram_10_cfi_idx_valid : _GEN_902; // @[]
  wire  _GEN_904 = 4'hb == io_redirect_bits ? ram_11_cfi_idx_valid : _GEN_903; // @[]
  wire  _GEN_905 = 4'hc == io_redirect_bits ? ram_12_cfi_idx_valid : _GEN_904; // @[]
  wire  _GEN_906 = 4'hd == io_redirect_bits ? ram_13_cfi_idx_valid : _GEN_905; // @[]
  wire  _GEN_907 = 4'he == io_redirect_bits ? ram_14_cfi_idx_valid : _GEN_906; // @[]
  wire  _GEN_908 = 4'hf == io_redirect_bits ? ram_15_cfi_idx_valid : _GEN_907; // @[]
  wire [3:0] _T_147 = io_redirect_bits + 4'h1; // @[util.scala 203:14]
  wire [3:0] _T_151 = redirect_new_entry_start_bank ? 4'h8 : 4'h0; // @[fetch-target-queue.scala 319:10]
  wire [5:0] _GEN_2041 = {{2'd0}, _T_151}; // @[fetch-target-queue.scala 318:50]
  wire [5:0] _T_152 = io_brupdate_b2_uop_pc_lob ^ _GEN_2041; // @[fetch-target-queue.scala 318:50]
  wire  _T_154 = _GEN_892 == _T_152[2:1]; // @[fetch-target-queue.scala 324:104]
  wire  _GEN_909 = io_brupdate_b2_mispredict | _GEN_908; // @[fetch-target-queue.scala 317:38 320:43]
  wire  _GEN_911 = io_brupdate_b2_mispredict | _GEN_860; // @[fetch-target-queue.scala 317:38 322:43]
  reg  _T_158; // @[fetch-target-queue.scala 332:23]
  reg  _T_159_cfi_idx_valid; // @[fetch-target-queue.scala 333:26]
  reg [1:0] _T_159_cfi_idx_bits; // @[fetch-target-queue.scala 333:26]
  reg  _T_159_cfi_taken; // @[fetch-target-queue.scala 333:26]
  reg [3:0] _T_159_br_mask; // @[fetch-target-queue.scala 333:26]
  reg  _T_159_cfi_is_call; // @[fetch-target-queue.scala 333:26]
  reg  _T_159_cfi_is_ret; // @[fetch-target-queue.scala 333:26]
  reg [3:0] _T_160; // @[fetch-target-queue.scala 337:16]
  reg  _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:46]
  reg [1:0] _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:46]
  reg  _T_163_cfi_taken; // @[fetch-target-queue.scala 337:46]
  reg  _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:46]
  reg [2:0] _T_163_cfi_type; // @[fetch-target-queue.scala 337:46]
  reg [3:0] _T_163_br_mask; // @[fetch-target-queue.scala 337:46]
  reg  _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:46]
  reg  _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:46]
  reg  _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:46]
  reg [39:0] _T_163_ras_top; // @[fetch-target-queue.scala 337:46]
  reg [4:0] _T_163_ras_idx; // @[fetch-target-queue.scala 337:46]
  reg  _T_163_start_bank; // @[fetch-target-queue.scala 337:46]
  wire [3:0] _T_165 = io_get_ftq_pc_0_ftq_idx + 4'h1; // @[util.scala 203:14]
  wire  _T_169 = _T_165 == enq_ptr & do_enq; // @[fetch-target-queue.scala 347:46]
  wire [39:0] _GEN_1538 = 4'h1 == _T_165 ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1539 = 4'h2 == _T_165 ? pcs_2 : _GEN_1538; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1540 = 4'h3 == _T_165 ? pcs_3 : _GEN_1539; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1541 = 4'h4 == _T_165 ? pcs_4 : _GEN_1540; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1542 = 4'h5 == _T_165 ? pcs_5 : _GEN_1541; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1543 = 4'h6 == _T_165 ? pcs_6 : _GEN_1542; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1544 = 4'h7 == _T_165 ? pcs_7 : _GEN_1543; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1545 = 4'h8 == _T_165 ? pcs_8 : _GEN_1544; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1546 = 4'h9 == _T_165 ? pcs_9 : _GEN_1545; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1547 = 4'ha == _T_165 ? pcs_10 : _GEN_1546; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1548 = 4'hb == _T_165 ? pcs_11 : _GEN_1547; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1549 = 4'hc == _T_165 ? pcs_12 : _GEN_1548; // @[fetch-target-queue.scala 348:{22,22}]
  reg  _T_171_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
  reg [1:0] _T_171_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
  reg  _T_171_cfi_taken; // @[fetch-target-queue.scala 351:42]
  reg  _T_171_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
  reg [2:0] _T_171_cfi_type; // @[fetch-target-queue.scala 351:42]
  reg [3:0] _T_171_br_mask; // @[fetch-target-queue.scala 351:42]
  reg  _T_171_cfi_is_call; // @[fetch-target-queue.scala 351:42]
  reg  _T_171_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
  reg  _T_171_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
  reg [39:0] _T_171_ras_top; // @[fetch-target-queue.scala 351:42]
  reg [4:0] _T_171_ras_idx; // @[fetch-target-queue.scala 351:42]
  reg  _T_171_start_bank; // @[fetch-target-queue.scala 351:42]
  wire  _GEN_1554 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_start_bank : ram_0_start_bank; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1555 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_start_bank : _GEN_1554; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1556 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_start_bank : _GEN_1555; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1557 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_start_bank : _GEN_1556; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1558 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_start_bank : _GEN_1557; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1559 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_start_bank : _GEN_1558; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1560 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_start_bank : _GEN_1559; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1561 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_start_bank : _GEN_1560; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1562 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_start_bank : _GEN_1561; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1563 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_start_bank : _GEN_1562; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1564 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_start_bank : _GEN_1563; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1570 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_ras_idx : ram_0_ras_idx; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1571 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_ras_idx : _GEN_1570; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1572 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_ras_idx : _GEN_1571; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1573 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_ras_idx : _GEN_1572; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1574 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_ras_idx : _GEN_1573; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1575 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_ras_idx : _GEN_1574; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1576 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_ras_idx : _GEN_1575; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1577 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_ras_idx : _GEN_1576; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1578 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_ras_idx : _GEN_1577; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1579 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_ras_idx : _GEN_1578; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1580 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_ras_idx : _GEN_1579; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1586 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_ras_top : ram_0_ras_top; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1587 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_ras_top : _GEN_1586; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1588 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_ras_top : _GEN_1587; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1589 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_ras_top : _GEN_1588; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1590 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_ras_top : _GEN_1589; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1591 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_ras_top : _GEN_1590; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1592 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_ras_top : _GEN_1591; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1593 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_ras_top : _GEN_1592; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1594 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_ras_top : _GEN_1593; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1595 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_ras_top : _GEN_1594; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1596 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_ras_top : _GEN_1595; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1602 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_npc_plus4 : ram_0_cfi_npc_plus4; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1603 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_npc_plus4 : _GEN_1602; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1604 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_npc_plus4 : _GEN_1603; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1605 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_npc_plus4 : _GEN_1604; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1606 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_npc_plus4 : _GEN_1605; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1607 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_npc_plus4 : _GEN_1606; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1608 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_npc_plus4 : _GEN_1607; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1609 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_npc_plus4 : _GEN_1608; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1610 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_npc_plus4 : _GEN_1609; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1611 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_npc_plus4 : _GEN_1610; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1612 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_npc_plus4 : _GEN_1611; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1618 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_is_ret : ram_0_cfi_is_ret; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1619 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_is_ret : _GEN_1618; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1620 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_is_ret : _GEN_1619; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1621 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_is_ret : _GEN_1620; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1622 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_is_ret : _GEN_1621; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1623 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_is_ret : _GEN_1622; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1624 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_is_ret : _GEN_1623; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1625 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_is_ret : _GEN_1624; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1626 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_is_ret : _GEN_1625; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1627 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_is_ret : _GEN_1626; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1628 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_is_ret : _GEN_1627; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1634 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_is_call : ram_0_cfi_is_call; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1635 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_is_call : _GEN_1634; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1636 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_is_call : _GEN_1635; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1637 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_is_call : _GEN_1636; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1638 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_is_call : _GEN_1637; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1639 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_is_call : _GEN_1638; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1640 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_is_call : _GEN_1639; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1641 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_is_call : _GEN_1640; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1642 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_is_call : _GEN_1641; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1643 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_is_call : _GEN_1642; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1644 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_is_call : _GEN_1643; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1650 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_br_mask : ram_0_br_mask; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1651 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_br_mask : _GEN_1650; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1652 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_br_mask : _GEN_1651; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1653 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_br_mask : _GEN_1652; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1654 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_br_mask : _GEN_1653; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1655 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_br_mask : _GEN_1654; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1656 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_br_mask : _GEN_1655; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1657 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_br_mask : _GEN_1656; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1658 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_br_mask : _GEN_1657; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1659 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_br_mask : _GEN_1658; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1660 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_br_mask : _GEN_1659; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1666 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_type : ram_0_cfi_type; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1667 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_type : _GEN_1666; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1668 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_type : _GEN_1667; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1669 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_type : _GEN_1668; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1670 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_type : _GEN_1669; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1671 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_type : _GEN_1670; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1672 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_type : _GEN_1671; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1673 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_type : _GEN_1672; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1674 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_type : _GEN_1673; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1675 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_type : _GEN_1674; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1676 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_type : _GEN_1675; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1682 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_mispredicted : ram_0_cfi_mispredicted; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1683 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_mispredicted : _GEN_1682; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1684 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_mispredicted : _GEN_1683; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1685 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_mispredicted : _GEN_1684; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1686 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_mispredicted : _GEN_1685; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1687 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_mispredicted : _GEN_1686; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1688 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_mispredicted : _GEN_1687; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1689 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_mispredicted : _GEN_1688; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1690 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_mispredicted : _GEN_1689; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1691 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_mispredicted : _GEN_1690; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1692 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_mispredicted : _GEN_1691; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1698 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_taken : ram_0_cfi_taken; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1699 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_taken : _GEN_1698; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1700 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_taken : _GEN_1699; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1701 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_taken : _GEN_1700; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1702 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_taken : _GEN_1701; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1703 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_taken : _GEN_1702; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1704 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_taken : _GEN_1703; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1705 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_taken : _GEN_1704; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1706 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_taken : _GEN_1705; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1707 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_taken : _GEN_1706; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1708 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_taken : _GEN_1707; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1714 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_idx_bits : ram_0_cfi_idx_bits; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1715 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_idx_bits : _GEN_1714; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1716 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_idx_bits : _GEN_1715; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1717 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_idx_bits : _GEN_1716; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1718 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_idx_bits : _GEN_1717; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1719 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_idx_bits : _GEN_1718; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1720 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_idx_bits : _GEN_1719; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1721 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_idx_bits : _GEN_1720; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1722 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_idx_bits : _GEN_1721; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1723 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_idx_bits : _GEN_1722; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1724 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_idx_bits : _GEN_1723; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1730 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? ram_1_cfi_idx_valid : ram_0_cfi_idx_valid; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1731 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? ram_2_cfi_idx_valid : _GEN_1730; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1732 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? ram_3_cfi_idx_valid : _GEN_1731; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1733 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? ram_4_cfi_idx_valid : _GEN_1732; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1734 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? ram_5_cfi_idx_valid : _GEN_1733; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1735 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? ram_6_cfi_idx_valid : _GEN_1734; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1736 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? ram_7_cfi_idx_valid : _GEN_1735; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1737 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? ram_8_cfi_idx_valid : _GEN_1736; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1738 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? ram_9_cfi_idx_valid : _GEN_1737; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1739 = 4'ha == io_get_ftq_pc_0_ftq_idx ? ram_10_cfi_idx_valid : _GEN_1738; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1740 = 4'hb == io_get_ftq_pc_0_ftq_idx ? ram_11_cfi_idx_valid : _GEN_1739; // @[fetch-target-queue.scala 351:{42,42}]
  reg [39:0] _T_172; // @[fetch-target-queue.scala 356:42]
  wire [39:0] _GEN_1746 = 4'h1 == io_get_ftq_pc_0_ftq_idx ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1747 = 4'h2 == io_get_ftq_pc_0_ftq_idx ? pcs_2 : _GEN_1746; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1748 = 4'h3 == io_get_ftq_pc_0_ftq_idx ? pcs_3 : _GEN_1747; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1749 = 4'h4 == io_get_ftq_pc_0_ftq_idx ? pcs_4 : _GEN_1748; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1750 = 4'h5 == io_get_ftq_pc_0_ftq_idx ? pcs_5 : _GEN_1749; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1751 = 4'h6 == io_get_ftq_pc_0_ftq_idx ? pcs_6 : _GEN_1750; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1752 = 4'h7 == io_get_ftq_pc_0_ftq_idx ? pcs_7 : _GEN_1751; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1753 = 4'h8 == io_get_ftq_pc_0_ftq_idx ? pcs_8 : _GEN_1752; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1754 = 4'h9 == io_get_ftq_pc_0_ftq_idx ? pcs_9 : _GEN_1753; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1755 = 4'ha == io_get_ftq_pc_0_ftq_idx ? pcs_10 : _GEN_1754; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1756 = 4'hb == io_get_ftq_pc_0_ftq_idx ? pcs_11 : _GEN_1755; // @[fetch-target-queue.scala 356:{42,42}]
  reg [39:0] _T_173; // @[fetch-target-queue.scala 357:42]
  reg  _T_176; // @[fetch-target-queue.scala 358:42]
  reg [39:0] _T_178; // @[fetch-target-queue.scala 359:42]
  wire [39:0] _GEN_1762 = 4'h1 == _GEN_445 ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1763 = 4'h2 == _GEN_445 ? pcs_2 : _GEN_1762; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1764 = 4'h3 == _GEN_445 ? pcs_3 : _GEN_1763; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1765 = 4'h4 == _GEN_445 ? pcs_4 : _GEN_1764; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1766 = 4'h5 == _GEN_445 ? pcs_5 : _GEN_1765; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1767 = 4'h6 == _GEN_445 ? pcs_6 : _GEN_1766; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1768 = 4'h7 == _GEN_445 ? pcs_7 : _GEN_1767; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1769 = 4'h8 == _GEN_445 ? pcs_8 : _GEN_1768; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1770 = 4'h9 == _GEN_445 ? pcs_9 : _GEN_1769; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1771 = 4'ha == _GEN_445 ? pcs_10 : _GEN_1770; // @[fetch-target-queue.scala 359:{42,42}]
  wire [39:0] _GEN_1772 = 4'hb == _GEN_445 ? pcs_11 : _GEN_1771; // @[fetch-target-queue.scala 359:{42,42}]
  wire [3:0] _T_180 = io_get_ftq_pc_1_ftq_idx + 4'h1; // @[util.scala 203:14]
  wire  _T_184 = _T_180 == enq_ptr & do_enq; // @[fetch-target-queue.scala 347:46]
  wire [39:0] _GEN_1778 = 4'h1 == _T_180 ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1779 = 4'h2 == _T_180 ? pcs_2 : _GEN_1778; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1780 = 4'h3 == _T_180 ? pcs_3 : _GEN_1779; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1781 = 4'h4 == _T_180 ? pcs_4 : _GEN_1780; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1782 = 4'h5 == _T_180 ? pcs_5 : _GEN_1781; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1783 = 4'h6 == _T_180 ? pcs_6 : _GEN_1782; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1784 = 4'h7 == _T_180 ? pcs_7 : _GEN_1783; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1785 = 4'h8 == _T_180 ? pcs_8 : _GEN_1784; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1786 = 4'h9 == _T_180 ? pcs_9 : _GEN_1785; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1787 = 4'ha == _T_180 ? pcs_10 : _GEN_1786; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1788 = 4'hb == _T_180 ? pcs_11 : _GEN_1787; // @[fetch-target-queue.scala 348:{22,22}]
  wire [39:0] _GEN_1789 = 4'hc == _T_180 ? pcs_12 : _GEN_1788; // @[fetch-target-queue.scala 348:{22,22}]
  reg  _T_186_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
  reg [1:0] _T_186_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
  reg  _T_186_cfi_taken; // @[fetch-target-queue.scala 351:42]
  reg  _T_186_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
  reg [2:0] _T_186_cfi_type; // @[fetch-target-queue.scala 351:42]
  reg [3:0] _T_186_br_mask; // @[fetch-target-queue.scala 351:42]
  reg  _T_186_cfi_is_call; // @[fetch-target-queue.scala 351:42]
  reg  _T_186_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
  reg  _T_186_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
  reg [39:0] _T_186_ras_top; // @[fetch-target-queue.scala 351:42]
  reg [4:0] _T_186_ras_idx; // @[fetch-target-queue.scala 351:42]
  reg  _T_186_start_bank; // @[fetch-target-queue.scala 351:42]
  wire  _GEN_1794 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_start_bank : ram_0_start_bank; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1795 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_start_bank : _GEN_1794; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1796 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_start_bank : _GEN_1795; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1797 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_start_bank : _GEN_1796; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1798 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_start_bank : _GEN_1797; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1799 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_start_bank : _GEN_1798; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1800 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_start_bank : _GEN_1799; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1801 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_start_bank : _GEN_1800; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1802 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_start_bank : _GEN_1801; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1803 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_start_bank : _GEN_1802; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1804 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_start_bank : _GEN_1803; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1810 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_ras_idx : ram_0_ras_idx; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1811 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_ras_idx : _GEN_1810; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1812 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_ras_idx : _GEN_1811; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1813 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_ras_idx : _GEN_1812; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1814 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_ras_idx : _GEN_1813; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1815 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_ras_idx : _GEN_1814; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1816 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_ras_idx : _GEN_1815; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1817 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_ras_idx : _GEN_1816; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1818 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_ras_idx : _GEN_1817; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1819 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_ras_idx : _GEN_1818; // @[fetch-target-queue.scala 351:{42,42}]
  wire [4:0] _GEN_1820 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_ras_idx : _GEN_1819; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1826 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_ras_top : ram_0_ras_top; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1827 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_ras_top : _GEN_1826; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1828 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_ras_top : _GEN_1827; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1829 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_ras_top : _GEN_1828; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1830 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_ras_top : _GEN_1829; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1831 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_ras_top : _GEN_1830; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1832 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_ras_top : _GEN_1831; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1833 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_ras_top : _GEN_1832; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1834 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_ras_top : _GEN_1833; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1835 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_ras_top : _GEN_1834; // @[fetch-target-queue.scala 351:{42,42}]
  wire [39:0] _GEN_1836 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_ras_top : _GEN_1835; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1842 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_npc_plus4 : ram_0_cfi_npc_plus4; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1843 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_npc_plus4 : _GEN_1842; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1844 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_npc_plus4 : _GEN_1843; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1845 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_npc_plus4 : _GEN_1844; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1846 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_npc_plus4 : _GEN_1845; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1847 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_npc_plus4 : _GEN_1846; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1848 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_npc_plus4 : _GEN_1847; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1849 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_npc_plus4 : _GEN_1848; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1850 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_npc_plus4 : _GEN_1849; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1851 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_npc_plus4 : _GEN_1850; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1852 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_npc_plus4 : _GEN_1851; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1858 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_is_ret : ram_0_cfi_is_ret; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1859 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_is_ret : _GEN_1858; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1860 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_is_ret : _GEN_1859; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1861 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_is_ret : _GEN_1860; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1862 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_is_ret : _GEN_1861; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1863 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_is_ret : _GEN_1862; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1864 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_is_ret : _GEN_1863; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1865 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_is_ret : _GEN_1864; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1866 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_is_ret : _GEN_1865; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1867 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_is_ret : _GEN_1866; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1868 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_is_ret : _GEN_1867; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1874 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_is_call : ram_0_cfi_is_call; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1875 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_is_call : _GEN_1874; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1876 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_is_call : _GEN_1875; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1877 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_is_call : _GEN_1876; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1878 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_is_call : _GEN_1877; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1879 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_is_call : _GEN_1878; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1880 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_is_call : _GEN_1879; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1881 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_is_call : _GEN_1880; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1882 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_is_call : _GEN_1881; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1883 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_is_call : _GEN_1882; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1884 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_is_call : _GEN_1883; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1890 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_br_mask : ram_0_br_mask; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1891 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_br_mask : _GEN_1890; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1892 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_br_mask : _GEN_1891; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1893 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_br_mask : _GEN_1892; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1894 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_br_mask : _GEN_1893; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1895 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_br_mask : _GEN_1894; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1896 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_br_mask : _GEN_1895; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1897 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_br_mask : _GEN_1896; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1898 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_br_mask : _GEN_1897; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1899 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_br_mask : _GEN_1898; // @[fetch-target-queue.scala 351:{42,42}]
  wire [3:0] _GEN_1900 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_br_mask : _GEN_1899; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1906 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_type : ram_0_cfi_type; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1907 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_type : _GEN_1906; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1908 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_type : _GEN_1907; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1909 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_type : _GEN_1908; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1910 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_type : _GEN_1909; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1911 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_type : _GEN_1910; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1912 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_type : _GEN_1911; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1913 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_type : _GEN_1912; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1914 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_type : _GEN_1913; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1915 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_type : _GEN_1914; // @[fetch-target-queue.scala 351:{42,42}]
  wire [2:0] _GEN_1916 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_type : _GEN_1915; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1922 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_mispredicted : ram_0_cfi_mispredicted; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1923 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_mispredicted : _GEN_1922; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1924 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_mispredicted : _GEN_1923; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1925 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_mispredicted : _GEN_1924; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1926 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_mispredicted : _GEN_1925; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1927 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_mispredicted : _GEN_1926; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1928 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_mispredicted : _GEN_1927; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1929 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_mispredicted : _GEN_1928; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1930 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_mispredicted : _GEN_1929; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1931 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_mispredicted : _GEN_1930; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1932 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_mispredicted : _GEN_1931; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1938 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_taken : ram_0_cfi_taken; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1939 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_taken : _GEN_1938; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1940 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_taken : _GEN_1939; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1941 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_taken : _GEN_1940; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1942 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_taken : _GEN_1941; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1943 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_taken : _GEN_1942; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1944 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_taken : _GEN_1943; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1945 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_taken : _GEN_1944; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1946 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_taken : _GEN_1945; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1947 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_taken : _GEN_1946; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1948 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_taken : _GEN_1947; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1954 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_idx_bits : ram_0_cfi_idx_bits; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1955 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_idx_bits : _GEN_1954; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1956 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_idx_bits : _GEN_1955; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1957 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_idx_bits : _GEN_1956; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1958 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_idx_bits : _GEN_1957; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1959 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_idx_bits : _GEN_1958; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1960 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_idx_bits : _GEN_1959; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1961 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_idx_bits : _GEN_1960; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1962 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_idx_bits : _GEN_1961; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1963 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_idx_bits : _GEN_1962; // @[fetch-target-queue.scala 351:{42,42}]
  wire [1:0] _GEN_1964 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_idx_bits : _GEN_1963; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1970 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? ram_1_cfi_idx_valid : ram_0_cfi_idx_valid; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1971 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? ram_2_cfi_idx_valid : _GEN_1970; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1972 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? ram_3_cfi_idx_valid : _GEN_1971; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1973 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? ram_4_cfi_idx_valid : _GEN_1972; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1974 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? ram_5_cfi_idx_valid : _GEN_1973; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1975 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? ram_6_cfi_idx_valid : _GEN_1974; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1976 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? ram_7_cfi_idx_valid : _GEN_1975; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1977 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? ram_8_cfi_idx_valid : _GEN_1976; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1978 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? ram_9_cfi_idx_valid : _GEN_1977; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1979 = 4'ha == io_get_ftq_pc_1_ftq_idx ? ram_10_cfi_idx_valid : _GEN_1978; // @[fetch-target-queue.scala 351:{42,42}]
  wire  _GEN_1980 = 4'hb == io_get_ftq_pc_1_ftq_idx ? ram_11_cfi_idx_valid : _GEN_1979; // @[fetch-target-queue.scala 351:{42,42}]
  reg [39:0] _T_191; // @[fetch-target-queue.scala 356:42]
  wire [39:0] _GEN_1988 = 4'h1 == io_get_ftq_pc_1_ftq_idx ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1989 = 4'h2 == io_get_ftq_pc_1_ftq_idx ? pcs_2 : _GEN_1988; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1990 = 4'h3 == io_get_ftq_pc_1_ftq_idx ? pcs_3 : _GEN_1989; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1991 = 4'h4 == io_get_ftq_pc_1_ftq_idx ? pcs_4 : _GEN_1990; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1992 = 4'h5 == io_get_ftq_pc_1_ftq_idx ? pcs_5 : _GEN_1991; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1993 = 4'h6 == io_get_ftq_pc_1_ftq_idx ? pcs_6 : _GEN_1992; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1994 = 4'h7 == io_get_ftq_pc_1_ftq_idx ? pcs_7 : _GEN_1993; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1995 = 4'h8 == io_get_ftq_pc_1_ftq_idx ? pcs_8 : _GEN_1994; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1996 = 4'h9 == io_get_ftq_pc_1_ftq_idx ? pcs_9 : _GEN_1995; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1997 = 4'ha == io_get_ftq_pc_1_ftq_idx ? pcs_10 : _GEN_1996; // @[fetch-target-queue.scala 356:{42,42}]
  wire [39:0] _GEN_1998 = 4'hb == io_get_ftq_pc_1_ftq_idx ? pcs_11 : _GEN_1997; // @[fetch-target-queue.scala 356:{42,42}]
  reg [39:0] _T_192; // @[fetch-target-queue.scala 357:42]
  reg  _T_195; // @[fetch-target-queue.scala 358:42]
  reg [39:0] _T_197; // @[fetch-target-queue.scala 359:42]
  reg [39:0] _T_198; // @[fetch-target-queue.scala 363:36]
  wire [39:0] _GEN_2020 = 4'h1 == io_debug_ftq_idx_0 ? pcs_1 : pcs_0; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2021 = 4'h2 == io_debug_ftq_idx_0 ? pcs_2 : _GEN_2020; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2022 = 4'h3 == io_debug_ftq_idx_0 ? pcs_3 : _GEN_2021; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2023 = 4'h4 == io_debug_ftq_idx_0 ? pcs_4 : _GEN_2022; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2024 = 4'h5 == io_debug_ftq_idx_0 ? pcs_5 : _GEN_2023; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2025 = 4'h6 == io_debug_ftq_idx_0 ? pcs_6 : _GEN_2024; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2026 = 4'h7 == io_debug_ftq_idx_0 ? pcs_7 : _GEN_2025; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2027 = 4'h8 == io_debug_ftq_idx_0 ? pcs_8 : _GEN_2026; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2028 = 4'h9 == io_debug_ftq_idx_0 ? pcs_9 : _GEN_2027; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2029 = 4'ha == io_debug_ftq_idx_0 ? pcs_10 : _GEN_2028; // @[fetch-target-queue.scala 363:{36,36}]
  wire [39:0] _GEN_2030 = 4'hb == io_debug_ftq_idx_0 ? pcs_11 : _GEN_2029; // @[fetch-target-queue.scala 363:{36,36}]
  assign meta_0_bpd_meta_en = meta_0_bpd_meta_en_pipe_0;
  assign meta_0_bpd_meta_addr = meta_0_bpd_meta_addr_pipe_0;
  assign meta_0_bpd_meta_data = meta_0[meta_0_bpd_meta_addr]; // @[fetch-target-queue.scala 142:29]
  assign meta_0__T_56_data = {{119'd0}, io_enq_bits_bpd_meta_0};
  assign meta_0__T_56_addr = enq_ptr;
  assign meta_0__T_56_mask = 1'h1;
  assign meta_0__T_56_en = io_enq_ready & io_enq_valid;
  assign ghist_0_old_history_bpd_ghist_en = ghist_0_old_history_bpd_ghist_en_pipe_0;
  assign ghist_0_old_history_bpd_ghist_addr = ghist_0_old_history_bpd_ghist_addr_pipe_0;
  assign ghist_0_old_history_bpd_ghist_data = ghist_0_old_history[ghist_0_old_history_bpd_ghist_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_0_old_history__T_54_data = io_enq_bits_ghist_current_saw_branch_not_taken ? io_enq_bits_ghist_old_history
     : _T_42[63:0];
  assign ghist_0_old_history__T_54_addr = enq_ptr;
  assign ghist_0_old_history__T_54_mask = 1'h1;
  assign ghist_0_old_history__T_54_en = io_enq_ready & io_enq_valid;
  assign ghist_0_current_saw_branch_not_taken_bpd_ghist_en = ghist_0_current_saw_branch_not_taken_bpd_ghist_en_pipe_0;
  assign ghist_0_current_saw_branch_not_taken_bpd_ghist_addr =
    ghist_0_current_saw_branch_not_taken_bpd_ghist_addr_pipe_0;
  assign ghist_0_current_saw_branch_not_taken_bpd_ghist_data =
    ghist_0_current_saw_branch_not_taken[ghist_0_current_saw_branch_not_taken_bpd_ghist_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_0_current_saw_branch_not_taken__T_54_data = io_enq_bits_ghist_current_saw_branch_not_taken;
  assign ghist_0_current_saw_branch_not_taken__T_54_addr = enq_ptr;
  assign ghist_0_current_saw_branch_not_taken__T_54_mask = 1'h1;
  assign ghist_0_current_saw_branch_not_taken__T_54_en = io_enq_ready & io_enq_valid;
  assign ghist_0_new_saw_branch_not_taken_bpd_ghist_en = ghist_0_new_saw_branch_not_taken_bpd_ghist_en_pipe_0;
  assign ghist_0_new_saw_branch_not_taken_bpd_ghist_addr = ghist_0_new_saw_branch_not_taken_bpd_ghist_addr_pipe_0;
  assign ghist_0_new_saw_branch_not_taken_bpd_ghist_data =
    ghist_0_new_saw_branch_not_taken[ghist_0_new_saw_branch_not_taken_bpd_ghist_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_0_new_saw_branch_not_taken__T_54_data = io_enq_bits_ghist_current_saw_branch_not_taken &
    io_enq_bits_ghist_new_saw_branch_not_taken;
  assign ghist_0_new_saw_branch_not_taken__T_54_addr = enq_ptr;
  assign ghist_0_new_saw_branch_not_taken__T_54_mask = 1'h1;
  assign ghist_0_new_saw_branch_not_taken__T_54_en = io_enq_ready & io_enq_valid;
  assign ghist_0_new_saw_branch_taken_bpd_ghist_en = ghist_0_new_saw_branch_taken_bpd_ghist_en_pipe_0;
  assign ghist_0_new_saw_branch_taken_bpd_ghist_addr = ghist_0_new_saw_branch_taken_bpd_ghist_addr_pipe_0;
  assign ghist_0_new_saw_branch_taken_bpd_ghist_data =
    ghist_0_new_saw_branch_taken[ghist_0_new_saw_branch_taken_bpd_ghist_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_0_new_saw_branch_taken__T_54_data = io_enq_bits_ghist_current_saw_branch_not_taken &
    io_enq_bits_ghist_new_saw_branch_taken;
  assign ghist_0_new_saw_branch_taken__T_54_addr = enq_ptr;
  assign ghist_0_new_saw_branch_taken__T_54_mask = 1'h1;
  assign ghist_0_new_saw_branch_taken__T_54_en = io_enq_ready & io_enq_valid;
  assign ghist_0_ras_idx_bpd_ghist_en = ghist_0_ras_idx_bpd_ghist_en_pipe_0;
  assign ghist_0_ras_idx_bpd_ghist_addr = ghist_0_ras_idx_bpd_ghist_addr_pipe_0;
  assign ghist_0_ras_idx_bpd_ghist_data = ghist_0_ras_idx[ghist_0_ras_idx_bpd_ghist_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_0_ras_idx__T_54_data = io_enq_bits_ghist_current_saw_branch_not_taken ? io_enq_bits_ghist_ras_idx : _T_52
    ;
  assign ghist_0_ras_idx__T_54_addr = enq_ptr;
  assign ghist_0_ras_idx__T_54_mask = 1'h1;
  assign ghist_0_ras_idx__T_54_en = io_enq_ready & io_enq_valid;
  assign ghist_1_old_history__T_190_en = ghist_1_old_history__T_190_en_pipe_0;
  assign ghist_1_old_history__T_190_addr = ghist_1_old_history__T_190_addr_pipe_0;
  assign ghist_1_old_history__T_190_data = ghist_1_old_history[ghist_1_old_history__T_190_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_1_old_history__T_55_data = io_enq_bits_ghist_current_saw_branch_not_taken ? io_enq_bits_ghist_old_history
     : _T_42[63:0];
  assign ghist_1_old_history__T_55_addr = enq_ptr;
  assign ghist_1_old_history__T_55_mask = 1'h1;
  assign ghist_1_old_history__T_55_en = io_enq_ready & io_enq_valid;
  assign ghist_1_current_saw_branch_not_taken__T_190_en = ghist_1_current_saw_branch_not_taken__T_190_en_pipe_0;
  assign ghist_1_current_saw_branch_not_taken__T_190_addr = ghist_1_current_saw_branch_not_taken__T_190_addr_pipe_0;
  assign ghist_1_current_saw_branch_not_taken__T_190_data =
    ghist_1_current_saw_branch_not_taken[ghist_1_current_saw_branch_not_taken__T_190_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_1_current_saw_branch_not_taken__T_55_data = io_enq_bits_ghist_current_saw_branch_not_taken;
  assign ghist_1_current_saw_branch_not_taken__T_55_addr = enq_ptr;
  assign ghist_1_current_saw_branch_not_taken__T_55_mask = 1'h1;
  assign ghist_1_current_saw_branch_not_taken__T_55_en = io_enq_ready & io_enq_valid;
  assign ghist_1_new_saw_branch_not_taken__T_190_en = ghist_1_new_saw_branch_not_taken__T_190_en_pipe_0;
  assign ghist_1_new_saw_branch_not_taken__T_190_addr = ghist_1_new_saw_branch_not_taken__T_190_addr_pipe_0;
  assign ghist_1_new_saw_branch_not_taken__T_190_data =
    ghist_1_new_saw_branch_not_taken[ghist_1_new_saw_branch_not_taken__T_190_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_1_new_saw_branch_not_taken__T_55_data = io_enq_bits_ghist_current_saw_branch_not_taken &
    io_enq_bits_ghist_new_saw_branch_not_taken;
  assign ghist_1_new_saw_branch_not_taken__T_55_addr = enq_ptr;
  assign ghist_1_new_saw_branch_not_taken__T_55_mask = 1'h1;
  assign ghist_1_new_saw_branch_not_taken__T_55_en = io_enq_ready & io_enq_valid;
  assign ghist_1_new_saw_branch_taken__T_190_en = ghist_1_new_saw_branch_taken__T_190_en_pipe_0;
  assign ghist_1_new_saw_branch_taken__T_190_addr = ghist_1_new_saw_branch_taken__T_190_addr_pipe_0;
  assign ghist_1_new_saw_branch_taken__T_190_data =
    ghist_1_new_saw_branch_taken[ghist_1_new_saw_branch_taken__T_190_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_1_new_saw_branch_taken__T_55_data = io_enq_bits_ghist_current_saw_branch_not_taken &
    io_enq_bits_ghist_new_saw_branch_taken;
  assign ghist_1_new_saw_branch_taken__T_55_addr = enq_ptr;
  assign ghist_1_new_saw_branch_taken__T_55_mask = 1'h1;
  assign ghist_1_new_saw_branch_taken__T_55_en = io_enq_ready & io_enq_valid;
  assign ghist_1_ras_idx__T_190_en = ghist_1_ras_idx__T_190_en_pipe_0;
  assign ghist_1_ras_idx__T_190_addr = ghist_1_ras_idx__T_190_addr_pipe_0;
  assign ghist_1_ras_idx__T_190_data = ghist_1_ras_idx[ghist_1_ras_idx__T_190_addr]; // @[fetch-target-queue.scala 144:43]
  assign ghist_1_ras_idx__T_55_data = io_enq_bits_ghist_current_saw_branch_not_taken ? io_enq_bits_ghist_ras_idx : _T_52
    ;
  assign ghist_1_ras_idx__T_55_addr = enq_ptr;
  assign ghist_1_ras_idx__T_55_mask = 1'h1;
  assign ghist_1_ras_idx__T_55_en = io_enq_ready & io_enq_valid;
  assign io_enq_ready = _T_145; // @[fetch-target-queue.scala 308:16]
  assign io_enq_idx = enq_ptr; // @[fetch-target-queue.scala 204:14]
  assign io_get_ftq_pc_0_entry_cfi_idx_valid = _T_171_cfi_idx_valid; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_cfi_idx_bits = _T_171_cfi_idx_bits; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_cfi_taken = _T_171_cfi_taken; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_cfi_mispredicted = _T_171_cfi_mispredicted; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_cfi_type = _T_171_cfi_type; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_br_mask = _T_171_br_mask; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_cfi_is_call = _T_171_cfi_is_call; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_cfi_is_ret = _T_171_cfi_is_ret; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_cfi_npc_plus4 = _T_171_cfi_npc_plus4; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_ras_top = _T_171_ras_top; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_ras_idx = _T_171_ras_idx; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_entry_start_bank = _T_171_start_bank; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_0_ghist_old_history = 64'h0;
  assign io_get_ftq_pc_0_ghist_current_saw_branch_not_taken = 1'h0;
  assign io_get_ftq_pc_0_ghist_new_saw_branch_not_taken = 1'h0;
  assign io_get_ftq_pc_0_ghist_new_saw_branch_taken = 1'h0;
  assign io_get_ftq_pc_0_ghist_ras_idx = 5'h0;
  assign io_get_ftq_pc_0_pc = _T_172; // @[fetch-target-queue.scala 356:32]
  assign io_get_ftq_pc_0_com_pc = _T_178; // @[fetch-target-queue.scala 359:32]
  assign io_get_ftq_pc_0_next_val = _T_176; // @[fetch-target-queue.scala 358:32]
  assign io_get_ftq_pc_0_next_pc = _T_173; // @[fetch-target-queue.scala 357:32]
  assign io_get_ftq_pc_1_entry_cfi_idx_valid = _T_186_cfi_idx_valid; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_cfi_idx_bits = _T_186_cfi_idx_bits; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_cfi_taken = _T_186_cfi_taken; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_cfi_mispredicted = _T_186_cfi_mispredicted; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_cfi_type = _T_186_cfi_type; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_br_mask = _T_186_br_mask; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_cfi_is_call = _T_186_cfi_is_call; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_cfi_is_ret = _T_186_cfi_is_ret; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_cfi_npc_plus4 = _T_186_cfi_npc_plus4; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_ras_top = _T_186_ras_top; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_ras_idx = _T_186_ras_idx; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_entry_start_bank = _T_186_start_bank; // @[fetch-target-queue.scala 351:32]
  assign io_get_ftq_pc_1_ghist_old_history = ghist_1_old_history__T_190_data; // @[fetch-target-queue.scala 353:32]
  assign io_get_ftq_pc_1_ghist_current_saw_branch_not_taken = ghist_1_current_saw_branch_not_taken__T_190_data; // @[fetch-target-queue.scala 353:32]
  assign io_get_ftq_pc_1_ghist_new_saw_branch_not_taken = ghist_1_new_saw_branch_not_taken__T_190_data; // @[fetch-target-queue.scala 353:32]
  assign io_get_ftq_pc_1_ghist_new_saw_branch_taken = ghist_1_new_saw_branch_taken__T_190_data; // @[fetch-target-queue.scala 353:32]
  assign io_get_ftq_pc_1_ghist_ras_idx = ghist_1_ras_idx__T_190_data; // @[fetch-target-queue.scala 353:32]
  assign io_get_ftq_pc_1_pc = _T_191; // @[fetch-target-queue.scala 356:32]
  assign io_get_ftq_pc_1_com_pc = _T_197; // @[fetch-target-queue.scala 359:32]
  assign io_get_ftq_pc_1_next_val = _T_195; // @[fetch-target-queue.scala 358:32]
  assign io_get_ftq_pc_1_next_pc = _T_192; // @[fetch-target-queue.scala 357:32]
  assign io_debug_fetch_pc_0 = _T_198; // @[fetch-target-queue.scala 363:26]
  assign io_bpdupdate_valid = _T_112 & _T_122; // @[fetch-target-queue.scala 206:22 278:80 282:24]
  assign io_bpdupdate_bits_is_mispredict_update = _T_123; // @[fetch-target-queue.scala 278:80 285:44]
  assign io_bpdupdate_bits_is_repair_update = _T_124; // @[fetch-target-queue.scala 278:80 286:44]
  assign io_bpdupdate_bits_btb_mispredicts = 4'h0;
  assign io_bpdupdate_bits_pc = bpd_pc; // @[fetch-target-queue.scala 278:80 287:31]
  assign io_bpdupdate_bits_br_mask = bpd_entry_cfi_idx_valid ? _T_133 : bpd_entry_br_mask; // @[fetch-target-queue.scala 289:37]
  assign io_bpdupdate_bits_cfi_idx_valid = bpd_entry_cfi_idx_valid; // @[fetch-target-queue.scala 278:80 291:31]
  assign io_bpdupdate_bits_cfi_idx_bits = bpd_entry_cfi_idx_bits; // @[fetch-target-queue.scala 278:80 291:31]
  assign io_bpdupdate_bits_cfi_taken = bpd_entry_cfi_taken; // @[fetch-target-queue.scala 278:80 293:34]
  assign io_bpdupdate_bits_cfi_mispredicted = bpd_entry_cfi_mispredicted; // @[fetch-target-queue.scala 278:80 292:40]
  assign io_bpdupdate_bits_cfi_is_br = _T_135[0]; // @[fetch-target-queue.scala 295:54]
  assign io_bpdupdate_bits_cfi_is_jal = bpd_entry_cfi_type == 3'h2 | bpd_entry_cfi_type == 3'h3; // @[fetch-target-queue.scala 296:68]
  assign io_bpdupdate_bits_cfi_is_jalr = 1'h0;
  assign io_bpdupdate_bits_ghist_old_history = ghist_0_old_history_bpd_ghist_data; // @[fetch-target-queue.scala 278:80 297:34]
  assign io_bpdupdate_bits_ghist_current_saw_branch_not_taken = ghist_0_current_saw_branch_not_taken_bpd_ghist_data; // @[fetch-target-queue.scala 278:80 297:34]
  assign io_bpdupdate_bits_ghist_new_saw_branch_not_taken = ghist_0_new_saw_branch_not_taken_bpd_ghist_data; // @[fetch-target-queue.scala 278:80 297:34]
  assign io_bpdupdate_bits_ghist_new_saw_branch_taken = ghist_0_new_saw_branch_taken_bpd_ghist_data; // @[fetch-target-queue.scala 278:80 297:34]
  assign io_bpdupdate_bits_ghist_ras_idx = ghist_0_ras_idx_bpd_ghist_data; // @[fetch-target-queue.scala 278:80 297:34]
  assign io_bpdupdate_bits_lhist_0 = 1'h0; // @[fetch-target-queue.scala 239:{12,12}]
  assign io_bpdupdate_bits_target = bpd_target; // @[fetch-target-queue.scala 278:80 294:34]
  assign io_bpdupdate_bits_meta_0 = meta_0_bpd_meta_data; // @[fetch-target-queue.scala 278:80 299:34]
  assign io_ras_update = _T_60; // @[fetch-target-queue.scala 222:21]
  assign io_ras_update_idx = _T_62; // @[fetch-target-queue.scala 224:21]
  assign io_ras_update_pc = _T_61; // @[fetch-target-queue.scala 223:21]
  always @(posedge clock) begin
    if (meta_0__T_56_en & meta_0__T_56_mask) begin
      meta_0[meta_0__T_56_addr] <= meta_0__T_56_data; // @[fetch-target-queue.scala 142:29]
    end
    meta_0_bpd_meta_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      if (io_redirect_valid) begin
        meta_0_bpd_meta_addr_pipe_0 <= io_redirect_bits;
      end else if (bpd_update_repair | bpd_update_mispredict) begin // @[fetch-target-queue.scala 233:8]
        meta_0_bpd_meta_addr_pipe_0 <= bpd_repair_idx;
      end else begin
        meta_0_bpd_meta_addr_pipe_0 <= bpd_ptr;
      end
    end
    if (ghist_0_old_history__T_54_en & ghist_0_old_history__T_54_mask) begin
      ghist_0_old_history[ghist_0_old_history__T_54_addr] <= ghist_0_old_history__T_54_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_0_old_history_bpd_ghist_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      if (io_redirect_valid) begin
        ghist_0_old_history_bpd_ghist_addr_pipe_0 <= io_redirect_bits;
      end else if (bpd_update_repair | bpd_update_mispredict) begin // @[fetch-target-queue.scala 233:8]
        ghist_0_old_history_bpd_ghist_addr_pipe_0 <= bpd_repair_idx;
      end else begin
        ghist_0_old_history_bpd_ghist_addr_pipe_0 <= bpd_ptr;
      end
    end
    if (ghist_0_current_saw_branch_not_taken__T_54_en & ghist_0_current_saw_branch_not_taken__T_54_mask) begin
      ghist_0_current_saw_branch_not_taken[ghist_0_current_saw_branch_not_taken__T_54_addr] <=
        ghist_0_current_saw_branch_not_taken__T_54_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_0_current_saw_branch_not_taken_bpd_ghist_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      if (io_redirect_valid) begin
        ghist_0_current_saw_branch_not_taken_bpd_ghist_addr_pipe_0 <= io_redirect_bits;
      end else if (bpd_update_repair | bpd_update_mispredict) begin // @[fetch-target-queue.scala 233:8]
        ghist_0_current_saw_branch_not_taken_bpd_ghist_addr_pipe_0 <= bpd_repair_idx;
      end else begin
        ghist_0_current_saw_branch_not_taken_bpd_ghist_addr_pipe_0 <= bpd_ptr;
      end
    end
    if (ghist_0_new_saw_branch_not_taken__T_54_en & ghist_0_new_saw_branch_not_taken__T_54_mask) begin
      ghist_0_new_saw_branch_not_taken[ghist_0_new_saw_branch_not_taken__T_54_addr] <=
        ghist_0_new_saw_branch_not_taken__T_54_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_0_new_saw_branch_not_taken_bpd_ghist_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      if (io_redirect_valid) begin
        ghist_0_new_saw_branch_not_taken_bpd_ghist_addr_pipe_0 <= io_redirect_bits;
      end else if (bpd_update_repair | bpd_update_mispredict) begin // @[fetch-target-queue.scala 233:8]
        ghist_0_new_saw_branch_not_taken_bpd_ghist_addr_pipe_0 <= bpd_repair_idx;
      end else begin
        ghist_0_new_saw_branch_not_taken_bpd_ghist_addr_pipe_0 <= bpd_ptr;
      end
    end
    if (ghist_0_new_saw_branch_taken__T_54_en & ghist_0_new_saw_branch_taken__T_54_mask) begin
      ghist_0_new_saw_branch_taken[ghist_0_new_saw_branch_taken__T_54_addr] <= ghist_0_new_saw_branch_taken__T_54_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_0_new_saw_branch_taken_bpd_ghist_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      if (io_redirect_valid) begin
        ghist_0_new_saw_branch_taken_bpd_ghist_addr_pipe_0 <= io_redirect_bits;
      end else if (bpd_update_repair | bpd_update_mispredict) begin // @[fetch-target-queue.scala 233:8]
        ghist_0_new_saw_branch_taken_bpd_ghist_addr_pipe_0 <= bpd_repair_idx;
      end else begin
        ghist_0_new_saw_branch_taken_bpd_ghist_addr_pipe_0 <= bpd_ptr;
      end
    end
    if (ghist_0_ras_idx__T_54_en & ghist_0_ras_idx__T_54_mask) begin
      ghist_0_ras_idx[ghist_0_ras_idx__T_54_addr] <= ghist_0_ras_idx__T_54_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_0_ras_idx_bpd_ghist_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      if (io_redirect_valid) begin
        ghist_0_ras_idx_bpd_ghist_addr_pipe_0 <= io_redirect_bits;
      end else if (bpd_update_repair | bpd_update_mispredict) begin // @[fetch-target-queue.scala 233:8]
        ghist_0_ras_idx_bpd_ghist_addr_pipe_0 <= bpd_repair_idx;
      end else begin
        ghist_0_ras_idx_bpd_ghist_addr_pipe_0 <= bpd_ptr;
      end
    end
    if (ghist_1_old_history__T_55_en & ghist_1_old_history__T_55_mask) begin
      ghist_1_old_history[ghist_1_old_history__T_55_addr] <= ghist_1_old_history__T_55_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_1_old_history__T_190_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      ghist_1_old_history__T_190_addr_pipe_0 <= io_get_ftq_pc_1_ftq_idx;
    end
    if (ghist_1_current_saw_branch_not_taken__T_55_en & ghist_1_current_saw_branch_not_taken__T_55_mask) begin
      ghist_1_current_saw_branch_not_taken[ghist_1_current_saw_branch_not_taken__T_55_addr] <=
        ghist_1_current_saw_branch_not_taken__T_55_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_1_current_saw_branch_not_taken__T_190_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      ghist_1_current_saw_branch_not_taken__T_190_addr_pipe_0 <= io_get_ftq_pc_1_ftq_idx;
    end
    if (ghist_1_new_saw_branch_not_taken__T_55_en & ghist_1_new_saw_branch_not_taken__T_55_mask) begin
      ghist_1_new_saw_branch_not_taken[ghist_1_new_saw_branch_not_taken__T_55_addr] <=
        ghist_1_new_saw_branch_not_taken__T_55_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_1_new_saw_branch_not_taken__T_190_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      ghist_1_new_saw_branch_not_taken__T_190_addr_pipe_0 <= io_get_ftq_pc_1_ftq_idx;
    end
    if (ghist_1_new_saw_branch_taken__T_55_en & ghist_1_new_saw_branch_taken__T_55_mask) begin
      ghist_1_new_saw_branch_taken[ghist_1_new_saw_branch_taken__T_55_addr] <= ghist_1_new_saw_branch_taken__T_55_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_1_new_saw_branch_taken__T_190_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      ghist_1_new_saw_branch_taken__T_190_addr_pipe_0 <= io_get_ftq_pc_1_ftq_idx;
    end
    if (ghist_1_ras_idx__T_55_en & ghist_1_ras_idx__T_55_mask) begin
      ghist_1_ras_idx[ghist_1_ras_idx__T_55_addr] <= ghist_1_ras_idx__T_55_data; // @[fetch-target-queue.scala 144:43]
    end
    ghist_1_ras_idx__T_190_en_pipe_0 <= 1'h1;
    if (1'h1) begin
      ghist_1_ras_idx__T_190_addr_pipe_0 <= io_get_ftq_pc_1_ftq_idx;
    end
    if (reset) begin // @[fetch-target-queue.scala 133:27]
      bpd_ptr <= 4'h0; // @[fetch-target-queue.scala 133:27]
    end else if (do_commit_update) begin // @[fetch-target-queue.scala 304:27]
      bpd_ptr <= _T_100; // @[fetch-target-queue.scala 305:13]
    end
    if (reset) begin // @[fetch-target-queue.scala 134:27]
      deq_ptr <= 4'h0; // @[fetch-target-queue.scala 134:27]
    end else if (io_deq_valid) begin // @[fetch-target-queue.scala 209:23]
      deq_ptr <= io_deq_bits; // @[fetch-target-queue.scala 210:13]
    end
    if (reset) begin // @[fetch-target-queue.scala 135:27]
      enq_ptr <= 4'h1; // @[fetch-target-queue.scala 135:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      enq_ptr <= _T_147; // @[fetch-target-queue.scala 315:16]
    end else if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      enq_ptr <= _T_1; // @[fetch-target-queue.scala 201:13]
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h0 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_0 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h1 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_1 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h2 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_2 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h3 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_3 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h4 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_4 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h5 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_5 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h6 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_6 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h7 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_7 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h8 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_8 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'h9 == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_9 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'ha == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_10 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'hb == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_11 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'hc == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_12 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'hd == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_13 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'he == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_14 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (do_enq) begin // @[fetch-target-queue.scala 158:17]
      if (4'hf == enq_ptr) begin // @[fetch-target-queue.scala 160:28]
        pcs_15 <= io_enq_bits_pc; // @[fetch-target-queue.scala 160:28]
      end
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_idx_valid <= _GEN_410;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_idx_valid <= _GEN_410;
      end
    end else begin
      ram_0_cfi_idx_valid <= _GEN_410;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_idx_bits <= _GEN_394;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_idx_bits <= _GEN_394;
      end
    end else begin
      ram_0_cfi_idx_bits <= _GEN_394;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_taken <= _GEN_378;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_taken <= _GEN_378;
      end
    end else begin
      ram_0_cfi_taken <= _GEN_378;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_mispredicted <= _GEN_362;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_mispredicted <= _GEN_362;
      end
    end else begin
      ram_0_cfi_mispredicted <= _GEN_362;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_type <= _GEN_346;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_type <= _GEN_346;
      end
    end else begin
      ram_0_cfi_type <= _GEN_346;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_br_mask <= _GEN_330;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_br_mask <= _GEN_330;
      end
    end else begin
      ram_0_br_mask <= _GEN_330;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_is_call <= _GEN_314;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_is_call <= _GEN_314;
      end
    end else begin
      ram_0_cfi_is_call <= _GEN_314;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_is_ret <= _GEN_298;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_is_ret <= _GEN_298;
      end
    end else begin
      ram_0_cfi_is_ret <= _GEN_298;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_cfi_npc_plus4 <= _GEN_282;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_cfi_npc_plus4 <= _GEN_282;
      end
    end else begin
      ram_0_cfi_npc_plus4 <= _GEN_282;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_ras_top <= _GEN_266;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_ras_top <= _GEN_266;
      end
    end else begin
      ram_0_ras_top <= _GEN_266;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_ras_idx <= _GEN_250;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_ras_idx <= _GEN_250;
      end
    end else begin
      ram_0_ras_idx <= _GEN_250;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_0_start_bank <= _GEN_234;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h0 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_0_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_0_start_bank <= _GEN_234;
      end
    end else begin
      ram_0_start_bank <= _GEN_234;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_idx_valid <= _GEN_411;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_idx_valid <= _GEN_411;
      end
    end else begin
      ram_1_cfi_idx_valid <= _GEN_411;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_idx_bits <= _GEN_395;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_idx_bits <= _GEN_395;
      end
    end else begin
      ram_1_cfi_idx_bits <= _GEN_395;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_taken <= _GEN_379;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_taken <= _GEN_379;
      end
    end else begin
      ram_1_cfi_taken <= _GEN_379;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_mispredicted <= _GEN_363;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_mispredicted <= _GEN_363;
      end
    end else begin
      ram_1_cfi_mispredicted <= _GEN_363;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_type <= _GEN_347;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_type <= _GEN_347;
      end
    end else begin
      ram_1_cfi_type <= _GEN_347;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_br_mask <= _GEN_331;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_br_mask <= _GEN_331;
      end
    end else begin
      ram_1_br_mask <= _GEN_331;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_is_call <= _GEN_315;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_is_call <= _GEN_315;
      end
    end else begin
      ram_1_cfi_is_call <= _GEN_315;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_is_ret <= _GEN_299;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_is_ret <= _GEN_299;
      end
    end else begin
      ram_1_cfi_is_ret <= _GEN_299;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_cfi_npc_plus4 <= _GEN_283;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_cfi_npc_plus4 <= _GEN_283;
      end
    end else begin
      ram_1_cfi_npc_plus4 <= _GEN_283;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_ras_top <= _GEN_267;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_ras_top <= _GEN_267;
      end
    end else begin
      ram_1_ras_top <= _GEN_267;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_ras_idx <= _GEN_251;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_ras_idx <= _GEN_251;
      end
    end else begin
      ram_1_ras_idx <= _GEN_251;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_1_start_bank <= _GEN_235;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h1 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_1_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_1_start_bank <= _GEN_235;
      end
    end else begin
      ram_1_start_bank <= _GEN_235;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_idx_valid <= _GEN_412;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_idx_valid <= _GEN_412;
      end
    end else begin
      ram_2_cfi_idx_valid <= _GEN_412;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_idx_bits <= _GEN_396;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_idx_bits <= _GEN_396;
      end
    end else begin
      ram_2_cfi_idx_bits <= _GEN_396;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_taken <= _GEN_380;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_taken <= _GEN_380;
      end
    end else begin
      ram_2_cfi_taken <= _GEN_380;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_mispredicted <= _GEN_364;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_mispredicted <= _GEN_364;
      end
    end else begin
      ram_2_cfi_mispredicted <= _GEN_364;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_type <= _GEN_348;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_type <= _GEN_348;
      end
    end else begin
      ram_2_cfi_type <= _GEN_348;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_br_mask <= _GEN_332;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_br_mask <= _GEN_332;
      end
    end else begin
      ram_2_br_mask <= _GEN_332;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_is_call <= _GEN_316;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_is_call <= _GEN_316;
      end
    end else begin
      ram_2_cfi_is_call <= _GEN_316;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_is_ret <= _GEN_300;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_is_ret <= _GEN_300;
      end
    end else begin
      ram_2_cfi_is_ret <= _GEN_300;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_cfi_npc_plus4 <= _GEN_284;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_cfi_npc_plus4 <= _GEN_284;
      end
    end else begin
      ram_2_cfi_npc_plus4 <= _GEN_284;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_ras_top <= _GEN_268;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_ras_top <= _GEN_268;
      end
    end else begin
      ram_2_ras_top <= _GEN_268;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_ras_idx <= _GEN_252;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_ras_idx <= _GEN_252;
      end
    end else begin
      ram_2_ras_idx <= _GEN_252;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_2_start_bank <= _GEN_236;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h2 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_2_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_2_start_bank <= _GEN_236;
      end
    end else begin
      ram_2_start_bank <= _GEN_236;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_idx_valid <= _GEN_413;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_idx_valid <= _GEN_413;
      end
    end else begin
      ram_3_cfi_idx_valid <= _GEN_413;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_idx_bits <= _GEN_397;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_idx_bits <= _GEN_397;
      end
    end else begin
      ram_3_cfi_idx_bits <= _GEN_397;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_taken <= _GEN_381;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_taken <= _GEN_381;
      end
    end else begin
      ram_3_cfi_taken <= _GEN_381;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_mispredicted <= _GEN_365;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_mispredicted <= _GEN_365;
      end
    end else begin
      ram_3_cfi_mispredicted <= _GEN_365;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_type <= _GEN_349;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_type <= _GEN_349;
      end
    end else begin
      ram_3_cfi_type <= _GEN_349;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_br_mask <= _GEN_333;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_br_mask <= _GEN_333;
      end
    end else begin
      ram_3_br_mask <= _GEN_333;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_is_call <= _GEN_317;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_is_call <= _GEN_317;
      end
    end else begin
      ram_3_cfi_is_call <= _GEN_317;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_is_ret <= _GEN_301;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_is_ret <= _GEN_301;
      end
    end else begin
      ram_3_cfi_is_ret <= _GEN_301;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_cfi_npc_plus4 <= _GEN_285;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_cfi_npc_plus4 <= _GEN_285;
      end
    end else begin
      ram_3_cfi_npc_plus4 <= _GEN_285;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_ras_top <= _GEN_269;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_ras_top <= _GEN_269;
      end
    end else begin
      ram_3_ras_top <= _GEN_269;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_ras_idx <= _GEN_253;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_ras_idx <= _GEN_253;
      end
    end else begin
      ram_3_ras_idx <= _GEN_253;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_3_start_bank <= _GEN_237;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h3 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_3_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_3_start_bank <= _GEN_237;
      end
    end else begin
      ram_3_start_bank <= _GEN_237;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_idx_valid <= _GEN_414;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_idx_valid <= _GEN_414;
      end
    end else begin
      ram_4_cfi_idx_valid <= _GEN_414;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_idx_bits <= _GEN_398;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_idx_bits <= _GEN_398;
      end
    end else begin
      ram_4_cfi_idx_bits <= _GEN_398;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_taken <= _GEN_382;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_taken <= _GEN_382;
      end
    end else begin
      ram_4_cfi_taken <= _GEN_382;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_mispredicted <= _GEN_366;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_mispredicted <= _GEN_366;
      end
    end else begin
      ram_4_cfi_mispredicted <= _GEN_366;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_type <= _GEN_350;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_type <= _GEN_350;
      end
    end else begin
      ram_4_cfi_type <= _GEN_350;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_br_mask <= _GEN_334;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_br_mask <= _GEN_334;
      end
    end else begin
      ram_4_br_mask <= _GEN_334;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_is_call <= _GEN_318;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_is_call <= _GEN_318;
      end
    end else begin
      ram_4_cfi_is_call <= _GEN_318;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_is_ret <= _GEN_302;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_is_ret <= _GEN_302;
      end
    end else begin
      ram_4_cfi_is_ret <= _GEN_302;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_cfi_npc_plus4 <= _GEN_286;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_cfi_npc_plus4 <= _GEN_286;
      end
    end else begin
      ram_4_cfi_npc_plus4 <= _GEN_286;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_ras_top <= _GEN_270;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_ras_top <= _GEN_270;
      end
    end else begin
      ram_4_ras_top <= _GEN_270;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_ras_idx <= _GEN_254;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_ras_idx <= _GEN_254;
      end
    end else begin
      ram_4_ras_idx <= _GEN_254;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_4_start_bank <= _GEN_238;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h4 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_4_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_4_start_bank <= _GEN_238;
      end
    end else begin
      ram_4_start_bank <= _GEN_238;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_idx_valid <= _GEN_415;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_idx_valid <= _GEN_415;
      end
    end else begin
      ram_5_cfi_idx_valid <= _GEN_415;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_idx_bits <= _GEN_399;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_idx_bits <= _GEN_399;
      end
    end else begin
      ram_5_cfi_idx_bits <= _GEN_399;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_taken <= _GEN_383;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_taken <= _GEN_383;
      end
    end else begin
      ram_5_cfi_taken <= _GEN_383;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_mispredicted <= _GEN_367;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_mispredicted <= _GEN_367;
      end
    end else begin
      ram_5_cfi_mispredicted <= _GEN_367;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_type <= _GEN_351;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_type <= _GEN_351;
      end
    end else begin
      ram_5_cfi_type <= _GEN_351;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_br_mask <= _GEN_335;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_br_mask <= _GEN_335;
      end
    end else begin
      ram_5_br_mask <= _GEN_335;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_is_call <= _GEN_319;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_is_call <= _GEN_319;
      end
    end else begin
      ram_5_cfi_is_call <= _GEN_319;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_is_ret <= _GEN_303;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_is_ret <= _GEN_303;
      end
    end else begin
      ram_5_cfi_is_ret <= _GEN_303;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_cfi_npc_plus4 <= _GEN_287;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_cfi_npc_plus4 <= _GEN_287;
      end
    end else begin
      ram_5_cfi_npc_plus4 <= _GEN_287;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_ras_top <= _GEN_271;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_ras_top <= _GEN_271;
      end
    end else begin
      ram_5_ras_top <= _GEN_271;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_ras_idx <= _GEN_255;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_ras_idx <= _GEN_255;
      end
    end else begin
      ram_5_ras_idx <= _GEN_255;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_5_start_bank <= _GEN_239;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h5 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_5_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_5_start_bank <= _GEN_239;
      end
    end else begin
      ram_5_start_bank <= _GEN_239;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_idx_valid <= _GEN_416;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_idx_valid <= _GEN_416;
      end
    end else begin
      ram_6_cfi_idx_valid <= _GEN_416;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_idx_bits <= _GEN_400;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_idx_bits <= _GEN_400;
      end
    end else begin
      ram_6_cfi_idx_bits <= _GEN_400;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_taken <= _GEN_384;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_taken <= _GEN_384;
      end
    end else begin
      ram_6_cfi_taken <= _GEN_384;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_mispredicted <= _GEN_368;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_mispredicted <= _GEN_368;
      end
    end else begin
      ram_6_cfi_mispredicted <= _GEN_368;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_type <= _GEN_352;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_type <= _GEN_352;
      end
    end else begin
      ram_6_cfi_type <= _GEN_352;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_br_mask <= _GEN_336;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_br_mask <= _GEN_336;
      end
    end else begin
      ram_6_br_mask <= _GEN_336;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_is_call <= _GEN_320;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_is_call <= _GEN_320;
      end
    end else begin
      ram_6_cfi_is_call <= _GEN_320;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_is_ret <= _GEN_304;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_is_ret <= _GEN_304;
      end
    end else begin
      ram_6_cfi_is_ret <= _GEN_304;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_cfi_npc_plus4 <= _GEN_288;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_cfi_npc_plus4 <= _GEN_288;
      end
    end else begin
      ram_6_cfi_npc_plus4 <= _GEN_288;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_ras_top <= _GEN_272;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_ras_top <= _GEN_272;
      end
    end else begin
      ram_6_ras_top <= _GEN_272;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_ras_idx <= _GEN_256;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_ras_idx <= _GEN_256;
      end
    end else begin
      ram_6_ras_idx <= _GEN_256;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_6_start_bank <= _GEN_240;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h6 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_6_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_6_start_bank <= _GEN_240;
      end
    end else begin
      ram_6_start_bank <= _GEN_240;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_idx_valid <= _GEN_417;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_idx_valid <= _GEN_417;
      end
    end else begin
      ram_7_cfi_idx_valid <= _GEN_417;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_idx_bits <= _GEN_401;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_idx_bits <= _GEN_401;
      end
    end else begin
      ram_7_cfi_idx_bits <= _GEN_401;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_taken <= _GEN_385;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_taken <= _GEN_385;
      end
    end else begin
      ram_7_cfi_taken <= _GEN_385;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_mispredicted <= _GEN_369;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_mispredicted <= _GEN_369;
      end
    end else begin
      ram_7_cfi_mispredicted <= _GEN_369;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_type <= _GEN_353;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_type <= _GEN_353;
      end
    end else begin
      ram_7_cfi_type <= _GEN_353;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_br_mask <= _GEN_337;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_br_mask <= _GEN_337;
      end
    end else begin
      ram_7_br_mask <= _GEN_337;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_is_call <= _GEN_321;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_is_call <= _GEN_321;
      end
    end else begin
      ram_7_cfi_is_call <= _GEN_321;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_is_ret <= _GEN_305;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_is_ret <= _GEN_305;
      end
    end else begin
      ram_7_cfi_is_ret <= _GEN_305;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_cfi_npc_plus4 <= _GEN_289;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_cfi_npc_plus4 <= _GEN_289;
      end
    end else begin
      ram_7_cfi_npc_plus4 <= _GEN_289;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_ras_top <= _GEN_273;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_ras_top <= _GEN_273;
      end
    end else begin
      ram_7_ras_top <= _GEN_273;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_ras_idx <= _GEN_257;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_ras_idx <= _GEN_257;
      end
    end else begin
      ram_7_ras_idx <= _GEN_257;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_7_start_bank <= _GEN_241;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h7 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_7_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_7_start_bank <= _GEN_241;
      end
    end else begin
      ram_7_start_bank <= _GEN_241;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_idx_valid <= _GEN_418;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_idx_valid <= _GEN_418;
      end
    end else begin
      ram_8_cfi_idx_valid <= _GEN_418;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_idx_bits <= _GEN_402;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_idx_bits <= _GEN_402;
      end
    end else begin
      ram_8_cfi_idx_bits <= _GEN_402;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_taken <= _GEN_386;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_taken <= _GEN_386;
      end
    end else begin
      ram_8_cfi_taken <= _GEN_386;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_mispredicted <= _GEN_370;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_mispredicted <= _GEN_370;
      end
    end else begin
      ram_8_cfi_mispredicted <= _GEN_370;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_type <= _GEN_354;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_type <= _GEN_354;
      end
    end else begin
      ram_8_cfi_type <= _GEN_354;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_br_mask <= _GEN_338;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_br_mask <= _GEN_338;
      end
    end else begin
      ram_8_br_mask <= _GEN_338;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_is_call <= _GEN_322;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_is_call <= _GEN_322;
      end
    end else begin
      ram_8_cfi_is_call <= _GEN_322;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_is_ret <= _GEN_306;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_is_ret <= _GEN_306;
      end
    end else begin
      ram_8_cfi_is_ret <= _GEN_306;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_cfi_npc_plus4 <= _GEN_290;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_cfi_npc_plus4 <= _GEN_290;
      end
    end else begin
      ram_8_cfi_npc_plus4 <= _GEN_290;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_ras_top <= _GEN_274;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_ras_top <= _GEN_274;
      end
    end else begin
      ram_8_ras_top <= _GEN_274;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_ras_idx <= _GEN_258;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_ras_idx <= _GEN_258;
      end
    end else begin
      ram_8_ras_idx <= _GEN_258;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_8_start_bank <= _GEN_242;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h8 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_8_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_8_start_bank <= _GEN_242;
      end
    end else begin
      ram_8_start_bank <= _GEN_242;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_idx_valid <= _GEN_419;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_idx_valid <= _GEN_419;
      end
    end else begin
      ram_9_cfi_idx_valid <= _GEN_419;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_idx_bits <= _GEN_403;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_idx_bits <= _GEN_403;
      end
    end else begin
      ram_9_cfi_idx_bits <= _GEN_403;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_taken <= _GEN_387;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_taken <= _GEN_387;
      end
    end else begin
      ram_9_cfi_taken <= _GEN_387;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_mispredicted <= _GEN_371;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_mispredicted <= _GEN_371;
      end
    end else begin
      ram_9_cfi_mispredicted <= _GEN_371;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_type <= _GEN_355;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_type <= _GEN_355;
      end
    end else begin
      ram_9_cfi_type <= _GEN_355;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_br_mask <= _GEN_339;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_br_mask <= _GEN_339;
      end
    end else begin
      ram_9_br_mask <= _GEN_339;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_is_call <= _GEN_323;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_is_call <= _GEN_323;
      end
    end else begin
      ram_9_cfi_is_call <= _GEN_323;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_is_ret <= _GEN_307;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_is_ret <= _GEN_307;
      end
    end else begin
      ram_9_cfi_is_ret <= _GEN_307;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_cfi_npc_plus4 <= _GEN_291;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_cfi_npc_plus4 <= _GEN_291;
      end
    end else begin
      ram_9_cfi_npc_plus4 <= _GEN_291;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_ras_top <= _GEN_275;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_ras_top <= _GEN_275;
      end
    end else begin
      ram_9_ras_top <= _GEN_275;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_ras_idx <= _GEN_259;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_ras_idx <= _GEN_259;
      end
    end else begin
      ram_9_ras_idx <= _GEN_259;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_9_start_bank <= _GEN_243;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'h9 == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_9_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_9_start_bank <= _GEN_243;
      end
    end else begin
      ram_9_start_bank <= _GEN_243;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_idx_valid <= _GEN_420;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_idx_valid <= _GEN_420;
      end
    end else begin
      ram_10_cfi_idx_valid <= _GEN_420;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_idx_bits <= _GEN_404;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_idx_bits <= _GEN_404;
      end
    end else begin
      ram_10_cfi_idx_bits <= _GEN_404;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_taken <= _GEN_388;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_taken <= _GEN_388;
      end
    end else begin
      ram_10_cfi_taken <= _GEN_388;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_mispredicted <= _GEN_372;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_mispredicted <= _GEN_372;
      end
    end else begin
      ram_10_cfi_mispredicted <= _GEN_372;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_type <= _GEN_356;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_type <= _GEN_356;
      end
    end else begin
      ram_10_cfi_type <= _GEN_356;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_br_mask <= _GEN_340;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_br_mask <= _GEN_340;
      end
    end else begin
      ram_10_br_mask <= _GEN_340;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_is_call <= _GEN_324;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_is_call <= _GEN_324;
      end
    end else begin
      ram_10_cfi_is_call <= _GEN_324;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_is_ret <= _GEN_308;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_is_ret <= _GEN_308;
      end
    end else begin
      ram_10_cfi_is_ret <= _GEN_308;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_cfi_npc_plus4 <= _GEN_292;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_cfi_npc_plus4 <= _GEN_292;
      end
    end else begin
      ram_10_cfi_npc_plus4 <= _GEN_292;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_ras_top <= _GEN_276;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_ras_top <= _GEN_276;
      end
    end else begin
      ram_10_ras_top <= _GEN_276;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_ras_idx <= _GEN_260;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_ras_idx <= _GEN_260;
      end
    end else begin
      ram_10_ras_idx <= _GEN_260;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_10_start_bank <= _GEN_244;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'ha == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_10_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_10_start_bank <= _GEN_244;
      end
    end else begin
      ram_10_start_bank <= _GEN_244;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_idx_valid <= _GEN_421;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_idx_valid <= _GEN_421;
      end
    end else begin
      ram_11_cfi_idx_valid <= _GEN_421;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_idx_bits <= _GEN_405;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_idx_bits <= _GEN_405;
      end
    end else begin
      ram_11_cfi_idx_bits <= _GEN_405;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_taken <= _GEN_389;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_taken <= _GEN_389;
      end
    end else begin
      ram_11_cfi_taken <= _GEN_389;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_mispredicted <= _GEN_373;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_mispredicted <= _GEN_373;
      end
    end else begin
      ram_11_cfi_mispredicted <= _GEN_373;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_type <= _GEN_357;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_type <= _GEN_357;
      end
    end else begin
      ram_11_cfi_type <= _GEN_357;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_br_mask <= _GEN_341;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_br_mask <= _GEN_341;
      end
    end else begin
      ram_11_br_mask <= _GEN_341;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_is_call <= _GEN_325;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_is_call <= _GEN_325;
      end
    end else begin
      ram_11_cfi_is_call <= _GEN_325;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_is_ret <= _GEN_309;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_is_ret <= _GEN_309;
      end
    end else begin
      ram_11_cfi_is_ret <= _GEN_309;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_cfi_npc_plus4 <= _GEN_293;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_cfi_npc_plus4 <= _GEN_293;
      end
    end else begin
      ram_11_cfi_npc_plus4 <= _GEN_293;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_ras_top <= _GEN_277;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_ras_top <= _GEN_277;
      end
    end else begin
      ram_11_ras_top <= _GEN_277;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_ras_idx <= _GEN_261;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_ras_idx <= _GEN_261;
      end
    end else begin
      ram_11_ras_idx <= _GEN_261;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_11_start_bank <= _GEN_245;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hb == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_11_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_11_start_bank <= _GEN_245;
      end
    end else begin
      ram_11_start_bank <= _GEN_245;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_idx_valid <= _GEN_422;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_idx_valid <= _GEN_422;
      end
    end else begin
      ram_12_cfi_idx_valid <= _GEN_422;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_idx_bits <= _GEN_406;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_idx_bits <= _GEN_406;
      end
    end else begin
      ram_12_cfi_idx_bits <= _GEN_406;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_taken <= _GEN_390;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_taken <= _GEN_390;
      end
    end else begin
      ram_12_cfi_taken <= _GEN_390;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_mispredicted <= _GEN_374;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_mispredicted <= _GEN_374;
      end
    end else begin
      ram_12_cfi_mispredicted <= _GEN_374;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_type <= _GEN_358;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_type <= _GEN_358;
      end
    end else begin
      ram_12_cfi_type <= _GEN_358;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_br_mask <= _GEN_342;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_br_mask <= _GEN_342;
      end
    end else begin
      ram_12_br_mask <= _GEN_342;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_is_call <= _GEN_326;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_is_call <= _GEN_326;
      end
    end else begin
      ram_12_cfi_is_call <= _GEN_326;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_is_ret <= _GEN_310;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_is_ret <= _GEN_310;
      end
    end else begin
      ram_12_cfi_is_ret <= _GEN_310;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_cfi_npc_plus4 <= _GEN_294;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_cfi_npc_plus4 <= _GEN_294;
      end
    end else begin
      ram_12_cfi_npc_plus4 <= _GEN_294;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_ras_top <= _GEN_278;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_ras_top <= _GEN_278;
      end
    end else begin
      ram_12_ras_top <= _GEN_278;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_ras_idx <= _GEN_262;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_ras_idx <= _GEN_262;
      end
    end else begin
      ram_12_ras_idx <= _GEN_262;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_12_start_bank <= _GEN_246;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hc == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_12_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_12_start_bank <= _GEN_246;
      end
    end else begin
      ram_12_start_bank <= _GEN_246;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_idx_valid <= _GEN_423;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_idx_valid <= _GEN_423;
      end
    end else begin
      ram_13_cfi_idx_valid <= _GEN_423;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_idx_bits <= _GEN_407;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_idx_bits <= _GEN_407;
      end
    end else begin
      ram_13_cfi_idx_bits <= _GEN_407;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_taken <= _GEN_391;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_taken <= _GEN_391;
      end
    end else begin
      ram_13_cfi_taken <= _GEN_391;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_mispredicted <= _GEN_375;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_mispredicted <= _GEN_375;
      end
    end else begin
      ram_13_cfi_mispredicted <= _GEN_375;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_type <= _GEN_359;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_type <= _GEN_359;
      end
    end else begin
      ram_13_cfi_type <= _GEN_359;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_br_mask <= _GEN_343;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_br_mask <= _GEN_343;
      end
    end else begin
      ram_13_br_mask <= _GEN_343;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_is_call <= _GEN_327;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_is_call <= _GEN_327;
      end
    end else begin
      ram_13_cfi_is_call <= _GEN_327;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_is_ret <= _GEN_311;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_is_ret <= _GEN_311;
      end
    end else begin
      ram_13_cfi_is_ret <= _GEN_311;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_cfi_npc_plus4 <= _GEN_295;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_cfi_npc_plus4 <= _GEN_295;
      end
    end else begin
      ram_13_cfi_npc_plus4 <= _GEN_295;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_ras_top <= _GEN_279;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_ras_top <= _GEN_279;
      end
    end else begin
      ram_13_ras_top <= _GEN_279;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_ras_idx <= _GEN_263;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_ras_idx <= _GEN_263;
      end
    end else begin
      ram_13_ras_idx <= _GEN_263;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_13_start_bank <= _GEN_247;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hd == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_13_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_13_start_bank <= _GEN_247;
      end
    end else begin
      ram_13_start_bank <= _GEN_247;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_idx_valid <= _GEN_424;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_idx_valid <= _GEN_424;
      end
    end else begin
      ram_14_cfi_idx_valid <= _GEN_424;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_idx_bits <= _GEN_408;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_idx_bits <= _GEN_408;
      end
    end else begin
      ram_14_cfi_idx_bits <= _GEN_408;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_taken <= _GEN_392;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_taken <= _GEN_392;
      end
    end else begin
      ram_14_cfi_taken <= _GEN_392;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_mispredicted <= _GEN_376;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_mispredicted <= _GEN_376;
      end
    end else begin
      ram_14_cfi_mispredicted <= _GEN_376;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_type <= _GEN_360;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_type <= _GEN_360;
      end
    end else begin
      ram_14_cfi_type <= _GEN_360;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_br_mask <= _GEN_344;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_br_mask <= _GEN_344;
      end
    end else begin
      ram_14_br_mask <= _GEN_344;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_is_call <= _GEN_328;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_is_call <= _GEN_328;
      end
    end else begin
      ram_14_cfi_is_call <= _GEN_328;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_is_ret <= _GEN_312;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_is_ret <= _GEN_312;
      end
    end else begin
      ram_14_cfi_is_ret <= _GEN_312;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_cfi_npc_plus4 <= _GEN_296;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_cfi_npc_plus4 <= _GEN_296;
      end
    end else begin
      ram_14_cfi_npc_plus4 <= _GEN_296;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_ras_top <= _GEN_280;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_ras_top <= _GEN_280;
      end
    end else begin
      ram_14_ras_top <= _GEN_280;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_ras_idx <= _GEN_264;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_ras_idx <= _GEN_264;
      end
    end else begin
      ram_14_ras_idx <= _GEN_264;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_14_start_bank <= _GEN_248;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'he == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_14_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_14_start_bank <= _GEN_248;
      end
    end else begin
      ram_14_start_bank <= _GEN_248;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_idx_valid <= _GEN_425;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_idx_valid <= _T_163_cfi_idx_valid; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_idx_valid <= _GEN_425;
      end
    end else begin
      ram_15_cfi_idx_valid <= _GEN_425;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_idx_bits <= _GEN_409;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_idx_bits <= _T_163_cfi_idx_bits; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_idx_bits <= _GEN_409;
      end
    end else begin
      ram_15_cfi_idx_bits <= _GEN_409;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_taken <= _GEN_393;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_taken <= _T_163_cfi_taken; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_taken <= _GEN_393;
      end
    end else begin
      ram_15_cfi_taken <= _GEN_393;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_mispredicted <= _GEN_377;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_mispredicted <= _T_163_cfi_mispredicted; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_mispredicted <= _GEN_377;
      end
    end else begin
      ram_15_cfi_mispredicted <= _GEN_377;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_type <= _GEN_361;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_type <= _T_163_cfi_type; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_type <= _GEN_361;
      end
    end else begin
      ram_15_cfi_type <= _GEN_361;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_br_mask <= _GEN_345;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_br_mask <= _T_163_br_mask; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_br_mask <= _GEN_345;
      end
    end else begin
      ram_15_br_mask <= _GEN_345;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_is_call <= _GEN_329;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_is_call <= _T_163_cfi_is_call; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_is_call <= _GEN_329;
      end
    end else begin
      ram_15_cfi_is_call <= _GEN_329;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_is_ret <= _GEN_313;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_is_ret <= _T_163_cfi_is_ret; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_is_ret <= _GEN_313;
      end
    end else begin
      ram_15_cfi_is_ret <= _GEN_313;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_cfi_npc_plus4 <= _GEN_297;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_cfi_npc_plus4 <= _T_163_cfi_npc_plus4; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_cfi_npc_plus4 <= _GEN_297;
      end
    end else begin
      ram_15_cfi_npc_plus4 <= _GEN_297;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_ras_top <= _GEN_281;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_ras_top <= _T_163_ras_top; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_ras_top <= _GEN_281;
      end
    end else begin
      ram_15_ras_top <= _GEN_281;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_ras_idx <= _GEN_265;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_ras_idx <= _T_163_ras_idx; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_ras_idx <= _GEN_265;
      end
    end else begin
      ram_15_ras_idx <= _GEN_265;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      ram_15_start_bank <= _GEN_249;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      if (4'hf == _T_160) begin // @[fetch-target-queue.scala 337:36]
        ram_15_start_bank <= _T_163_start_bank; // @[fetch-target-queue.scala 337:36]
      end else begin
        ram_15_start_bank <= _GEN_249;
      end
    end else begin
      ram_15_start_bank <= _GEN_249;
    end
    if (reset) begin // @[fetch-target-queue.scala 155:27]
      prev_ghist_old_history <= 64'h0; // @[fetch-target-queue.scala 155:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_ghist_old_history <= _GEN_443;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_ghist_old_history <= ghist_0_old_history_bpd_ghist_data; // @[fetch-target-queue.scala 334:16]
    end else begin
      prev_ghist_old_history <= _GEN_443;
    end
    if (reset) begin // @[fetch-target-queue.scala 155:27]
      prev_ghist_current_saw_branch_not_taken <= 1'h0; // @[fetch-target-queue.scala 155:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_ghist_current_saw_branch_not_taken <= _GEN_442;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_ghist_current_saw_branch_not_taken <= ghist_0_current_saw_branch_not_taken_bpd_ghist_data; // @[fetch-target-queue.scala 334:16]
    end else begin
      prev_ghist_current_saw_branch_not_taken <= _GEN_442;
    end
    if (reset) begin // @[fetch-target-queue.scala 155:27]
      prev_ghist_ras_idx <= 5'h0; // @[fetch-target-queue.scala 155:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_ghist_ras_idx <= _GEN_439;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_ghist_ras_idx <= ghist_0_ras_idx_bpd_ghist_data; // @[fetch-target-queue.scala 334:16]
    end else begin
      prev_ghist_ras_idx <= _GEN_439;
    end
    if (reset) begin // @[fetch-target-queue.scala 156:27]
      prev_entry_cfi_idx_valid <= 1'h0; // @[fetch-target-queue.scala 156:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_entry_cfi_idx_valid <= _GEN_438;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_entry_cfi_idx_valid <= _T_159_cfi_idx_valid; // @[fetch-target-queue.scala 333:16]
    end else begin
      prev_entry_cfi_idx_valid <= _GEN_438;
    end
    if (reset) begin // @[fetch-target-queue.scala 156:27]
      prev_entry_cfi_idx_bits <= 2'h0; // @[fetch-target-queue.scala 156:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_entry_cfi_idx_bits <= _GEN_437;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_entry_cfi_idx_bits <= _T_159_cfi_idx_bits; // @[fetch-target-queue.scala 333:16]
    end else begin
      prev_entry_cfi_idx_bits <= _GEN_437;
    end
    if (reset) begin // @[fetch-target-queue.scala 156:27]
      prev_entry_cfi_taken <= 1'h0; // @[fetch-target-queue.scala 156:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_entry_cfi_taken <= _GEN_436;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_entry_cfi_taken <= _T_159_cfi_taken; // @[fetch-target-queue.scala 333:16]
    end else begin
      prev_entry_cfi_taken <= _GEN_436;
    end
    if (reset) begin // @[fetch-target-queue.scala 156:27]
      prev_entry_br_mask <= 4'h0; // @[fetch-target-queue.scala 156:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_entry_br_mask <= _GEN_433;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_entry_br_mask <= _T_159_br_mask; // @[fetch-target-queue.scala 333:16]
    end else begin
      prev_entry_br_mask <= _GEN_433;
    end
    if (reset) begin // @[fetch-target-queue.scala 156:27]
      prev_entry_cfi_is_call <= 1'h0; // @[fetch-target-queue.scala 156:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_entry_cfi_is_call <= _GEN_432;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_entry_cfi_is_call <= _T_159_cfi_is_call; // @[fetch-target-queue.scala 333:16]
    end else begin
      prev_entry_cfi_is_call <= _GEN_432;
    end
    if (reset) begin // @[fetch-target-queue.scala 156:27]
      prev_entry_cfi_is_ret <= 1'h0; // @[fetch-target-queue.scala 156:27]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      prev_entry_cfi_is_ret <= _GEN_431;
    end else if (_T_158) begin // @[fetch-target-queue.scala 332:44]
      prev_entry_cfi_is_ret <= _T_159_cfi_is_ret; // @[fetch-target-queue.scala 333:16]
    end else begin
      prev_entry_cfi_is_ret <= _GEN_431;
    end
    first_empty <= reset | _GEN_715; // @[fetch-target-queue.scala 214:{28,28}]
    _T_60 <= io_redirect_valid; // @[fetch-target-queue.scala 314:28 328:20]
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (4'hf == io_redirect_bits) begin
        _T_61 <= ram_15_ras_top;
      end else if (4'he == io_redirect_bits) begin
        _T_61 <= ram_14_ras_top;
      end else if (4'hd == io_redirect_bits) begin
        _T_61 <= ram_13_ras_top;
      end else begin
        _T_61 <= _GEN_761;
      end
    end else begin
      _T_61 <= 40'h0;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (4'hf == io_redirect_bits) begin
        _T_62 <= ram_15_ras_idx;
      end else if (4'he == io_redirect_bits) begin
        _T_62 <= ram_14_ras_idx;
      end else if (4'hd == io_redirect_bits) begin
        _T_62 <= ram_13_ras_idx;
      end else begin
        _T_62 <= _GEN_745;
      end
    end else begin
      _T_62 <= 5'h0;
    end
    if (reset) begin // @[fetch-target-queue.scala 226:38]
      bpd_update_mispredict <= 1'h0; // @[fetch-target-queue.scala 226:38]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 245:28]
      bpd_update_mispredict <= 1'h0; // @[fetch-target-queue.scala 246:27]
    end else begin
      bpd_update_mispredict <= _GEN_685;
    end
    if (reset) begin // @[fetch-target-queue.scala 227:34]
      bpd_update_repair <= 1'h0; // @[fetch-target-queue.scala 227:34]
    end else if (io_redirect_valid) begin // @[fetch-target-queue.scala 245:28]
      bpd_update_repair <= 1'h0; // @[fetch-target-queue.scala 247:27]
    end else if (!(_T_74)) begin // @[fetch-target-queue.scala 248:52]
      bpd_update_repair <= _GEN_682;
    end
    if (!(io_redirect_valid)) begin // @[fetch-target-queue.scala 245:28]
      if (_T_74) begin // @[fetch-target-queue.scala 248:52]
        bpd_repair_idx <= _T_75; // @[fetch-target-queue.scala 250:27]
      end else if (bpd_update_mispredict) begin // @[fetch-target-queue.scala 252:39]
        bpd_repair_idx <= _T_78; // @[fetch-target-queue.scala 255:27]
      end else if (bpd_update_repair & _T_80) begin // @[fetch-target-queue.scala 256:69]
        bpd_repair_idx <= _T_78; // @[fetch-target-queue.scala 258:27]
      end else begin
        bpd_repair_idx <= _GEN_676;
      end
    end
    if (!(io_redirect_valid)) begin // @[fetch-target-queue.scala 245:28]
      if (_T_74) begin // @[fetch-target-queue.scala 248:52]
        bpd_end_idx <= _T_76; // @[fetch-target-queue.scala 251:27]
      end
    end
    if (!(io_redirect_valid)) begin // @[fetch-target-queue.scala 245:28]
      if (!(_T_74)) begin // @[fetch-target-queue.scala 248:52]
        if (!(bpd_update_mispredict)) begin // @[fetch-target-queue.scala 252:39]
          if (bpd_update_repair & _T_80) begin // @[fetch-target-queue.scala 256:69]
            bpd_repair_pc <= bpd_pc; // @[fetch-target-queue.scala 257:27]
          end
        end
      end
    end
    if (4'hf == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_valid <= ram_15_cfi_idx_valid; // @[fetch-target-queue.scala 234:26]
    end else if (4'he == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_valid <= ram_14_cfi_idx_valid; // @[fetch-target-queue.scala 234:26]
    end else if (4'hd == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_valid <= ram_13_cfi_idx_valid; // @[fetch-target-queue.scala 234:26]
    end else if (4'hc == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_valid <= ram_12_cfi_idx_valid; // @[fetch-target-queue.scala 234:26]
    end else begin
      bpd_entry_cfi_idx_valid <= _GEN_633;
    end
    if (4'hf == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_bits <= ram_15_cfi_idx_bits; // @[fetch-target-queue.scala 234:26]
    end else if (4'he == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_bits <= ram_14_cfi_idx_bits; // @[fetch-target-queue.scala 234:26]
    end else if (4'hd == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_bits <= ram_13_cfi_idx_bits; // @[fetch-target-queue.scala 234:26]
    end else if (4'hc == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_idx_bits <= ram_12_cfi_idx_bits; // @[fetch-target-queue.scala 234:26]
    end else begin
      bpd_entry_cfi_idx_bits <= _GEN_617;
    end
    if (4'hf == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_taken <= ram_15_cfi_taken; // @[fetch-target-queue.scala 234:26]
    end else if (4'he == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_taken <= ram_14_cfi_taken; // @[fetch-target-queue.scala 234:26]
    end else if (4'hd == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_taken <= ram_13_cfi_taken; // @[fetch-target-queue.scala 234:26]
    end else if (4'hc == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_taken <= ram_12_cfi_taken; // @[fetch-target-queue.scala 234:26]
    end else begin
      bpd_entry_cfi_taken <= _GEN_601;
    end
    if (4'hf == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_mispredicted <= ram_15_cfi_mispredicted; // @[fetch-target-queue.scala 234:26]
    end else if (4'he == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_mispredicted <= ram_14_cfi_mispredicted; // @[fetch-target-queue.scala 234:26]
    end else if (4'hd == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_mispredicted <= ram_13_cfi_mispredicted; // @[fetch-target-queue.scala 234:26]
    end else if (4'hc == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_mispredicted <= ram_12_cfi_mispredicted; // @[fetch-target-queue.scala 234:26]
    end else begin
      bpd_entry_cfi_mispredicted <= _GEN_585;
    end
    if (4'hf == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_type <= ram_15_cfi_type; // @[fetch-target-queue.scala 234:26]
    end else if (4'he == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_type <= ram_14_cfi_type; // @[fetch-target-queue.scala 234:26]
    end else if (4'hd == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_type <= ram_13_cfi_type; // @[fetch-target-queue.scala 234:26]
    end else if (4'hc == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_cfi_type <= ram_12_cfi_type; // @[fetch-target-queue.scala 234:26]
    end else begin
      bpd_entry_cfi_type <= _GEN_569;
    end
    if (4'hf == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_br_mask <= ram_15_br_mask; // @[fetch-target-queue.scala 234:26]
    end else if (4'he == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_br_mask <= ram_14_br_mask; // @[fetch-target-queue.scala 234:26]
    end else if (4'hd == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_br_mask <= ram_13_br_mask; // @[fetch-target-queue.scala 234:26]
    end else if (4'hc == bpd_idx) begin // @[fetch-target-queue.scala 234:26]
      bpd_entry_br_mask <= ram_12_br_mask; // @[fetch-target-queue.scala 234:26]
    end else begin
      bpd_entry_br_mask <= _GEN_553;
    end
    if (4'hf == bpd_idx) begin // @[fetch-target-queue.scala 242:26]
      bpd_pc <= pcs_15; // @[fetch-target-queue.scala 242:26]
    end else if (4'he == bpd_idx) begin // @[fetch-target-queue.scala 242:26]
      bpd_pc <= pcs_14; // @[fetch-target-queue.scala 242:26]
    end else if (4'hd == bpd_idx) begin // @[fetch-target-queue.scala 242:26]
      bpd_pc <= pcs_13; // @[fetch-target-queue.scala 242:26]
    end else if (4'hc == bpd_idx) begin // @[fetch-target-queue.scala 242:26]
      bpd_pc <= pcs_12; // @[fetch-target-queue.scala 242:26]
    end else begin
      bpd_pc <= _GEN_654;
    end
    if (4'hf == _T_72) begin // @[fetch-target-queue.scala 243:27]
      bpd_target <= pcs_15; // @[fetch-target-queue.scala 243:27]
    end else if (4'he == _T_72) begin // @[fetch-target-queue.scala 243:27]
      bpd_target <= pcs_14; // @[fetch-target-queue.scala 243:27]
    end else if (4'hd == _T_72) begin // @[fetch-target-queue.scala 243:27]
      bpd_target <= pcs_13; // @[fetch-target-queue.scala 243:27]
    end else if (4'hc == _T_72) begin // @[fetch-target-queue.scala 243:27]
      bpd_target <= pcs_12; // @[fetch-target-queue.scala 243:27]
    end else begin
      bpd_target <= _GEN_670;
    end
    _T_74 <= io_brupdate_b2_mispredict; // @[fetch-target-queue.scala 248:23]
    _T_75 <= io_brupdate_b2_uop_ftq_idx; // @[fetch-target-queue.scala 250:37]
    _T_76 <= enq_ptr; // @[fetch-target-queue.scala 251:37]
    _T_80 <= bpd_update_mispredict; // @[fetch-target-queue.scala 256:44]
    _T_108 <= io_redirect_valid; // @[fetch-target-queue.scala 274:61]
    _T_112 <= do_commit_update | bpd_update_repair | bpd_update_mispredict; // @[fetch-target-queue.scala 278:54]
    _T_118 <= bpd_update_repair; // @[fetch-target-queue.scala 284:37]
    _T_123 <= bpd_update_mispredict; // @[fetch-target-queue.scala 285:54]
    _T_124 <= bpd_update_repair; // @[fetch-target-queue.scala 286:54]
    _T_145 <= ~full | do_commit_update; // @[fetch-target-queue.scala 308:33]
    _T_158 <= io_redirect_valid; // @[fetch-target-queue.scala 332:23]
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      _T_159_cfi_idx_valid <= _GEN_909;
    end else if (4'hf == io_redirect_bits) begin
      _T_159_cfi_idx_valid <= ram_15_cfi_idx_valid;
    end else if (4'he == io_redirect_bits) begin
      _T_159_cfi_idx_valid <= ram_14_cfi_idx_valid;
    end else if (4'hd == io_redirect_bits) begin
      _T_159_cfi_idx_valid <= ram_13_cfi_idx_valid;
    end else begin
      _T_159_cfi_idx_valid <= _GEN_905;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_159_cfi_idx_bits <= _T_152[2:1]; // @[fetch-target-queue.scala 321:43]
      end else begin
        _T_159_cfi_idx_bits <= _GEN_892;
      end
    end else begin
      _T_159_cfi_idx_bits <= _GEN_892;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_159_cfi_taken <= io_brupdate_b2_taken; // @[fetch-target-queue.scala 323:43]
      end else begin
        _T_159_cfi_taken <= _GEN_876;
      end
    end else begin
      _T_159_cfi_taken <= _GEN_876;
    end
    if (4'hf == io_redirect_bits) begin
      _T_159_br_mask <= ram_15_br_mask;
    end else if (4'he == io_redirect_bits) begin
      _T_159_br_mask <= ram_14_br_mask;
    end else if (4'hd == io_redirect_bits) begin
      _T_159_br_mask <= ram_13_br_mask;
    end else if (4'hc == io_redirect_bits) begin
      _T_159_br_mask <= ram_12_br_mask;
    end else begin
      _T_159_br_mask <= _GEN_824;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_159_cfi_is_call <= _GEN_812 & _GEN_892 == _T_152[2:1]; // @[fetch-target-queue.scala 324:43]
      end else begin
        _T_159_cfi_is_call <= _GEN_812;
      end
    end else begin
      _T_159_cfi_is_call <= _GEN_812;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_159_cfi_is_ret <= _GEN_796 & _T_154; // @[fetch-target-queue.scala 325:43]
      end else begin
        _T_159_cfi_is_ret <= _GEN_796;
      end
    end else begin
      _T_159_cfi_is_ret <= _GEN_796;
    end
    _T_160 <= io_redirect_bits; // @[fetch-target-queue.scala 337:16]
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      _T_163_cfi_idx_valid <= _GEN_909;
    end else if (4'hf == io_redirect_bits) begin
      _T_163_cfi_idx_valid <= ram_15_cfi_idx_valid;
    end else if (4'he == io_redirect_bits) begin
      _T_163_cfi_idx_valid <= ram_14_cfi_idx_valid;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_cfi_idx_valid <= ram_13_cfi_idx_valid;
    end else begin
      _T_163_cfi_idx_valid <= _GEN_905;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_163_cfi_idx_bits <= _T_152[2:1]; // @[fetch-target-queue.scala 321:43]
      end else begin
        _T_163_cfi_idx_bits <= _GEN_892;
      end
    end else begin
      _T_163_cfi_idx_bits <= _GEN_892;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_163_cfi_taken <= io_brupdate_b2_taken; // @[fetch-target-queue.scala 323:43]
      end else begin
        _T_163_cfi_taken <= _GEN_876;
      end
    end else begin
      _T_163_cfi_taken <= _GEN_876;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      _T_163_cfi_mispredicted <= _GEN_911;
    end else if (4'hf == io_redirect_bits) begin
      _T_163_cfi_mispredicted <= ram_15_cfi_mispredicted;
    end else if (4'he == io_redirect_bits) begin
      _T_163_cfi_mispredicted <= ram_14_cfi_mispredicted;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_cfi_mispredicted <= ram_13_cfi_mispredicted;
    end else begin
      _T_163_cfi_mispredicted <= _GEN_857;
    end
    if (4'hf == io_redirect_bits) begin
      _T_163_cfi_type <= ram_15_cfi_type;
    end else if (4'he == io_redirect_bits) begin
      _T_163_cfi_type <= ram_14_cfi_type;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_cfi_type <= ram_13_cfi_type;
    end else if (4'hc == io_redirect_bits) begin
      _T_163_cfi_type <= ram_12_cfi_type;
    end else begin
      _T_163_cfi_type <= _GEN_840;
    end
    if (4'hf == io_redirect_bits) begin
      _T_163_br_mask <= ram_15_br_mask;
    end else if (4'he == io_redirect_bits) begin
      _T_163_br_mask <= ram_14_br_mask;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_br_mask <= ram_13_br_mask;
    end else if (4'hc == io_redirect_bits) begin
      _T_163_br_mask <= ram_12_br_mask;
    end else begin
      _T_163_br_mask <= _GEN_824;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_163_cfi_is_call <= _GEN_812 & _GEN_892 == _T_152[2:1]; // @[fetch-target-queue.scala 324:43]
      end else begin
        _T_163_cfi_is_call <= _GEN_812;
      end
    end else begin
      _T_163_cfi_is_call <= _GEN_812;
    end
    if (io_redirect_valid) begin // @[fetch-target-queue.scala 314:28]
      if (io_brupdate_b2_mispredict) begin // @[fetch-target-queue.scala 317:38]
        _T_163_cfi_is_ret <= _GEN_796 & _T_154; // @[fetch-target-queue.scala 325:43]
      end else begin
        _T_163_cfi_is_ret <= _GEN_796;
      end
    end else begin
      _T_163_cfi_is_ret <= _GEN_796;
    end
    if (4'hf == io_redirect_bits) begin
      _T_163_cfi_npc_plus4 <= ram_15_cfi_npc_plus4;
    end else if (4'he == io_redirect_bits) begin
      _T_163_cfi_npc_plus4 <= ram_14_cfi_npc_plus4;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_cfi_npc_plus4 <= ram_13_cfi_npc_plus4;
    end else if (4'hc == io_redirect_bits) begin
      _T_163_cfi_npc_plus4 <= ram_12_cfi_npc_plus4;
    end else begin
      _T_163_cfi_npc_plus4 <= _GEN_776;
    end
    if (4'hf == io_redirect_bits) begin
      _T_163_ras_top <= ram_15_ras_top;
    end else if (4'he == io_redirect_bits) begin
      _T_163_ras_top <= ram_14_ras_top;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_ras_top <= ram_13_ras_top;
    end else if (4'hc == io_redirect_bits) begin
      _T_163_ras_top <= ram_12_ras_top;
    end else begin
      _T_163_ras_top <= _GEN_760;
    end
    if (4'hf == io_redirect_bits) begin
      _T_163_ras_idx <= ram_15_ras_idx;
    end else if (4'he == io_redirect_bits) begin
      _T_163_ras_idx <= ram_14_ras_idx;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_ras_idx <= ram_13_ras_idx;
    end else if (4'hc == io_redirect_bits) begin
      _T_163_ras_idx <= ram_12_ras_idx;
    end else begin
      _T_163_ras_idx <= _GEN_744;
    end
    if (4'hf == io_redirect_bits) begin
      _T_163_start_bank <= ram_15_start_bank;
    end else if (4'he == io_redirect_bits) begin
      _T_163_start_bank <= ram_14_start_bank;
    end else if (4'hd == io_redirect_bits) begin
      _T_163_start_bank <= ram_13_start_bank;
    end else if (4'hc == io_redirect_bits) begin
      _T_163_start_bank <= ram_12_start_bank;
    end else begin
      _T_163_start_bank <= _GEN_728;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_valid <= ram_15_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_valid <= ram_14_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_valid <= ram_13_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_valid <= ram_12_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_idx_valid <= _GEN_1740;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_bits <= ram_15_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_bits <= ram_14_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_bits <= ram_13_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_idx_bits <= ram_12_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_idx_bits <= _GEN_1724;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_taken <= ram_15_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_taken <= ram_14_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_taken <= ram_13_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_taken <= ram_12_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_taken <= _GEN_1708;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_mispredicted <= ram_15_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_mispredicted <= ram_14_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_mispredicted <= ram_13_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_mispredicted <= ram_12_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_mispredicted <= _GEN_1692;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_type <= ram_15_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_type <= ram_14_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_type <= ram_13_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_type <= ram_12_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_type <= _GEN_1676;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_br_mask <= ram_15_br_mask; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_br_mask <= ram_14_br_mask; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_br_mask <= ram_13_br_mask; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_br_mask <= ram_12_br_mask; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_br_mask <= _GEN_1660;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_call <= ram_15_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_call <= ram_14_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_call <= ram_13_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_call <= ram_12_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_is_call <= _GEN_1644;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_ret <= ram_15_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_ret <= ram_14_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_ret <= ram_13_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_is_ret <= ram_12_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_is_ret <= _GEN_1628;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_npc_plus4 <= ram_15_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_npc_plus4 <= ram_14_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_npc_plus4 <= ram_13_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_cfi_npc_plus4 <= ram_12_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_cfi_npc_plus4 <= _GEN_1612;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_top <= ram_15_ras_top; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_top <= ram_14_ras_top; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_top <= ram_13_ras_top; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_top <= ram_12_ras_top; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_ras_top <= _GEN_1596;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_idx <= ram_15_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_idx <= ram_14_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_idx <= ram_13_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_ras_idx <= ram_12_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_ras_idx <= _GEN_1580;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_start_bank <= ram_15_start_bank; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_start_bank <= ram_14_start_bank; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_start_bank <= ram_13_start_bank; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_171_start_bank <= ram_12_start_bank; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_171_start_bank <= _GEN_1564;
    end
    if (4'hf == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_172 <= pcs_15; // @[fetch-target-queue.scala 356:42]
    end else if (4'he == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_172 <= pcs_14; // @[fetch-target-queue.scala 356:42]
    end else if (4'hd == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_172 <= pcs_13; // @[fetch-target-queue.scala 356:42]
    end else if (4'hc == io_get_ftq_pc_0_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_172 <= pcs_12; // @[fetch-target-queue.scala 356:42]
    end else begin
      _T_172 <= _GEN_1756;
    end
    if (_T_169) begin // @[fetch-target-queue.scala 348:22]
      _T_173 <= io_enq_bits_pc;
    end else if (4'hf == _T_165) begin // @[fetch-target-queue.scala 348:22]
      _T_173 <= pcs_15; // @[fetch-target-queue.scala 348:22]
    end else if (4'he == _T_165) begin // @[fetch-target-queue.scala 348:22]
      _T_173 <= pcs_14; // @[fetch-target-queue.scala 348:22]
    end else if (4'hd == _T_165) begin // @[fetch-target-queue.scala 348:22]
      _T_173 <= pcs_13; // @[fetch-target-queue.scala 348:22]
    end else begin
      _T_173 <= _GEN_1549;
    end
    _T_176 <= _T_165 != enq_ptr | _T_169; // @[fetch-target-queue.scala 358:64]
    if (4'hf == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_178 <= pcs_15; // @[fetch-target-queue.scala 359:42]
    end else if (4'he == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_178 <= pcs_14; // @[fetch-target-queue.scala 359:42]
    end else if (4'hd == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_178 <= pcs_13; // @[fetch-target-queue.scala 359:42]
    end else if (4'hc == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_178 <= pcs_12; // @[fetch-target-queue.scala 359:42]
    end else begin
      _T_178 <= _GEN_1772;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_valid <= ram_15_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_valid <= ram_14_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_valid <= ram_13_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_valid <= ram_12_cfi_idx_valid; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_idx_valid <= _GEN_1980;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_bits <= ram_15_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_bits <= ram_14_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_bits <= ram_13_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_idx_bits <= ram_12_cfi_idx_bits; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_idx_bits <= _GEN_1964;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_taken <= ram_15_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_taken <= ram_14_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_taken <= ram_13_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_taken <= ram_12_cfi_taken; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_taken <= _GEN_1948;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_mispredicted <= ram_15_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_mispredicted <= ram_14_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_mispredicted <= ram_13_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_mispredicted <= ram_12_cfi_mispredicted; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_mispredicted <= _GEN_1932;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_type <= ram_15_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_type <= ram_14_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_type <= ram_13_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_type <= ram_12_cfi_type; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_type <= _GEN_1916;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_br_mask <= ram_15_br_mask; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_br_mask <= ram_14_br_mask; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_br_mask <= ram_13_br_mask; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_br_mask <= ram_12_br_mask; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_br_mask <= _GEN_1900;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_call <= ram_15_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_call <= ram_14_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_call <= ram_13_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_call <= ram_12_cfi_is_call; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_is_call <= _GEN_1884;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_ret <= ram_15_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_ret <= ram_14_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_ret <= ram_13_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_is_ret <= ram_12_cfi_is_ret; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_is_ret <= _GEN_1868;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_npc_plus4 <= ram_15_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_npc_plus4 <= ram_14_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_npc_plus4 <= ram_13_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_cfi_npc_plus4 <= ram_12_cfi_npc_plus4; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_cfi_npc_plus4 <= _GEN_1852;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_top <= ram_15_ras_top; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_top <= ram_14_ras_top; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_top <= ram_13_ras_top; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_top <= ram_12_ras_top; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_ras_top <= _GEN_1836;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_idx <= ram_15_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_idx <= ram_14_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_idx <= ram_13_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_ras_idx <= ram_12_ras_idx; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_ras_idx <= _GEN_1820;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_start_bank <= ram_15_start_bank; // @[fetch-target-queue.scala 351:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_start_bank <= ram_14_start_bank; // @[fetch-target-queue.scala 351:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_start_bank <= ram_13_start_bank; // @[fetch-target-queue.scala 351:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 351:42]
      _T_186_start_bank <= ram_12_start_bank; // @[fetch-target-queue.scala 351:42]
    end else begin
      _T_186_start_bank <= _GEN_1804;
    end
    if (4'hf == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_191 <= pcs_15; // @[fetch-target-queue.scala 356:42]
    end else if (4'he == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_191 <= pcs_14; // @[fetch-target-queue.scala 356:42]
    end else if (4'hd == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_191 <= pcs_13; // @[fetch-target-queue.scala 356:42]
    end else if (4'hc == io_get_ftq_pc_1_ftq_idx) begin // @[fetch-target-queue.scala 356:42]
      _T_191 <= pcs_12; // @[fetch-target-queue.scala 356:42]
    end else begin
      _T_191 <= _GEN_1998;
    end
    if (_T_184) begin // @[fetch-target-queue.scala 348:22]
      _T_192 <= io_enq_bits_pc;
    end else if (4'hf == _T_180) begin // @[fetch-target-queue.scala 348:22]
      _T_192 <= pcs_15; // @[fetch-target-queue.scala 348:22]
    end else if (4'he == _T_180) begin // @[fetch-target-queue.scala 348:22]
      _T_192 <= pcs_14; // @[fetch-target-queue.scala 348:22]
    end else if (4'hd == _T_180) begin // @[fetch-target-queue.scala 348:22]
      _T_192 <= pcs_13; // @[fetch-target-queue.scala 348:22]
    end else begin
      _T_192 <= _GEN_1789;
    end
    _T_195 <= _T_180 != enq_ptr | _T_184; // @[fetch-target-queue.scala 358:64]
    if (4'hf == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_197 <= pcs_15; // @[fetch-target-queue.scala 359:42]
    end else if (4'he == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_197 <= pcs_14; // @[fetch-target-queue.scala 359:42]
    end else if (4'hd == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_197 <= pcs_13; // @[fetch-target-queue.scala 359:42]
    end else if (4'hc == _GEN_445) begin // @[fetch-target-queue.scala 359:42]
      _T_197 <= pcs_12; // @[fetch-target-queue.scala 359:42]
    end else begin
      _T_197 <= _GEN_1772;
    end
    if (4'hf == io_debug_ftq_idx_0) begin // @[fetch-target-queue.scala 363:36]
      _T_198 <= pcs_15; // @[fetch-target-queue.scala 363:36]
    end else if (4'he == io_debug_ftq_idx_0) begin // @[fetch-target-queue.scala 363:36]
      _T_198 <= pcs_14; // @[fetch-target-queue.scala 363:36]
    end else if (4'hd == io_debug_ftq_idx_0) begin // @[fetch-target-queue.scala 363:36]
      _T_198 <= pcs_13; // @[fetch-target-queue.scala 363:36]
    end else if (4'hc == io_debug_ftq_idx_0) begin // @[fetch-target-queue.scala 363:36]
      _T_198 <= pcs_12; // @[fetch-target-queue.scala 363:36]
    end else begin
      _T_198 <= _GEN_2030;
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
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {4{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    meta_0[initvar] = _RAND_0[119:0];
  _RAND_3 = {2{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_0_old_history[initvar] = _RAND_3[63:0];
  _RAND_6 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_0_current_saw_branch_not_taken[initvar] = _RAND_6[0:0];
  _RAND_9 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_0_new_saw_branch_not_taken[initvar] = _RAND_9[0:0];
  _RAND_12 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_0_new_saw_branch_taken[initvar] = _RAND_12[0:0];
  _RAND_15 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_0_ras_idx[initvar] = _RAND_15[4:0];
  _RAND_18 = {2{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_1_old_history[initvar] = _RAND_18[63:0];
  _RAND_21 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_1_current_saw_branch_not_taken[initvar] = _RAND_21[0:0];
  _RAND_24 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_1_new_saw_branch_not_taken[initvar] = _RAND_24[0:0];
  _RAND_27 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_1_new_saw_branch_taken[initvar] = _RAND_27[0:0];
  _RAND_30 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ghist_1_ras_idx[initvar] = _RAND_30[4:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  meta_0_bpd_meta_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  meta_0_bpd_meta_addr_pipe_0 = _RAND_2[3:0];
  _RAND_4 = {1{`RANDOM}};
  ghist_0_old_history_bpd_ghist_en_pipe_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  ghist_0_old_history_bpd_ghist_addr_pipe_0 = _RAND_5[3:0];
  _RAND_7 = {1{`RANDOM}};
  ghist_0_current_saw_branch_not_taken_bpd_ghist_en_pipe_0 = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  ghist_0_current_saw_branch_not_taken_bpd_ghist_addr_pipe_0 = _RAND_8[3:0];
  _RAND_10 = {1{`RANDOM}};
  ghist_0_new_saw_branch_not_taken_bpd_ghist_en_pipe_0 = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  ghist_0_new_saw_branch_not_taken_bpd_ghist_addr_pipe_0 = _RAND_11[3:0];
  _RAND_13 = {1{`RANDOM}};
  ghist_0_new_saw_branch_taken_bpd_ghist_en_pipe_0 = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  ghist_0_new_saw_branch_taken_bpd_ghist_addr_pipe_0 = _RAND_14[3:0];
  _RAND_16 = {1{`RANDOM}};
  ghist_0_ras_idx_bpd_ghist_en_pipe_0 = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  ghist_0_ras_idx_bpd_ghist_addr_pipe_0 = _RAND_17[3:0];
  _RAND_19 = {1{`RANDOM}};
  ghist_1_old_history__T_190_en_pipe_0 = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  ghist_1_old_history__T_190_addr_pipe_0 = _RAND_20[3:0];
  _RAND_22 = {1{`RANDOM}};
  ghist_1_current_saw_branch_not_taken__T_190_en_pipe_0 = _RAND_22[0:0];
  _RAND_23 = {1{`RANDOM}};
  ghist_1_current_saw_branch_not_taken__T_190_addr_pipe_0 = _RAND_23[3:0];
  _RAND_25 = {1{`RANDOM}};
  ghist_1_new_saw_branch_not_taken__T_190_en_pipe_0 = _RAND_25[0:0];
  _RAND_26 = {1{`RANDOM}};
  ghist_1_new_saw_branch_not_taken__T_190_addr_pipe_0 = _RAND_26[3:0];
  _RAND_28 = {1{`RANDOM}};
  ghist_1_new_saw_branch_taken__T_190_en_pipe_0 = _RAND_28[0:0];
  _RAND_29 = {1{`RANDOM}};
  ghist_1_new_saw_branch_taken__T_190_addr_pipe_0 = _RAND_29[3:0];
  _RAND_31 = {1{`RANDOM}};
  ghist_1_ras_idx__T_190_en_pipe_0 = _RAND_31[0:0];
  _RAND_32 = {1{`RANDOM}};
  ghist_1_ras_idx__T_190_addr_pipe_0 = _RAND_32[3:0];
  _RAND_33 = {1{`RANDOM}};
  bpd_ptr = _RAND_33[3:0];
  _RAND_34 = {1{`RANDOM}};
  deq_ptr = _RAND_34[3:0];
  _RAND_35 = {1{`RANDOM}};
  enq_ptr = _RAND_35[3:0];
  _RAND_36 = {2{`RANDOM}};
  pcs_0 = _RAND_36[39:0];
  _RAND_37 = {2{`RANDOM}};
  pcs_1 = _RAND_37[39:0];
  _RAND_38 = {2{`RANDOM}};
  pcs_2 = _RAND_38[39:0];
  _RAND_39 = {2{`RANDOM}};
  pcs_3 = _RAND_39[39:0];
  _RAND_40 = {2{`RANDOM}};
  pcs_4 = _RAND_40[39:0];
  _RAND_41 = {2{`RANDOM}};
  pcs_5 = _RAND_41[39:0];
  _RAND_42 = {2{`RANDOM}};
  pcs_6 = _RAND_42[39:0];
  _RAND_43 = {2{`RANDOM}};
  pcs_7 = _RAND_43[39:0];
  _RAND_44 = {2{`RANDOM}};
  pcs_8 = _RAND_44[39:0];
  _RAND_45 = {2{`RANDOM}};
  pcs_9 = _RAND_45[39:0];
  _RAND_46 = {2{`RANDOM}};
  pcs_10 = _RAND_46[39:0];
  _RAND_47 = {2{`RANDOM}};
  pcs_11 = _RAND_47[39:0];
  _RAND_48 = {2{`RANDOM}};
  pcs_12 = _RAND_48[39:0];
  _RAND_49 = {2{`RANDOM}};
  pcs_13 = _RAND_49[39:0];
  _RAND_50 = {2{`RANDOM}};
  pcs_14 = _RAND_50[39:0];
  _RAND_51 = {2{`RANDOM}};
  pcs_15 = _RAND_51[39:0];
  _RAND_52 = {1{`RANDOM}};
  ram_0_cfi_idx_valid = _RAND_52[0:0];
  _RAND_53 = {1{`RANDOM}};
  ram_0_cfi_idx_bits = _RAND_53[1:0];
  _RAND_54 = {1{`RANDOM}};
  ram_0_cfi_taken = _RAND_54[0:0];
  _RAND_55 = {1{`RANDOM}};
  ram_0_cfi_mispredicted = _RAND_55[0:0];
  _RAND_56 = {1{`RANDOM}};
  ram_0_cfi_type = _RAND_56[2:0];
  _RAND_57 = {1{`RANDOM}};
  ram_0_br_mask = _RAND_57[3:0];
  _RAND_58 = {1{`RANDOM}};
  ram_0_cfi_is_call = _RAND_58[0:0];
  _RAND_59 = {1{`RANDOM}};
  ram_0_cfi_is_ret = _RAND_59[0:0];
  _RAND_60 = {1{`RANDOM}};
  ram_0_cfi_npc_plus4 = _RAND_60[0:0];
  _RAND_61 = {2{`RANDOM}};
  ram_0_ras_top = _RAND_61[39:0];
  _RAND_62 = {1{`RANDOM}};
  ram_0_ras_idx = _RAND_62[4:0];
  _RAND_63 = {1{`RANDOM}};
  ram_0_start_bank = _RAND_63[0:0];
  _RAND_64 = {1{`RANDOM}};
  ram_1_cfi_idx_valid = _RAND_64[0:0];
  _RAND_65 = {1{`RANDOM}};
  ram_1_cfi_idx_bits = _RAND_65[1:0];
  _RAND_66 = {1{`RANDOM}};
  ram_1_cfi_taken = _RAND_66[0:0];
  _RAND_67 = {1{`RANDOM}};
  ram_1_cfi_mispredicted = _RAND_67[0:0];
  _RAND_68 = {1{`RANDOM}};
  ram_1_cfi_type = _RAND_68[2:0];
  _RAND_69 = {1{`RANDOM}};
  ram_1_br_mask = _RAND_69[3:0];
  _RAND_70 = {1{`RANDOM}};
  ram_1_cfi_is_call = _RAND_70[0:0];
  _RAND_71 = {1{`RANDOM}};
  ram_1_cfi_is_ret = _RAND_71[0:0];
  _RAND_72 = {1{`RANDOM}};
  ram_1_cfi_npc_plus4 = _RAND_72[0:0];
  _RAND_73 = {2{`RANDOM}};
  ram_1_ras_top = _RAND_73[39:0];
  _RAND_74 = {1{`RANDOM}};
  ram_1_ras_idx = _RAND_74[4:0];
  _RAND_75 = {1{`RANDOM}};
  ram_1_start_bank = _RAND_75[0:0];
  _RAND_76 = {1{`RANDOM}};
  ram_2_cfi_idx_valid = _RAND_76[0:0];
  _RAND_77 = {1{`RANDOM}};
  ram_2_cfi_idx_bits = _RAND_77[1:0];
  _RAND_78 = {1{`RANDOM}};
  ram_2_cfi_taken = _RAND_78[0:0];
  _RAND_79 = {1{`RANDOM}};
  ram_2_cfi_mispredicted = _RAND_79[0:0];
  _RAND_80 = {1{`RANDOM}};
  ram_2_cfi_type = _RAND_80[2:0];
  _RAND_81 = {1{`RANDOM}};
  ram_2_br_mask = _RAND_81[3:0];
  _RAND_82 = {1{`RANDOM}};
  ram_2_cfi_is_call = _RAND_82[0:0];
  _RAND_83 = {1{`RANDOM}};
  ram_2_cfi_is_ret = _RAND_83[0:0];
  _RAND_84 = {1{`RANDOM}};
  ram_2_cfi_npc_plus4 = _RAND_84[0:0];
  _RAND_85 = {2{`RANDOM}};
  ram_2_ras_top = _RAND_85[39:0];
  _RAND_86 = {1{`RANDOM}};
  ram_2_ras_idx = _RAND_86[4:0];
  _RAND_87 = {1{`RANDOM}};
  ram_2_start_bank = _RAND_87[0:0];
  _RAND_88 = {1{`RANDOM}};
  ram_3_cfi_idx_valid = _RAND_88[0:0];
  _RAND_89 = {1{`RANDOM}};
  ram_3_cfi_idx_bits = _RAND_89[1:0];
  _RAND_90 = {1{`RANDOM}};
  ram_3_cfi_taken = _RAND_90[0:0];
  _RAND_91 = {1{`RANDOM}};
  ram_3_cfi_mispredicted = _RAND_91[0:0];
  _RAND_92 = {1{`RANDOM}};
  ram_3_cfi_type = _RAND_92[2:0];
  _RAND_93 = {1{`RANDOM}};
  ram_3_br_mask = _RAND_93[3:0];
  _RAND_94 = {1{`RANDOM}};
  ram_3_cfi_is_call = _RAND_94[0:0];
  _RAND_95 = {1{`RANDOM}};
  ram_3_cfi_is_ret = _RAND_95[0:0];
  _RAND_96 = {1{`RANDOM}};
  ram_3_cfi_npc_plus4 = _RAND_96[0:0];
  _RAND_97 = {2{`RANDOM}};
  ram_3_ras_top = _RAND_97[39:0];
  _RAND_98 = {1{`RANDOM}};
  ram_3_ras_idx = _RAND_98[4:0];
  _RAND_99 = {1{`RANDOM}};
  ram_3_start_bank = _RAND_99[0:0];
  _RAND_100 = {1{`RANDOM}};
  ram_4_cfi_idx_valid = _RAND_100[0:0];
  _RAND_101 = {1{`RANDOM}};
  ram_4_cfi_idx_bits = _RAND_101[1:0];
  _RAND_102 = {1{`RANDOM}};
  ram_4_cfi_taken = _RAND_102[0:0];
  _RAND_103 = {1{`RANDOM}};
  ram_4_cfi_mispredicted = _RAND_103[0:0];
  _RAND_104 = {1{`RANDOM}};
  ram_4_cfi_type = _RAND_104[2:0];
  _RAND_105 = {1{`RANDOM}};
  ram_4_br_mask = _RAND_105[3:0];
  _RAND_106 = {1{`RANDOM}};
  ram_4_cfi_is_call = _RAND_106[0:0];
  _RAND_107 = {1{`RANDOM}};
  ram_4_cfi_is_ret = _RAND_107[0:0];
  _RAND_108 = {1{`RANDOM}};
  ram_4_cfi_npc_plus4 = _RAND_108[0:0];
  _RAND_109 = {2{`RANDOM}};
  ram_4_ras_top = _RAND_109[39:0];
  _RAND_110 = {1{`RANDOM}};
  ram_4_ras_idx = _RAND_110[4:0];
  _RAND_111 = {1{`RANDOM}};
  ram_4_start_bank = _RAND_111[0:0];
  _RAND_112 = {1{`RANDOM}};
  ram_5_cfi_idx_valid = _RAND_112[0:0];
  _RAND_113 = {1{`RANDOM}};
  ram_5_cfi_idx_bits = _RAND_113[1:0];
  _RAND_114 = {1{`RANDOM}};
  ram_5_cfi_taken = _RAND_114[0:0];
  _RAND_115 = {1{`RANDOM}};
  ram_5_cfi_mispredicted = _RAND_115[0:0];
  _RAND_116 = {1{`RANDOM}};
  ram_5_cfi_type = _RAND_116[2:0];
  _RAND_117 = {1{`RANDOM}};
  ram_5_br_mask = _RAND_117[3:0];
  _RAND_118 = {1{`RANDOM}};
  ram_5_cfi_is_call = _RAND_118[0:0];
  _RAND_119 = {1{`RANDOM}};
  ram_5_cfi_is_ret = _RAND_119[0:0];
  _RAND_120 = {1{`RANDOM}};
  ram_5_cfi_npc_plus4 = _RAND_120[0:0];
  _RAND_121 = {2{`RANDOM}};
  ram_5_ras_top = _RAND_121[39:0];
  _RAND_122 = {1{`RANDOM}};
  ram_5_ras_idx = _RAND_122[4:0];
  _RAND_123 = {1{`RANDOM}};
  ram_5_start_bank = _RAND_123[0:0];
  _RAND_124 = {1{`RANDOM}};
  ram_6_cfi_idx_valid = _RAND_124[0:0];
  _RAND_125 = {1{`RANDOM}};
  ram_6_cfi_idx_bits = _RAND_125[1:0];
  _RAND_126 = {1{`RANDOM}};
  ram_6_cfi_taken = _RAND_126[0:0];
  _RAND_127 = {1{`RANDOM}};
  ram_6_cfi_mispredicted = _RAND_127[0:0];
  _RAND_128 = {1{`RANDOM}};
  ram_6_cfi_type = _RAND_128[2:0];
  _RAND_129 = {1{`RANDOM}};
  ram_6_br_mask = _RAND_129[3:0];
  _RAND_130 = {1{`RANDOM}};
  ram_6_cfi_is_call = _RAND_130[0:0];
  _RAND_131 = {1{`RANDOM}};
  ram_6_cfi_is_ret = _RAND_131[0:0];
  _RAND_132 = {1{`RANDOM}};
  ram_6_cfi_npc_plus4 = _RAND_132[0:0];
  _RAND_133 = {2{`RANDOM}};
  ram_6_ras_top = _RAND_133[39:0];
  _RAND_134 = {1{`RANDOM}};
  ram_6_ras_idx = _RAND_134[4:0];
  _RAND_135 = {1{`RANDOM}};
  ram_6_start_bank = _RAND_135[0:0];
  _RAND_136 = {1{`RANDOM}};
  ram_7_cfi_idx_valid = _RAND_136[0:0];
  _RAND_137 = {1{`RANDOM}};
  ram_7_cfi_idx_bits = _RAND_137[1:0];
  _RAND_138 = {1{`RANDOM}};
  ram_7_cfi_taken = _RAND_138[0:0];
  _RAND_139 = {1{`RANDOM}};
  ram_7_cfi_mispredicted = _RAND_139[0:0];
  _RAND_140 = {1{`RANDOM}};
  ram_7_cfi_type = _RAND_140[2:0];
  _RAND_141 = {1{`RANDOM}};
  ram_7_br_mask = _RAND_141[3:0];
  _RAND_142 = {1{`RANDOM}};
  ram_7_cfi_is_call = _RAND_142[0:0];
  _RAND_143 = {1{`RANDOM}};
  ram_7_cfi_is_ret = _RAND_143[0:0];
  _RAND_144 = {1{`RANDOM}};
  ram_7_cfi_npc_plus4 = _RAND_144[0:0];
  _RAND_145 = {2{`RANDOM}};
  ram_7_ras_top = _RAND_145[39:0];
  _RAND_146 = {1{`RANDOM}};
  ram_7_ras_idx = _RAND_146[4:0];
  _RAND_147 = {1{`RANDOM}};
  ram_7_start_bank = _RAND_147[0:0];
  _RAND_148 = {1{`RANDOM}};
  ram_8_cfi_idx_valid = _RAND_148[0:0];
  _RAND_149 = {1{`RANDOM}};
  ram_8_cfi_idx_bits = _RAND_149[1:0];
  _RAND_150 = {1{`RANDOM}};
  ram_8_cfi_taken = _RAND_150[0:0];
  _RAND_151 = {1{`RANDOM}};
  ram_8_cfi_mispredicted = _RAND_151[0:0];
  _RAND_152 = {1{`RANDOM}};
  ram_8_cfi_type = _RAND_152[2:0];
  _RAND_153 = {1{`RANDOM}};
  ram_8_br_mask = _RAND_153[3:0];
  _RAND_154 = {1{`RANDOM}};
  ram_8_cfi_is_call = _RAND_154[0:0];
  _RAND_155 = {1{`RANDOM}};
  ram_8_cfi_is_ret = _RAND_155[0:0];
  _RAND_156 = {1{`RANDOM}};
  ram_8_cfi_npc_plus4 = _RAND_156[0:0];
  _RAND_157 = {2{`RANDOM}};
  ram_8_ras_top = _RAND_157[39:0];
  _RAND_158 = {1{`RANDOM}};
  ram_8_ras_idx = _RAND_158[4:0];
  _RAND_159 = {1{`RANDOM}};
  ram_8_start_bank = _RAND_159[0:0];
  _RAND_160 = {1{`RANDOM}};
  ram_9_cfi_idx_valid = _RAND_160[0:0];
  _RAND_161 = {1{`RANDOM}};
  ram_9_cfi_idx_bits = _RAND_161[1:0];
  _RAND_162 = {1{`RANDOM}};
  ram_9_cfi_taken = _RAND_162[0:0];
  _RAND_163 = {1{`RANDOM}};
  ram_9_cfi_mispredicted = _RAND_163[0:0];
  _RAND_164 = {1{`RANDOM}};
  ram_9_cfi_type = _RAND_164[2:0];
  _RAND_165 = {1{`RANDOM}};
  ram_9_br_mask = _RAND_165[3:0];
  _RAND_166 = {1{`RANDOM}};
  ram_9_cfi_is_call = _RAND_166[0:0];
  _RAND_167 = {1{`RANDOM}};
  ram_9_cfi_is_ret = _RAND_167[0:0];
  _RAND_168 = {1{`RANDOM}};
  ram_9_cfi_npc_plus4 = _RAND_168[0:0];
  _RAND_169 = {2{`RANDOM}};
  ram_9_ras_top = _RAND_169[39:0];
  _RAND_170 = {1{`RANDOM}};
  ram_9_ras_idx = _RAND_170[4:0];
  _RAND_171 = {1{`RANDOM}};
  ram_9_start_bank = _RAND_171[0:0];
  _RAND_172 = {1{`RANDOM}};
  ram_10_cfi_idx_valid = _RAND_172[0:0];
  _RAND_173 = {1{`RANDOM}};
  ram_10_cfi_idx_bits = _RAND_173[1:0];
  _RAND_174 = {1{`RANDOM}};
  ram_10_cfi_taken = _RAND_174[0:0];
  _RAND_175 = {1{`RANDOM}};
  ram_10_cfi_mispredicted = _RAND_175[0:0];
  _RAND_176 = {1{`RANDOM}};
  ram_10_cfi_type = _RAND_176[2:0];
  _RAND_177 = {1{`RANDOM}};
  ram_10_br_mask = _RAND_177[3:0];
  _RAND_178 = {1{`RANDOM}};
  ram_10_cfi_is_call = _RAND_178[0:0];
  _RAND_179 = {1{`RANDOM}};
  ram_10_cfi_is_ret = _RAND_179[0:0];
  _RAND_180 = {1{`RANDOM}};
  ram_10_cfi_npc_plus4 = _RAND_180[0:0];
  _RAND_181 = {2{`RANDOM}};
  ram_10_ras_top = _RAND_181[39:0];
  _RAND_182 = {1{`RANDOM}};
  ram_10_ras_idx = _RAND_182[4:0];
  _RAND_183 = {1{`RANDOM}};
  ram_10_start_bank = _RAND_183[0:0];
  _RAND_184 = {1{`RANDOM}};
  ram_11_cfi_idx_valid = _RAND_184[0:0];
  _RAND_185 = {1{`RANDOM}};
  ram_11_cfi_idx_bits = _RAND_185[1:0];
  _RAND_186 = {1{`RANDOM}};
  ram_11_cfi_taken = _RAND_186[0:0];
  _RAND_187 = {1{`RANDOM}};
  ram_11_cfi_mispredicted = _RAND_187[0:0];
  _RAND_188 = {1{`RANDOM}};
  ram_11_cfi_type = _RAND_188[2:0];
  _RAND_189 = {1{`RANDOM}};
  ram_11_br_mask = _RAND_189[3:0];
  _RAND_190 = {1{`RANDOM}};
  ram_11_cfi_is_call = _RAND_190[0:0];
  _RAND_191 = {1{`RANDOM}};
  ram_11_cfi_is_ret = _RAND_191[0:0];
  _RAND_192 = {1{`RANDOM}};
  ram_11_cfi_npc_plus4 = _RAND_192[0:0];
  _RAND_193 = {2{`RANDOM}};
  ram_11_ras_top = _RAND_193[39:0];
  _RAND_194 = {1{`RANDOM}};
  ram_11_ras_idx = _RAND_194[4:0];
  _RAND_195 = {1{`RANDOM}};
  ram_11_start_bank = _RAND_195[0:0];
  _RAND_196 = {1{`RANDOM}};
  ram_12_cfi_idx_valid = _RAND_196[0:0];
  _RAND_197 = {1{`RANDOM}};
  ram_12_cfi_idx_bits = _RAND_197[1:0];
  _RAND_198 = {1{`RANDOM}};
  ram_12_cfi_taken = _RAND_198[0:0];
  _RAND_199 = {1{`RANDOM}};
  ram_12_cfi_mispredicted = _RAND_199[0:0];
  _RAND_200 = {1{`RANDOM}};
  ram_12_cfi_type = _RAND_200[2:0];
  _RAND_201 = {1{`RANDOM}};
  ram_12_br_mask = _RAND_201[3:0];
  _RAND_202 = {1{`RANDOM}};
  ram_12_cfi_is_call = _RAND_202[0:0];
  _RAND_203 = {1{`RANDOM}};
  ram_12_cfi_is_ret = _RAND_203[0:0];
  _RAND_204 = {1{`RANDOM}};
  ram_12_cfi_npc_plus4 = _RAND_204[0:0];
  _RAND_205 = {2{`RANDOM}};
  ram_12_ras_top = _RAND_205[39:0];
  _RAND_206 = {1{`RANDOM}};
  ram_12_ras_idx = _RAND_206[4:0];
  _RAND_207 = {1{`RANDOM}};
  ram_12_start_bank = _RAND_207[0:0];
  _RAND_208 = {1{`RANDOM}};
  ram_13_cfi_idx_valid = _RAND_208[0:0];
  _RAND_209 = {1{`RANDOM}};
  ram_13_cfi_idx_bits = _RAND_209[1:0];
  _RAND_210 = {1{`RANDOM}};
  ram_13_cfi_taken = _RAND_210[0:0];
  _RAND_211 = {1{`RANDOM}};
  ram_13_cfi_mispredicted = _RAND_211[0:0];
  _RAND_212 = {1{`RANDOM}};
  ram_13_cfi_type = _RAND_212[2:0];
  _RAND_213 = {1{`RANDOM}};
  ram_13_br_mask = _RAND_213[3:0];
  _RAND_214 = {1{`RANDOM}};
  ram_13_cfi_is_call = _RAND_214[0:0];
  _RAND_215 = {1{`RANDOM}};
  ram_13_cfi_is_ret = _RAND_215[0:0];
  _RAND_216 = {1{`RANDOM}};
  ram_13_cfi_npc_plus4 = _RAND_216[0:0];
  _RAND_217 = {2{`RANDOM}};
  ram_13_ras_top = _RAND_217[39:0];
  _RAND_218 = {1{`RANDOM}};
  ram_13_ras_idx = _RAND_218[4:0];
  _RAND_219 = {1{`RANDOM}};
  ram_13_start_bank = _RAND_219[0:0];
  _RAND_220 = {1{`RANDOM}};
  ram_14_cfi_idx_valid = _RAND_220[0:0];
  _RAND_221 = {1{`RANDOM}};
  ram_14_cfi_idx_bits = _RAND_221[1:0];
  _RAND_222 = {1{`RANDOM}};
  ram_14_cfi_taken = _RAND_222[0:0];
  _RAND_223 = {1{`RANDOM}};
  ram_14_cfi_mispredicted = _RAND_223[0:0];
  _RAND_224 = {1{`RANDOM}};
  ram_14_cfi_type = _RAND_224[2:0];
  _RAND_225 = {1{`RANDOM}};
  ram_14_br_mask = _RAND_225[3:0];
  _RAND_226 = {1{`RANDOM}};
  ram_14_cfi_is_call = _RAND_226[0:0];
  _RAND_227 = {1{`RANDOM}};
  ram_14_cfi_is_ret = _RAND_227[0:0];
  _RAND_228 = {1{`RANDOM}};
  ram_14_cfi_npc_plus4 = _RAND_228[0:0];
  _RAND_229 = {2{`RANDOM}};
  ram_14_ras_top = _RAND_229[39:0];
  _RAND_230 = {1{`RANDOM}};
  ram_14_ras_idx = _RAND_230[4:0];
  _RAND_231 = {1{`RANDOM}};
  ram_14_start_bank = _RAND_231[0:0];
  _RAND_232 = {1{`RANDOM}};
  ram_15_cfi_idx_valid = _RAND_232[0:0];
  _RAND_233 = {1{`RANDOM}};
  ram_15_cfi_idx_bits = _RAND_233[1:0];
  _RAND_234 = {1{`RANDOM}};
  ram_15_cfi_taken = _RAND_234[0:0];
  _RAND_235 = {1{`RANDOM}};
  ram_15_cfi_mispredicted = _RAND_235[0:0];
  _RAND_236 = {1{`RANDOM}};
  ram_15_cfi_type = _RAND_236[2:0];
  _RAND_237 = {1{`RANDOM}};
  ram_15_br_mask = _RAND_237[3:0];
  _RAND_238 = {1{`RANDOM}};
  ram_15_cfi_is_call = _RAND_238[0:0];
  _RAND_239 = {1{`RANDOM}};
  ram_15_cfi_is_ret = _RAND_239[0:0];
  _RAND_240 = {1{`RANDOM}};
  ram_15_cfi_npc_plus4 = _RAND_240[0:0];
  _RAND_241 = {2{`RANDOM}};
  ram_15_ras_top = _RAND_241[39:0];
  _RAND_242 = {1{`RANDOM}};
  ram_15_ras_idx = _RAND_242[4:0];
  _RAND_243 = {1{`RANDOM}};
  ram_15_start_bank = _RAND_243[0:0];
  _RAND_244 = {2{`RANDOM}};
  prev_ghist_old_history = _RAND_244[63:0];
  _RAND_245 = {1{`RANDOM}};
  prev_ghist_current_saw_branch_not_taken = _RAND_245[0:0];
  _RAND_246 = {1{`RANDOM}};
  prev_ghist_ras_idx = _RAND_246[4:0];
  _RAND_247 = {1{`RANDOM}};
  prev_entry_cfi_idx_valid = _RAND_247[0:0];
  _RAND_248 = {1{`RANDOM}};
  prev_entry_cfi_idx_bits = _RAND_248[1:0];
  _RAND_249 = {1{`RANDOM}};
  prev_entry_cfi_taken = _RAND_249[0:0];
  _RAND_250 = {1{`RANDOM}};
  prev_entry_br_mask = _RAND_250[3:0];
  _RAND_251 = {1{`RANDOM}};
  prev_entry_cfi_is_call = _RAND_251[0:0];
  _RAND_252 = {1{`RANDOM}};
  prev_entry_cfi_is_ret = _RAND_252[0:0];
  _RAND_253 = {1{`RANDOM}};
  first_empty = _RAND_253[0:0];
  _RAND_254 = {1{`RANDOM}};
  _T_60 = _RAND_254[0:0];
  _RAND_255 = {2{`RANDOM}};
  _T_61 = _RAND_255[39:0];
  _RAND_256 = {1{`RANDOM}};
  _T_62 = _RAND_256[4:0];
  _RAND_257 = {1{`RANDOM}};
  bpd_update_mispredict = _RAND_257[0:0];
  _RAND_258 = {1{`RANDOM}};
  bpd_update_repair = _RAND_258[0:0];
  _RAND_259 = {1{`RANDOM}};
  bpd_repair_idx = _RAND_259[3:0];
  _RAND_260 = {1{`RANDOM}};
  bpd_end_idx = _RAND_260[3:0];
  _RAND_261 = {2{`RANDOM}};
  bpd_repair_pc = _RAND_261[39:0];
  _RAND_262 = {1{`RANDOM}};
  bpd_entry_cfi_idx_valid = _RAND_262[0:0];
  _RAND_263 = {1{`RANDOM}};
  bpd_entry_cfi_idx_bits = _RAND_263[1:0];
  _RAND_264 = {1{`RANDOM}};
  bpd_entry_cfi_taken = _RAND_264[0:0];
  _RAND_265 = {1{`RANDOM}};
  bpd_entry_cfi_mispredicted = _RAND_265[0:0];
  _RAND_266 = {1{`RANDOM}};
  bpd_entry_cfi_type = _RAND_266[2:0];
  _RAND_267 = {1{`RANDOM}};
  bpd_entry_br_mask = _RAND_267[3:0];
  _RAND_268 = {2{`RANDOM}};
  bpd_pc = _RAND_268[39:0];
  _RAND_269 = {2{`RANDOM}};
  bpd_target = _RAND_269[39:0];
  _RAND_270 = {1{`RANDOM}};
  _T_74 = _RAND_270[0:0];
  _RAND_271 = {1{`RANDOM}};
  _T_75 = _RAND_271[3:0];
  _RAND_272 = {1{`RANDOM}};
  _T_76 = _RAND_272[3:0];
  _RAND_273 = {1{`RANDOM}};
  _T_80 = _RAND_273[0:0];
  _RAND_274 = {1{`RANDOM}};
  _T_108 = _RAND_274[0:0];
  _RAND_275 = {1{`RANDOM}};
  _T_112 = _RAND_275[0:0];
  _RAND_276 = {1{`RANDOM}};
  _T_118 = _RAND_276[0:0];
  _RAND_277 = {1{`RANDOM}};
  _T_123 = _RAND_277[0:0];
  _RAND_278 = {1{`RANDOM}};
  _T_124 = _RAND_278[0:0];
  _RAND_279 = {1{`RANDOM}};
  _T_145 = _RAND_279[0:0];
  _RAND_280 = {1{`RANDOM}};
  _T_158 = _RAND_280[0:0];
  _RAND_281 = {1{`RANDOM}};
  _T_159_cfi_idx_valid = _RAND_281[0:0];
  _RAND_282 = {1{`RANDOM}};
  _T_159_cfi_idx_bits = _RAND_282[1:0];
  _RAND_283 = {1{`RANDOM}};
  _T_159_cfi_taken = _RAND_283[0:0];
  _RAND_284 = {1{`RANDOM}};
  _T_159_br_mask = _RAND_284[3:0];
  _RAND_285 = {1{`RANDOM}};
  _T_159_cfi_is_call = _RAND_285[0:0];
  _RAND_286 = {1{`RANDOM}};
  _T_159_cfi_is_ret = _RAND_286[0:0];
  _RAND_287 = {1{`RANDOM}};
  _T_160 = _RAND_287[3:0];
  _RAND_288 = {1{`RANDOM}};
  _T_163_cfi_idx_valid = _RAND_288[0:0];
  _RAND_289 = {1{`RANDOM}};
  _T_163_cfi_idx_bits = _RAND_289[1:0];
  _RAND_290 = {1{`RANDOM}};
  _T_163_cfi_taken = _RAND_290[0:0];
  _RAND_291 = {1{`RANDOM}};
  _T_163_cfi_mispredicted = _RAND_291[0:0];
  _RAND_292 = {1{`RANDOM}};
  _T_163_cfi_type = _RAND_292[2:0];
  _RAND_293 = {1{`RANDOM}};
  _T_163_br_mask = _RAND_293[3:0];
  _RAND_294 = {1{`RANDOM}};
  _T_163_cfi_is_call = _RAND_294[0:0];
  _RAND_295 = {1{`RANDOM}};
  _T_163_cfi_is_ret = _RAND_295[0:0];
  _RAND_296 = {1{`RANDOM}};
  _T_163_cfi_npc_plus4 = _RAND_296[0:0];
  _RAND_297 = {2{`RANDOM}};
  _T_163_ras_top = _RAND_297[39:0];
  _RAND_298 = {1{`RANDOM}};
  _T_163_ras_idx = _RAND_298[4:0];
  _RAND_299 = {1{`RANDOM}};
  _T_163_start_bank = _RAND_299[0:0];
  _RAND_300 = {1{`RANDOM}};
  _T_171_cfi_idx_valid = _RAND_300[0:0];
  _RAND_301 = {1{`RANDOM}};
  _T_171_cfi_idx_bits = _RAND_301[1:0];
  _RAND_302 = {1{`RANDOM}};
  _T_171_cfi_taken = _RAND_302[0:0];
  _RAND_303 = {1{`RANDOM}};
  _T_171_cfi_mispredicted = _RAND_303[0:0];
  _RAND_304 = {1{`RANDOM}};
  _T_171_cfi_type = _RAND_304[2:0];
  _RAND_305 = {1{`RANDOM}};
  _T_171_br_mask = _RAND_305[3:0];
  _RAND_306 = {1{`RANDOM}};
  _T_171_cfi_is_call = _RAND_306[0:0];
  _RAND_307 = {1{`RANDOM}};
  _T_171_cfi_is_ret = _RAND_307[0:0];
  _RAND_308 = {1{`RANDOM}};
  _T_171_cfi_npc_plus4 = _RAND_308[0:0];
  _RAND_309 = {2{`RANDOM}};
  _T_171_ras_top = _RAND_309[39:0];
  _RAND_310 = {1{`RANDOM}};
  _T_171_ras_idx = _RAND_310[4:0];
  _RAND_311 = {1{`RANDOM}};
  _T_171_start_bank = _RAND_311[0:0];
  _RAND_312 = {2{`RANDOM}};
  _T_172 = _RAND_312[39:0];
  _RAND_313 = {2{`RANDOM}};
  _T_173 = _RAND_313[39:0];
  _RAND_314 = {1{`RANDOM}};
  _T_176 = _RAND_314[0:0];
  _RAND_315 = {2{`RANDOM}};
  _T_178 = _RAND_315[39:0];
  _RAND_316 = {1{`RANDOM}};
  _T_186_cfi_idx_valid = _RAND_316[0:0];
  _RAND_317 = {1{`RANDOM}};
  _T_186_cfi_idx_bits = _RAND_317[1:0];
  _RAND_318 = {1{`RANDOM}};
  _T_186_cfi_taken = _RAND_318[0:0];
  _RAND_319 = {1{`RANDOM}};
  _T_186_cfi_mispredicted = _RAND_319[0:0];
  _RAND_320 = {1{`RANDOM}};
  _T_186_cfi_type = _RAND_320[2:0];
  _RAND_321 = {1{`RANDOM}};
  _T_186_br_mask = _RAND_321[3:0];
  _RAND_322 = {1{`RANDOM}};
  _T_186_cfi_is_call = _RAND_322[0:0];
  _RAND_323 = {1{`RANDOM}};
  _T_186_cfi_is_ret = _RAND_323[0:0];
  _RAND_324 = {1{`RANDOM}};
  _T_186_cfi_npc_plus4 = _RAND_324[0:0];
  _RAND_325 = {2{`RANDOM}};
  _T_186_ras_top = _RAND_325[39:0];
  _RAND_326 = {1{`RANDOM}};
  _T_186_ras_idx = _RAND_326[4:0];
  _RAND_327 = {1{`RANDOM}};
  _T_186_start_bank = _RAND_327[0:0];
  _RAND_328 = {2{`RANDOM}};
  _T_191 = _RAND_328[39:0];
  _RAND_329 = {2{`RANDOM}};
  _T_192 = _RAND_329[39:0];
  _RAND_330 = {1{`RANDOM}};
  _T_195 = _RAND_330[0:0];
  _RAND_331 = {2{`RANDOM}};
  _T_197 = _RAND_331[39:0];
  _RAND_332 = {2{`RANDOM}};
  _T_198 = _RAND_332[39:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
