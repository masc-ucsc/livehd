module RenameMapTable(
  input         clock,
  input         reset,
  input  [5:0]  io_map_reqs_0_lrs1,
  input  [5:0]  io_map_reqs_0_lrs2,
  input  [5:0]  io_map_reqs_0_lrs3,
  input  [5:0]  io_map_reqs_0_ldst,
  output [5:0]  io_map_resps_0_prs1,
  output [5:0]  io_map_resps_0_prs2,
  output [5:0]  io_map_resps_0_prs3,
  output [5:0]  io_map_resps_0_stale_pdst,
  input  [5:0]  io_remap_reqs_0_ldst,
  input  [5:0]  io_remap_reqs_0_pdst,
  input         io_remap_reqs_0_valid,
  input         io_ren_br_tags_0_valid,
  input  [2:0]  io_ren_br_tags_0_bits,
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
  input         io_rollback
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
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
  reg [31:0] _RAND_17;
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
  reg [31:0] _RAND_31;
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
  reg [31:0] _RAND_45;
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
  reg [31:0] _RAND_59;
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
  reg [31:0] _RAND_73;
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
  reg [31:0] _RAND_97;
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
  reg [31:0] _RAND_109;
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
  reg [31:0] _RAND_121;
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
  reg [31:0] _RAND_133;
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
  reg [31:0] _RAND_145;
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
  reg [31:0] _RAND_157;
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
  reg [31:0] _RAND_169;
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
  reg [31:0] _RAND_254;
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
`endif // RANDOMIZE_REG_INIT
  reg [5:0] map_table_1; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_2; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_3; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_4; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_5; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_6; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_7; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_8; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_9; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_10; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_11; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_12; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_13; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_14; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_15; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_16; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_17; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_18; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_19; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_20; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_21; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_22; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_23; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_24; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_25; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_26; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_27; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_28; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_29; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_30; // @[rename-maptable.scala 70:26]
  reg [5:0] map_table_31; // @[rename-maptable.scala 70:26]
  reg [5:0] br_snapshots_0_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_0_31; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_1_31; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_2_31; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_3_31; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_4_31; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_5_31; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_6_31; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_1; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_2; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_3; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_4; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_5; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_6; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_7; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_8; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_9; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_10; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_11; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_12; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_13; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_14; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_15; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_16; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_17; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_18; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_19; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_20; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_21; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_22; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_23; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_24; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_25; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_26; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_27; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_28; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_29; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_30; // @[rename-maptable.scala 71:25]
  reg [5:0] br_snapshots_7_31; // @[rename-maptable.scala 71:25]
  wire [63:0] _T_1 = 64'h1 << io_remap_reqs_0_ldst; // @[OneHot.scala 58:35]
  wire [31:0] _T_3 = io_remap_reqs_0_valid ? 32'hffffffff : 32'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _GEN_928 = {{32'd0}, _T_3}; // @[rename-maptable.scala 78:69]
  wire [63:0] remap_ldsts_oh_0 = _T_1 & _GEN_928; // @[rename-maptable.scala 78:69]
  wire [5:0] _GEN_521 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_1 : br_snapshots_0_1; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_522 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_1 : _GEN_521; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_523 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_1 : _GEN_522; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_524 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_1 : _GEN_523; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_525 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_1 : _GEN_524; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_529 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_2 : br_snapshots_0_2; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_530 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_2 : _GEN_529; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_531 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_2 : _GEN_530; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_532 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_2 : _GEN_531; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_533 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_2 : _GEN_532; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_537 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_3 : br_snapshots_0_3; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_538 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_3 : _GEN_537; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_539 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_3 : _GEN_538; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_540 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_3 : _GEN_539; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_541 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_3 : _GEN_540; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_545 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_4 : br_snapshots_0_4; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_546 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_4 : _GEN_545; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_547 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_4 : _GEN_546; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_548 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_4 : _GEN_547; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_549 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_4 : _GEN_548; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_553 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_5 : br_snapshots_0_5; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_554 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_5 : _GEN_553; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_555 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_5 : _GEN_554; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_556 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_5 : _GEN_555; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_557 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_5 : _GEN_556; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_561 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_6 : br_snapshots_0_6; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_562 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_6 : _GEN_561; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_563 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_6 : _GEN_562; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_564 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_6 : _GEN_563; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_565 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_6 : _GEN_564; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_569 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_7 : br_snapshots_0_7; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_570 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_7 : _GEN_569; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_571 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_7 : _GEN_570; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_572 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_7 : _GEN_571; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_573 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_7 : _GEN_572; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_577 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_8 : br_snapshots_0_8; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_578 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_8 : _GEN_577; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_579 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_8 : _GEN_578; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_580 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_8 : _GEN_579; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_581 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_8 : _GEN_580; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_585 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_9 : br_snapshots_0_9; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_586 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_9 : _GEN_585; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_587 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_9 : _GEN_586; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_588 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_9 : _GEN_587; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_589 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_9 : _GEN_588; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_593 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_10 : br_snapshots_0_10; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_594 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_10 : _GEN_593; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_595 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_10 : _GEN_594; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_596 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_10 : _GEN_595; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_597 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_10 : _GEN_596; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_601 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_11 : br_snapshots_0_11; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_602 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_11 : _GEN_601; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_603 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_11 : _GEN_602; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_604 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_11 : _GEN_603; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_605 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_11 : _GEN_604; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_609 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_12 : br_snapshots_0_12; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_610 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_12 : _GEN_609; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_611 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_12 : _GEN_610; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_612 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_12 : _GEN_611; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_613 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_12 : _GEN_612; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_617 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_13 : br_snapshots_0_13; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_618 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_13 : _GEN_617; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_619 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_13 : _GEN_618; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_620 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_13 : _GEN_619; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_621 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_13 : _GEN_620; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_625 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_14 : br_snapshots_0_14; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_626 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_14 : _GEN_625; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_627 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_14 : _GEN_626; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_628 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_14 : _GEN_627; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_629 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_14 : _GEN_628; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_633 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_15 : br_snapshots_0_15; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_634 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_15 : _GEN_633; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_635 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_15 : _GEN_634; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_636 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_15 : _GEN_635; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_637 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_15 : _GEN_636; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_641 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_16 : br_snapshots_0_16; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_642 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_16 : _GEN_641; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_643 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_16 : _GEN_642; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_644 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_16 : _GEN_643; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_645 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_16 : _GEN_644; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_649 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_17 : br_snapshots_0_17; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_650 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_17 : _GEN_649; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_651 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_17 : _GEN_650; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_652 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_17 : _GEN_651; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_653 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_17 : _GEN_652; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_657 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_18 : br_snapshots_0_18; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_658 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_18 : _GEN_657; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_659 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_18 : _GEN_658; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_660 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_18 : _GEN_659; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_661 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_18 : _GEN_660; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_665 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_19 : br_snapshots_0_19; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_666 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_19 : _GEN_665; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_667 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_19 : _GEN_666; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_668 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_19 : _GEN_667; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_669 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_19 : _GEN_668; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_673 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_20 : br_snapshots_0_20; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_674 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_20 : _GEN_673; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_675 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_20 : _GEN_674; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_676 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_20 : _GEN_675; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_677 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_20 : _GEN_676; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_681 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_21 : br_snapshots_0_21; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_682 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_21 : _GEN_681; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_683 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_21 : _GEN_682; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_684 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_21 : _GEN_683; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_685 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_21 : _GEN_684; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_689 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_22 : br_snapshots_0_22; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_690 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_22 : _GEN_689; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_691 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_22 : _GEN_690; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_692 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_22 : _GEN_691; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_693 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_22 : _GEN_692; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_697 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_23 : br_snapshots_0_23; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_698 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_23 : _GEN_697; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_699 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_23 : _GEN_698; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_700 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_23 : _GEN_699; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_701 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_23 : _GEN_700; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_705 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_24 : br_snapshots_0_24; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_706 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_24 : _GEN_705; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_707 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_24 : _GEN_706; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_708 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_24 : _GEN_707; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_709 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_24 : _GEN_708; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_713 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_25 : br_snapshots_0_25; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_714 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_25 : _GEN_713; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_715 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_25 : _GEN_714; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_716 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_25 : _GEN_715; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_717 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_25 : _GEN_716; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_721 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_26 : br_snapshots_0_26; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_722 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_26 : _GEN_721; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_723 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_26 : _GEN_722; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_724 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_26 : _GEN_723; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_725 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_26 : _GEN_724; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_729 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_27 : br_snapshots_0_27; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_730 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_27 : _GEN_729; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_731 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_27 : _GEN_730; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_732 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_27 : _GEN_731; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_733 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_27 : _GEN_732; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_737 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_28 : br_snapshots_0_28; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_738 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_28 : _GEN_737; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_739 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_28 : _GEN_738; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_740 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_28 : _GEN_739; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_741 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_28 : _GEN_740; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_745 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_29 : br_snapshots_0_29; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_746 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_29 : _GEN_745; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_747 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_29 : _GEN_746; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_748 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_29 : _GEN_747; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_749 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_29 : _GEN_748; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_753 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_30 : br_snapshots_0_30; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_754 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_30 : _GEN_753; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_755 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_30 : _GEN_754; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_756 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_30 : _GEN_755; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_757 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_30 : _GEN_756; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_761 = 3'h1 == io_brupdate_b2_uop_br_tag ? br_snapshots_1_31 : br_snapshots_0_31; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_762 = 3'h2 == io_brupdate_b2_uop_br_tag ? br_snapshots_2_31 : _GEN_761; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_763 = 3'h3 == io_brupdate_b2_uop_br_tag ? br_snapshots_3_31 : _GEN_762; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_764 = 3'h4 == io_brupdate_b2_uop_br_tag ? br_snapshots_4_31 : _GEN_763; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_765 = 3'h5 == io_brupdate_b2_uop_br_tag ? br_snapshots_5_31 : _GEN_764; // @[rename-maptable.scala 105:{15,15}]
  wire [5:0] _GEN_801 = 5'h1 == io_map_reqs_0_lrs1[4:0] ? map_table_1 : 6'h0; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_802 = 5'h2 == io_map_reqs_0_lrs1[4:0] ? map_table_2 : _GEN_801; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_803 = 5'h3 == io_map_reqs_0_lrs1[4:0] ? map_table_3 : _GEN_802; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_804 = 5'h4 == io_map_reqs_0_lrs1[4:0] ? map_table_4 : _GEN_803; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_805 = 5'h5 == io_map_reqs_0_lrs1[4:0] ? map_table_5 : _GEN_804; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_806 = 5'h6 == io_map_reqs_0_lrs1[4:0] ? map_table_6 : _GEN_805; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_807 = 5'h7 == io_map_reqs_0_lrs1[4:0] ? map_table_7 : _GEN_806; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_808 = 5'h8 == io_map_reqs_0_lrs1[4:0] ? map_table_8 : _GEN_807; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_809 = 5'h9 == io_map_reqs_0_lrs1[4:0] ? map_table_9 : _GEN_808; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_810 = 5'ha == io_map_reqs_0_lrs1[4:0] ? map_table_10 : _GEN_809; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_811 = 5'hb == io_map_reqs_0_lrs1[4:0] ? map_table_11 : _GEN_810; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_812 = 5'hc == io_map_reqs_0_lrs1[4:0] ? map_table_12 : _GEN_811; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_813 = 5'hd == io_map_reqs_0_lrs1[4:0] ? map_table_13 : _GEN_812; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_814 = 5'he == io_map_reqs_0_lrs1[4:0] ? map_table_14 : _GEN_813; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_815 = 5'hf == io_map_reqs_0_lrs1[4:0] ? map_table_15 : _GEN_814; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_816 = 5'h10 == io_map_reqs_0_lrs1[4:0] ? map_table_16 : _GEN_815; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_817 = 5'h11 == io_map_reqs_0_lrs1[4:0] ? map_table_17 : _GEN_816; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_818 = 5'h12 == io_map_reqs_0_lrs1[4:0] ? map_table_18 : _GEN_817; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_819 = 5'h13 == io_map_reqs_0_lrs1[4:0] ? map_table_19 : _GEN_818; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_820 = 5'h14 == io_map_reqs_0_lrs1[4:0] ? map_table_20 : _GEN_819; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_821 = 5'h15 == io_map_reqs_0_lrs1[4:0] ? map_table_21 : _GEN_820; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_822 = 5'h16 == io_map_reqs_0_lrs1[4:0] ? map_table_22 : _GEN_821; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_823 = 5'h17 == io_map_reqs_0_lrs1[4:0] ? map_table_23 : _GEN_822; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_824 = 5'h18 == io_map_reqs_0_lrs1[4:0] ? map_table_24 : _GEN_823; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_825 = 5'h19 == io_map_reqs_0_lrs1[4:0] ? map_table_25 : _GEN_824; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_826 = 5'h1a == io_map_reqs_0_lrs1[4:0] ? map_table_26 : _GEN_825; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_827 = 5'h1b == io_map_reqs_0_lrs1[4:0] ? map_table_27 : _GEN_826; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_828 = 5'h1c == io_map_reqs_0_lrs1[4:0] ? map_table_28 : _GEN_827; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_829 = 5'h1d == io_map_reqs_0_lrs1[4:0] ? map_table_29 : _GEN_828; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_830 = 5'h1e == io_map_reqs_0_lrs1[4:0] ? map_table_30 : _GEN_829; // @[rename-maptable.scala 113:{32,32}]
  wire [5:0] _GEN_833 = 5'h1 == io_map_reqs_0_lrs2[4:0] ? map_table_1 : 6'h0; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_834 = 5'h2 == io_map_reqs_0_lrs2[4:0] ? map_table_2 : _GEN_833; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_835 = 5'h3 == io_map_reqs_0_lrs2[4:0] ? map_table_3 : _GEN_834; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_836 = 5'h4 == io_map_reqs_0_lrs2[4:0] ? map_table_4 : _GEN_835; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_837 = 5'h5 == io_map_reqs_0_lrs2[4:0] ? map_table_5 : _GEN_836; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_838 = 5'h6 == io_map_reqs_0_lrs2[4:0] ? map_table_6 : _GEN_837; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_839 = 5'h7 == io_map_reqs_0_lrs2[4:0] ? map_table_7 : _GEN_838; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_840 = 5'h8 == io_map_reqs_0_lrs2[4:0] ? map_table_8 : _GEN_839; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_841 = 5'h9 == io_map_reqs_0_lrs2[4:0] ? map_table_9 : _GEN_840; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_842 = 5'ha == io_map_reqs_0_lrs2[4:0] ? map_table_10 : _GEN_841; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_843 = 5'hb == io_map_reqs_0_lrs2[4:0] ? map_table_11 : _GEN_842; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_844 = 5'hc == io_map_reqs_0_lrs2[4:0] ? map_table_12 : _GEN_843; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_845 = 5'hd == io_map_reqs_0_lrs2[4:0] ? map_table_13 : _GEN_844; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_846 = 5'he == io_map_reqs_0_lrs2[4:0] ? map_table_14 : _GEN_845; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_847 = 5'hf == io_map_reqs_0_lrs2[4:0] ? map_table_15 : _GEN_846; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_848 = 5'h10 == io_map_reqs_0_lrs2[4:0] ? map_table_16 : _GEN_847; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_849 = 5'h11 == io_map_reqs_0_lrs2[4:0] ? map_table_17 : _GEN_848; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_850 = 5'h12 == io_map_reqs_0_lrs2[4:0] ? map_table_18 : _GEN_849; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_851 = 5'h13 == io_map_reqs_0_lrs2[4:0] ? map_table_19 : _GEN_850; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_852 = 5'h14 == io_map_reqs_0_lrs2[4:0] ? map_table_20 : _GEN_851; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_853 = 5'h15 == io_map_reqs_0_lrs2[4:0] ? map_table_21 : _GEN_852; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_854 = 5'h16 == io_map_reqs_0_lrs2[4:0] ? map_table_22 : _GEN_853; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_855 = 5'h17 == io_map_reqs_0_lrs2[4:0] ? map_table_23 : _GEN_854; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_856 = 5'h18 == io_map_reqs_0_lrs2[4:0] ? map_table_24 : _GEN_855; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_857 = 5'h19 == io_map_reqs_0_lrs2[4:0] ? map_table_25 : _GEN_856; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_858 = 5'h1a == io_map_reqs_0_lrs2[4:0] ? map_table_26 : _GEN_857; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_859 = 5'h1b == io_map_reqs_0_lrs2[4:0] ? map_table_27 : _GEN_858; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_860 = 5'h1c == io_map_reqs_0_lrs2[4:0] ? map_table_28 : _GEN_859; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_861 = 5'h1d == io_map_reqs_0_lrs2[4:0] ? map_table_29 : _GEN_860; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_862 = 5'h1e == io_map_reqs_0_lrs2[4:0] ? map_table_30 : _GEN_861; // @[rename-maptable.scala 115:{32,32}]
  wire [5:0] _GEN_897 = 5'h1 == io_map_reqs_0_ldst[4:0] ? map_table_1 : 6'h0; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_898 = 5'h2 == io_map_reqs_0_ldst[4:0] ? map_table_2 : _GEN_897; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_899 = 5'h3 == io_map_reqs_0_ldst[4:0] ? map_table_3 : _GEN_898; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_900 = 5'h4 == io_map_reqs_0_ldst[4:0] ? map_table_4 : _GEN_899; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_901 = 5'h5 == io_map_reqs_0_ldst[4:0] ? map_table_5 : _GEN_900; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_902 = 5'h6 == io_map_reqs_0_ldst[4:0] ? map_table_6 : _GEN_901; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_903 = 5'h7 == io_map_reqs_0_ldst[4:0] ? map_table_7 : _GEN_902; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_904 = 5'h8 == io_map_reqs_0_ldst[4:0] ? map_table_8 : _GEN_903; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_905 = 5'h9 == io_map_reqs_0_ldst[4:0] ? map_table_9 : _GEN_904; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_906 = 5'ha == io_map_reqs_0_ldst[4:0] ? map_table_10 : _GEN_905; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_907 = 5'hb == io_map_reqs_0_ldst[4:0] ? map_table_11 : _GEN_906; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_908 = 5'hc == io_map_reqs_0_ldst[4:0] ? map_table_12 : _GEN_907; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_909 = 5'hd == io_map_reqs_0_ldst[4:0] ? map_table_13 : _GEN_908; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_910 = 5'he == io_map_reqs_0_ldst[4:0] ? map_table_14 : _GEN_909; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_911 = 5'hf == io_map_reqs_0_ldst[4:0] ? map_table_15 : _GEN_910; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_912 = 5'h10 == io_map_reqs_0_ldst[4:0] ? map_table_16 : _GEN_911; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_913 = 5'h11 == io_map_reqs_0_ldst[4:0] ? map_table_17 : _GEN_912; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_914 = 5'h12 == io_map_reqs_0_ldst[4:0] ? map_table_18 : _GEN_913; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_915 = 5'h13 == io_map_reqs_0_ldst[4:0] ? map_table_19 : _GEN_914; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_916 = 5'h14 == io_map_reqs_0_ldst[4:0] ? map_table_20 : _GEN_915; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_917 = 5'h15 == io_map_reqs_0_ldst[4:0] ? map_table_21 : _GEN_916; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_918 = 5'h16 == io_map_reqs_0_ldst[4:0] ? map_table_22 : _GEN_917; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_919 = 5'h17 == io_map_reqs_0_ldst[4:0] ? map_table_23 : _GEN_918; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_920 = 5'h18 == io_map_reqs_0_ldst[4:0] ? map_table_24 : _GEN_919; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_921 = 5'h19 == io_map_reqs_0_ldst[4:0] ? map_table_25 : _GEN_920; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_922 = 5'h1a == io_map_reqs_0_ldst[4:0] ? map_table_26 : _GEN_921; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_923 = 5'h1b == io_map_reqs_0_ldst[4:0] ? map_table_27 : _GEN_922; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_924 = 5'h1c == io_map_reqs_0_ldst[4:0] ? map_table_28 : _GEN_923; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_925 = 5'h1d == io_map_reqs_0_ldst[4:0] ? map_table_29 : _GEN_924; // @[rename-maptable.scala 119:{32,32}]
  wire [5:0] _GEN_926 = 5'h1e == io_map_reqs_0_ldst[4:0] ? map_table_30 : _GEN_925; // @[rename-maptable.scala 119:{32,32}]
  wire  _T_86 = map_table_15 == io_remap_reqs_0_pdst; // @[rename-maptable.scala 128:38]
  wire  _T_118 = 6'h0 == io_remap_reqs_0_pdst | map_table_1 == io_remap_reqs_0_pdst | map_table_2 ==
    io_remap_reqs_0_pdst | map_table_3 == io_remap_reqs_0_pdst | map_table_4 == io_remap_reqs_0_pdst | map_table_5 ==
    io_remap_reqs_0_pdst | map_table_6 == io_remap_reqs_0_pdst | map_table_7 == io_remap_reqs_0_pdst | map_table_8 ==
    io_remap_reqs_0_pdst | map_table_9 == io_remap_reqs_0_pdst | map_table_10 == io_remap_reqs_0_pdst | map_table_11 ==
    io_remap_reqs_0_pdst | map_table_12 == io_remap_reqs_0_pdst | map_table_13 == io_remap_reqs_0_pdst | map_table_14
     == io_remap_reqs_0_pdst | _T_86; // @[rename-maptable.scala 128:38]
  wire  _T_133 = _T_118 | map_table_16 == io_remap_reqs_0_pdst | map_table_17 == io_remap_reqs_0_pdst | map_table_18 ==
    io_remap_reqs_0_pdst | map_table_19 == io_remap_reqs_0_pdst | map_table_20 == io_remap_reqs_0_pdst | map_table_21
     == io_remap_reqs_0_pdst | map_table_22 == io_remap_reqs_0_pdst | map_table_23 == io_remap_reqs_0_pdst |
    map_table_24 == io_remap_reqs_0_pdst | map_table_25 == io_remap_reqs_0_pdst | map_table_26 == io_remap_reqs_0_pdst
     | map_table_27 == io_remap_reqs_0_pdst | map_table_28 == io_remap_reqs_0_pdst | map_table_29 ==
    io_remap_reqs_0_pdst | map_table_30 == io_remap_reqs_0_pdst; // @[rename-maptable.scala 128:38]
  assign io_map_resps_0_prs1 = 5'h1f == io_map_reqs_0_lrs1[4:0] ? map_table_31 : _GEN_830; // @[rename-maptable.scala 113:{32,32}]
  assign io_map_resps_0_prs2 = 5'h1f == io_map_reqs_0_lrs2[4:0] ? map_table_31 : _GEN_862; // @[rename-maptable.scala 115:{32,32}]
  assign io_map_resps_0_prs3 = 6'h0;
  assign io_map_resps_0_stale_pdst = 5'h1f == io_map_reqs_0_ldst[4:0] ? map_table_31 : _GEN_926; // @[rename-maptable.scala 119:{32,32}]
  always @(posedge clock) begin
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_1 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_1 <= br_snapshots_7_1; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_1 <= br_snapshots_6_1; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_1 <= _GEN_525;
      end
    end else if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
      map_table_1 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_2 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_2 <= br_snapshots_7_2; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_2 <= br_snapshots_6_2; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_2 <= _GEN_533;
      end
    end else if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
      map_table_2 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_3 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_3 <= br_snapshots_7_3; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_3 <= br_snapshots_6_3; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_3 <= _GEN_541;
      end
    end else if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
      map_table_3 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_4 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_4 <= br_snapshots_7_4; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_4 <= br_snapshots_6_4; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_4 <= _GEN_549;
      end
    end else if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
      map_table_4 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_5 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_5 <= br_snapshots_7_5; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_5 <= br_snapshots_6_5; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_5 <= _GEN_557;
      end
    end else if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
      map_table_5 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_6 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_6 <= br_snapshots_7_6; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_6 <= br_snapshots_6_6; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_6 <= _GEN_565;
      end
    end else if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
      map_table_6 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_7 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_7 <= br_snapshots_7_7; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_7 <= br_snapshots_6_7; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_7 <= _GEN_573;
      end
    end else if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
      map_table_7 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_8 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_8 <= br_snapshots_7_8; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_8 <= br_snapshots_6_8; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_8 <= _GEN_581;
      end
    end else if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
      map_table_8 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_9 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_9 <= br_snapshots_7_9; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_9 <= br_snapshots_6_9; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_9 <= _GEN_589;
      end
    end else if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
      map_table_9 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_10 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_10 <= br_snapshots_7_10; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_10 <= br_snapshots_6_10; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_10 <= _GEN_597;
      end
    end else if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
      map_table_10 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_11 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_11 <= br_snapshots_7_11; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_11 <= br_snapshots_6_11; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_11 <= _GEN_605;
      end
    end else if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
      map_table_11 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_12 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_12 <= br_snapshots_7_12; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_12 <= br_snapshots_6_12; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_12 <= _GEN_613;
      end
    end else if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
      map_table_12 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_13 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_13 <= br_snapshots_7_13; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_13 <= br_snapshots_6_13; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_13 <= _GEN_621;
      end
    end else if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
      map_table_13 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_14 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_14 <= br_snapshots_7_14; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_14 <= br_snapshots_6_14; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_14 <= _GEN_629;
      end
    end else if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
      map_table_14 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_15 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_15 <= br_snapshots_7_15; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_15 <= br_snapshots_6_15; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_15 <= _GEN_637;
      end
    end else if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
      map_table_15 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_16 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_16 <= br_snapshots_7_16; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_16 <= br_snapshots_6_16; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_16 <= _GEN_645;
      end
    end else if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
      map_table_16 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_17 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_17 <= br_snapshots_7_17; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_17 <= br_snapshots_6_17; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_17 <= _GEN_653;
      end
    end else if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
      map_table_17 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_18 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_18 <= br_snapshots_7_18; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_18 <= br_snapshots_6_18; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_18 <= _GEN_661;
      end
    end else if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
      map_table_18 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_19 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_19 <= br_snapshots_7_19; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_19 <= br_snapshots_6_19; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_19 <= _GEN_669;
      end
    end else if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
      map_table_19 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_20 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_20 <= br_snapshots_7_20; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_20 <= br_snapshots_6_20; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_20 <= _GEN_677;
      end
    end else if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
      map_table_20 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_21 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_21 <= br_snapshots_7_21; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_21 <= br_snapshots_6_21; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_21 <= _GEN_685;
      end
    end else if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
      map_table_21 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_22 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_22 <= br_snapshots_7_22; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_22 <= br_snapshots_6_22; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_22 <= _GEN_693;
      end
    end else if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
      map_table_22 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_23 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_23 <= br_snapshots_7_23; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_23 <= br_snapshots_6_23; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_23 <= _GEN_701;
      end
    end else if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
      map_table_23 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_24 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_24 <= br_snapshots_7_24; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_24 <= br_snapshots_6_24; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_24 <= _GEN_709;
      end
    end else if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
      map_table_24 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_25 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_25 <= br_snapshots_7_25; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_25 <= br_snapshots_6_25; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_25 <= _GEN_717;
      end
    end else if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
      map_table_25 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_26 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_26 <= br_snapshots_7_26; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_26 <= br_snapshots_6_26; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_26 <= _GEN_725;
      end
    end else if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
      map_table_26 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_27 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_27 <= br_snapshots_7_27; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_27 <= br_snapshots_6_27; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_27 <= _GEN_733;
      end
    end else if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
      map_table_27 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_28 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_28 <= br_snapshots_7_28; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_28 <= br_snapshots_6_28; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_28 <= _GEN_741;
      end
    end else if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
      map_table_28 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_29 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_29 <= br_snapshots_7_29; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_29 <= br_snapshots_6_29; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_29 <= _GEN_749;
      end
    end else if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
      map_table_29 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_30 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_30 <= br_snapshots_7_30; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_30 <= br_snapshots_6_30; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_30 <= _GEN_757;
      end
    end else if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
      map_table_30 <= io_remap_reqs_0_pdst;
    end
    if (reset) begin // @[rename-maptable.scala 70:26]
      map_table_31 <= 6'h0; // @[rename-maptable.scala 70:26]
    end else if (io_brupdate_b2_mispredict) begin // @[rename-maptable.scala 103:36]
      if (3'h7 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_31 <= br_snapshots_7_31; // @[rename-maptable.scala 105:15]
      end else if (3'h6 == io_brupdate_b2_uop_br_tag) begin // @[rename-maptable.scala 105:15]
        map_table_31 <= br_snapshots_6_31; // @[rename-maptable.scala 105:15]
      end else begin
        map_table_31 <= _GEN_765;
      end
    end else if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
      map_table_31 <= io_remap_reqs_0_pdst;
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h0 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_0_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_0_31 <= map_table_31;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h1 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_1_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_1_31 <= map_table_31;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h2 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_2_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_2_31 <= map_table_31;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h3 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_3_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_3_31 <= map_table_31;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h4 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_4_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_4_31 <= map_table_31;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h5 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_5_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_5_31 <= map_table_31;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h6 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_6_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_6_31 <= map_table_31;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[1]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_1 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_1 <= map_table_1;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[2]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_2 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_2 <= map_table_2;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[3]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_3 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_3 <= map_table_3;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[4]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_4 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_4 <= map_table_4;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[5]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_5 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_5 <= map_table_5;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[6]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_6 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_6 <= map_table_6;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[7]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_7 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_7 <= map_table_7;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[8]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_8 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_8 <= map_table_8;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[9]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_9 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_9 <= map_table_9;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[10]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_10 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_10 <= map_table_10;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[11]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_11 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_11 <= map_table_11;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[12]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_12 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_12 <= map_table_12;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[13]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_13 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_13 <= map_table_13;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[14]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_14 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_14 <= map_table_14;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[15]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_15 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_15 <= map_table_15;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[16]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_16 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_16 <= map_table_16;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[17]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_17 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_17 <= map_table_17;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[18]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_18 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_18 <= map_table_18;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[19]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_19 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_19 <= map_table_19;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[20]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_20 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_20 <= map_table_20;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[21]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_21 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_21 <= map_table_21;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[22]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_22 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_22 <= map_table_22;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[23]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_23 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_23 <= map_table_23;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[24]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_24 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_24 <= map_table_24;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[25]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_25 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_25 <= map_table_25;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[26]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_26 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_26 <= map_table_26;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[27]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_27 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_27 <= map_table_27;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[28]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_28 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_28 <= map_table_28;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[29]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_29 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_29 <= map_table_29;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[30]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_30 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_30 <= map_table_30;
        end
      end
    end
    if (io_ren_br_tags_0_valid) begin // @[rename-maptable.scala 98:36]
      if (3'h7 == io_ren_br_tags_0_bits) begin // @[rename-maptable.scala 99:44]
        if (remap_ldsts_oh_0[31]) begin // @[rename-maptable.scala 88:70]
          br_snapshots_7_31 <= io_remap_reqs_0_pdst;
        end else begin
          br_snapshots_7_31 <= map_table_31;
        end
      end
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(~io_remap_reqs_0_valid | ~(_T_133 | map_table_31 == io_remap_reqs_0_pdst) | io_remap_reqs_0_pdst == 6'h0
           & io_rollback | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: [maptable] Trying to write a duplicate mapping.\n    at rename-maptable.scala:128 assert (!r || !map_table.contains(p) || p === 0.U && io.rollback, \"[maptable] Trying to write a duplicate mapping.\")}\n"
            ); // @[rename-maptable.scala 128:12]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(~io_remap_reqs_0_valid | ~(_T_133 | map_table_31 == io_remap_reqs_0_pdst) | io_remap_reqs_0_pdst == 6'h0
           & io_rollback | reset)) begin
          $fatal; // @[rename-maptable.scala 128:12]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
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
  map_table_1 = _RAND_0[5:0];
  _RAND_1 = {1{`RANDOM}};
  map_table_2 = _RAND_1[5:0];
  _RAND_2 = {1{`RANDOM}};
  map_table_3 = _RAND_2[5:0];
  _RAND_3 = {1{`RANDOM}};
  map_table_4 = _RAND_3[5:0];
  _RAND_4 = {1{`RANDOM}};
  map_table_5 = _RAND_4[5:0];
  _RAND_5 = {1{`RANDOM}};
  map_table_6 = _RAND_5[5:0];
  _RAND_6 = {1{`RANDOM}};
  map_table_7 = _RAND_6[5:0];
  _RAND_7 = {1{`RANDOM}};
  map_table_8 = _RAND_7[5:0];
  _RAND_8 = {1{`RANDOM}};
  map_table_9 = _RAND_8[5:0];
  _RAND_9 = {1{`RANDOM}};
  map_table_10 = _RAND_9[5:0];
  _RAND_10 = {1{`RANDOM}};
  map_table_11 = _RAND_10[5:0];
  _RAND_11 = {1{`RANDOM}};
  map_table_12 = _RAND_11[5:0];
  _RAND_12 = {1{`RANDOM}};
  map_table_13 = _RAND_12[5:0];
  _RAND_13 = {1{`RANDOM}};
  map_table_14 = _RAND_13[5:0];
  _RAND_14 = {1{`RANDOM}};
  map_table_15 = _RAND_14[5:0];
  _RAND_15 = {1{`RANDOM}};
  map_table_16 = _RAND_15[5:0];
  _RAND_16 = {1{`RANDOM}};
  map_table_17 = _RAND_16[5:0];
  _RAND_17 = {1{`RANDOM}};
  map_table_18 = _RAND_17[5:0];
  _RAND_18 = {1{`RANDOM}};
  map_table_19 = _RAND_18[5:0];
  _RAND_19 = {1{`RANDOM}};
  map_table_20 = _RAND_19[5:0];
  _RAND_20 = {1{`RANDOM}};
  map_table_21 = _RAND_20[5:0];
  _RAND_21 = {1{`RANDOM}};
  map_table_22 = _RAND_21[5:0];
  _RAND_22 = {1{`RANDOM}};
  map_table_23 = _RAND_22[5:0];
  _RAND_23 = {1{`RANDOM}};
  map_table_24 = _RAND_23[5:0];
  _RAND_24 = {1{`RANDOM}};
  map_table_25 = _RAND_24[5:0];
  _RAND_25 = {1{`RANDOM}};
  map_table_26 = _RAND_25[5:0];
  _RAND_26 = {1{`RANDOM}};
  map_table_27 = _RAND_26[5:0];
  _RAND_27 = {1{`RANDOM}};
  map_table_28 = _RAND_27[5:0];
  _RAND_28 = {1{`RANDOM}};
  map_table_29 = _RAND_28[5:0];
  _RAND_29 = {1{`RANDOM}};
  map_table_30 = _RAND_29[5:0];
  _RAND_30 = {1{`RANDOM}};
  map_table_31 = _RAND_30[5:0];
  _RAND_31 = {1{`RANDOM}};
  br_snapshots_0_1 = _RAND_31[5:0];
  _RAND_32 = {1{`RANDOM}};
  br_snapshots_0_2 = _RAND_32[5:0];
  _RAND_33 = {1{`RANDOM}};
  br_snapshots_0_3 = _RAND_33[5:0];
  _RAND_34 = {1{`RANDOM}};
  br_snapshots_0_4 = _RAND_34[5:0];
  _RAND_35 = {1{`RANDOM}};
  br_snapshots_0_5 = _RAND_35[5:0];
  _RAND_36 = {1{`RANDOM}};
  br_snapshots_0_6 = _RAND_36[5:0];
  _RAND_37 = {1{`RANDOM}};
  br_snapshots_0_7 = _RAND_37[5:0];
  _RAND_38 = {1{`RANDOM}};
  br_snapshots_0_8 = _RAND_38[5:0];
  _RAND_39 = {1{`RANDOM}};
  br_snapshots_0_9 = _RAND_39[5:0];
  _RAND_40 = {1{`RANDOM}};
  br_snapshots_0_10 = _RAND_40[5:0];
  _RAND_41 = {1{`RANDOM}};
  br_snapshots_0_11 = _RAND_41[5:0];
  _RAND_42 = {1{`RANDOM}};
  br_snapshots_0_12 = _RAND_42[5:0];
  _RAND_43 = {1{`RANDOM}};
  br_snapshots_0_13 = _RAND_43[5:0];
  _RAND_44 = {1{`RANDOM}};
  br_snapshots_0_14 = _RAND_44[5:0];
  _RAND_45 = {1{`RANDOM}};
  br_snapshots_0_15 = _RAND_45[5:0];
  _RAND_46 = {1{`RANDOM}};
  br_snapshots_0_16 = _RAND_46[5:0];
  _RAND_47 = {1{`RANDOM}};
  br_snapshots_0_17 = _RAND_47[5:0];
  _RAND_48 = {1{`RANDOM}};
  br_snapshots_0_18 = _RAND_48[5:0];
  _RAND_49 = {1{`RANDOM}};
  br_snapshots_0_19 = _RAND_49[5:0];
  _RAND_50 = {1{`RANDOM}};
  br_snapshots_0_20 = _RAND_50[5:0];
  _RAND_51 = {1{`RANDOM}};
  br_snapshots_0_21 = _RAND_51[5:0];
  _RAND_52 = {1{`RANDOM}};
  br_snapshots_0_22 = _RAND_52[5:0];
  _RAND_53 = {1{`RANDOM}};
  br_snapshots_0_23 = _RAND_53[5:0];
  _RAND_54 = {1{`RANDOM}};
  br_snapshots_0_24 = _RAND_54[5:0];
  _RAND_55 = {1{`RANDOM}};
  br_snapshots_0_25 = _RAND_55[5:0];
  _RAND_56 = {1{`RANDOM}};
  br_snapshots_0_26 = _RAND_56[5:0];
  _RAND_57 = {1{`RANDOM}};
  br_snapshots_0_27 = _RAND_57[5:0];
  _RAND_58 = {1{`RANDOM}};
  br_snapshots_0_28 = _RAND_58[5:0];
  _RAND_59 = {1{`RANDOM}};
  br_snapshots_0_29 = _RAND_59[5:0];
  _RAND_60 = {1{`RANDOM}};
  br_snapshots_0_30 = _RAND_60[5:0];
  _RAND_61 = {1{`RANDOM}};
  br_snapshots_0_31 = _RAND_61[5:0];
  _RAND_62 = {1{`RANDOM}};
  br_snapshots_1_1 = _RAND_62[5:0];
  _RAND_63 = {1{`RANDOM}};
  br_snapshots_1_2 = _RAND_63[5:0];
  _RAND_64 = {1{`RANDOM}};
  br_snapshots_1_3 = _RAND_64[5:0];
  _RAND_65 = {1{`RANDOM}};
  br_snapshots_1_4 = _RAND_65[5:0];
  _RAND_66 = {1{`RANDOM}};
  br_snapshots_1_5 = _RAND_66[5:0];
  _RAND_67 = {1{`RANDOM}};
  br_snapshots_1_6 = _RAND_67[5:0];
  _RAND_68 = {1{`RANDOM}};
  br_snapshots_1_7 = _RAND_68[5:0];
  _RAND_69 = {1{`RANDOM}};
  br_snapshots_1_8 = _RAND_69[5:0];
  _RAND_70 = {1{`RANDOM}};
  br_snapshots_1_9 = _RAND_70[5:0];
  _RAND_71 = {1{`RANDOM}};
  br_snapshots_1_10 = _RAND_71[5:0];
  _RAND_72 = {1{`RANDOM}};
  br_snapshots_1_11 = _RAND_72[5:0];
  _RAND_73 = {1{`RANDOM}};
  br_snapshots_1_12 = _RAND_73[5:0];
  _RAND_74 = {1{`RANDOM}};
  br_snapshots_1_13 = _RAND_74[5:0];
  _RAND_75 = {1{`RANDOM}};
  br_snapshots_1_14 = _RAND_75[5:0];
  _RAND_76 = {1{`RANDOM}};
  br_snapshots_1_15 = _RAND_76[5:0];
  _RAND_77 = {1{`RANDOM}};
  br_snapshots_1_16 = _RAND_77[5:0];
  _RAND_78 = {1{`RANDOM}};
  br_snapshots_1_17 = _RAND_78[5:0];
  _RAND_79 = {1{`RANDOM}};
  br_snapshots_1_18 = _RAND_79[5:0];
  _RAND_80 = {1{`RANDOM}};
  br_snapshots_1_19 = _RAND_80[5:0];
  _RAND_81 = {1{`RANDOM}};
  br_snapshots_1_20 = _RAND_81[5:0];
  _RAND_82 = {1{`RANDOM}};
  br_snapshots_1_21 = _RAND_82[5:0];
  _RAND_83 = {1{`RANDOM}};
  br_snapshots_1_22 = _RAND_83[5:0];
  _RAND_84 = {1{`RANDOM}};
  br_snapshots_1_23 = _RAND_84[5:0];
  _RAND_85 = {1{`RANDOM}};
  br_snapshots_1_24 = _RAND_85[5:0];
  _RAND_86 = {1{`RANDOM}};
  br_snapshots_1_25 = _RAND_86[5:0];
  _RAND_87 = {1{`RANDOM}};
  br_snapshots_1_26 = _RAND_87[5:0];
  _RAND_88 = {1{`RANDOM}};
  br_snapshots_1_27 = _RAND_88[5:0];
  _RAND_89 = {1{`RANDOM}};
  br_snapshots_1_28 = _RAND_89[5:0];
  _RAND_90 = {1{`RANDOM}};
  br_snapshots_1_29 = _RAND_90[5:0];
  _RAND_91 = {1{`RANDOM}};
  br_snapshots_1_30 = _RAND_91[5:0];
  _RAND_92 = {1{`RANDOM}};
  br_snapshots_1_31 = _RAND_92[5:0];
  _RAND_93 = {1{`RANDOM}};
  br_snapshots_2_1 = _RAND_93[5:0];
  _RAND_94 = {1{`RANDOM}};
  br_snapshots_2_2 = _RAND_94[5:0];
  _RAND_95 = {1{`RANDOM}};
  br_snapshots_2_3 = _RAND_95[5:0];
  _RAND_96 = {1{`RANDOM}};
  br_snapshots_2_4 = _RAND_96[5:0];
  _RAND_97 = {1{`RANDOM}};
  br_snapshots_2_5 = _RAND_97[5:0];
  _RAND_98 = {1{`RANDOM}};
  br_snapshots_2_6 = _RAND_98[5:0];
  _RAND_99 = {1{`RANDOM}};
  br_snapshots_2_7 = _RAND_99[5:0];
  _RAND_100 = {1{`RANDOM}};
  br_snapshots_2_8 = _RAND_100[5:0];
  _RAND_101 = {1{`RANDOM}};
  br_snapshots_2_9 = _RAND_101[5:0];
  _RAND_102 = {1{`RANDOM}};
  br_snapshots_2_10 = _RAND_102[5:0];
  _RAND_103 = {1{`RANDOM}};
  br_snapshots_2_11 = _RAND_103[5:0];
  _RAND_104 = {1{`RANDOM}};
  br_snapshots_2_12 = _RAND_104[5:0];
  _RAND_105 = {1{`RANDOM}};
  br_snapshots_2_13 = _RAND_105[5:0];
  _RAND_106 = {1{`RANDOM}};
  br_snapshots_2_14 = _RAND_106[5:0];
  _RAND_107 = {1{`RANDOM}};
  br_snapshots_2_15 = _RAND_107[5:0];
  _RAND_108 = {1{`RANDOM}};
  br_snapshots_2_16 = _RAND_108[5:0];
  _RAND_109 = {1{`RANDOM}};
  br_snapshots_2_17 = _RAND_109[5:0];
  _RAND_110 = {1{`RANDOM}};
  br_snapshots_2_18 = _RAND_110[5:0];
  _RAND_111 = {1{`RANDOM}};
  br_snapshots_2_19 = _RAND_111[5:0];
  _RAND_112 = {1{`RANDOM}};
  br_snapshots_2_20 = _RAND_112[5:0];
  _RAND_113 = {1{`RANDOM}};
  br_snapshots_2_21 = _RAND_113[5:0];
  _RAND_114 = {1{`RANDOM}};
  br_snapshots_2_22 = _RAND_114[5:0];
  _RAND_115 = {1{`RANDOM}};
  br_snapshots_2_23 = _RAND_115[5:0];
  _RAND_116 = {1{`RANDOM}};
  br_snapshots_2_24 = _RAND_116[5:0];
  _RAND_117 = {1{`RANDOM}};
  br_snapshots_2_25 = _RAND_117[5:0];
  _RAND_118 = {1{`RANDOM}};
  br_snapshots_2_26 = _RAND_118[5:0];
  _RAND_119 = {1{`RANDOM}};
  br_snapshots_2_27 = _RAND_119[5:0];
  _RAND_120 = {1{`RANDOM}};
  br_snapshots_2_28 = _RAND_120[5:0];
  _RAND_121 = {1{`RANDOM}};
  br_snapshots_2_29 = _RAND_121[5:0];
  _RAND_122 = {1{`RANDOM}};
  br_snapshots_2_30 = _RAND_122[5:0];
  _RAND_123 = {1{`RANDOM}};
  br_snapshots_2_31 = _RAND_123[5:0];
  _RAND_124 = {1{`RANDOM}};
  br_snapshots_3_1 = _RAND_124[5:0];
  _RAND_125 = {1{`RANDOM}};
  br_snapshots_3_2 = _RAND_125[5:0];
  _RAND_126 = {1{`RANDOM}};
  br_snapshots_3_3 = _RAND_126[5:0];
  _RAND_127 = {1{`RANDOM}};
  br_snapshots_3_4 = _RAND_127[5:0];
  _RAND_128 = {1{`RANDOM}};
  br_snapshots_3_5 = _RAND_128[5:0];
  _RAND_129 = {1{`RANDOM}};
  br_snapshots_3_6 = _RAND_129[5:0];
  _RAND_130 = {1{`RANDOM}};
  br_snapshots_3_7 = _RAND_130[5:0];
  _RAND_131 = {1{`RANDOM}};
  br_snapshots_3_8 = _RAND_131[5:0];
  _RAND_132 = {1{`RANDOM}};
  br_snapshots_3_9 = _RAND_132[5:0];
  _RAND_133 = {1{`RANDOM}};
  br_snapshots_3_10 = _RAND_133[5:0];
  _RAND_134 = {1{`RANDOM}};
  br_snapshots_3_11 = _RAND_134[5:0];
  _RAND_135 = {1{`RANDOM}};
  br_snapshots_3_12 = _RAND_135[5:0];
  _RAND_136 = {1{`RANDOM}};
  br_snapshots_3_13 = _RAND_136[5:0];
  _RAND_137 = {1{`RANDOM}};
  br_snapshots_3_14 = _RAND_137[5:0];
  _RAND_138 = {1{`RANDOM}};
  br_snapshots_3_15 = _RAND_138[5:0];
  _RAND_139 = {1{`RANDOM}};
  br_snapshots_3_16 = _RAND_139[5:0];
  _RAND_140 = {1{`RANDOM}};
  br_snapshots_3_17 = _RAND_140[5:0];
  _RAND_141 = {1{`RANDOM}};
  br_snapshots_3_18 = _RAND_141[5:0];
  _RAND_142 = {1{`RANDOM}};
  br_snapshots_3_19 = _RAND_142[5:0];
  _RAND_143 = {1{`RANDOM}};
  br_snapshots_3_20 = _RAND_143[5:0];
  _RAND_144 = {1{`RANDOM}};
  br_snapshots_3_21 = _RAND_144[5:0];
  _RAND_145 = {1{`RANDOM}};
  br_snapshots_3_22 = _RAND_145[5:0];
  _RAND_146 = {1{`RANDOM}};
  br_snapshots_3_23 = _RAND_146[5:0];
  _RAND_147 = {1{`RANDOM}};
  br_snapshots_3_24 = _RAND_147[5:0];
  _RAND_148 = {1{`RANDOM}};
  br_snapshots_3_25 = _RAND_148[5:0];
  _RAND_149 = {1{`RANDOM}};
  br_snapshots_3_26 = _RAND_149[5:0];
  _RAND_150 = {1{`RANDOM}};
  br_snapshots_3_27 = _RAND_150[5:0];
  _RAND_151 = {1{`RANDOM}};
  br_snapshots_3_28 = _RAND_151[5:0];
  _RAND_152 = {1{`RANDOM}};
  br_snapshots_3_29 = _RAND_152[5:0];
  _RAND_153 = {1{`RANDOM}};
  br_snapshots_3_30 = _RAND_153[5:0];
  _RAND_154 = {1{`RANDOM}};
  br_snapshots_3_31 = _RAND_154[5:0];
  _RAND_155 = {1{`RANDOM}};
  br_snapshots_4_1 = _RAND_155[5:0];
  _RAND_156 = {1{`RANDOM}};
  br_snapshots_4_2 = _RAND_156[5:0];
  _RAND_157 = {1{`RANDOM}};
  br_snapshots_4_3 = _RAND_157[5:0];
  _RAND_158 = {1{`RANDOM}};
  br_snapshots_4_4 = _RAND_158[5:0];
  _RAND_159 = {1{`RANDOM}};
  br_snapshots_4_5 = _RAND_159[5:0];
  _RAND_160 = {1{`RANDOM}};
  br_snapshots_4_6 = _RAND_160[5:0];
  _RAND_161 = {1{`RANDOM}};
  br_snapshots_4_7 = _RAND_161[5:0];
  _RAND_162 = {1{`RANDOM}};
  br_snapshots_4_8 = _RAND_162[5:0];
  _RAND_163 = {1{`RANDOM}};
  br_snapshots_4_9 = _RAND_163[5:0];
  _RAND_164 = {1{`RANDOM}};
  br_snapshots_4_10 = _RAND_164[5:0];
  _RAND_165 = {1{`RANDOM}};
  br_snapshots_4_11 = _RAND_165[5:0];
  _RAND_166 = {1{`RANDOM}};
  br_snapshots_4_12 = _RAND_166[5:0];
  _RAND_167 = {1{`RANDOM}};
  br_snapshots_4_13 = _RAND_167[5:0];
  _RAND_168 = {1{`RANDOM}};
  br_snapshots_4_14 = _RAND_168[5:0];
  _RAND_169 = {1{`RANDOM}};
  br_snapshots_4_15 = _RAND_169[5:0];
  _RAND_170 = {1{`RANDOM}};
  br_snapshots_4_16 = _RAND_170[5:0];
  _RAND_171 = {1{`RANDOM}};
  br_snapshots_4_17 = _RAND_171[5:0];
  _RAND_172 = {1{`RANDOM}};
  br_snapshots_4_18 = _RAND_172[5:0];
  _RAND_173 = {1{`RANDOM}};
  br_snapshots_4_19 = _RAND_173[5:0];
  _RAND_174 = {1{`RANDOM}};
  br_snapshots_4_20 = _RAND_174[5:0];
  _RAND_175 = {1{`RANDOM}};
  br_snapshots_4_21 = _RAND_175[5:0];
  _RAND_176 = {1{`RANDOM}};
  br_snapshots_4_22 = _RAND_176[5:0];
  _RAND_177 = {1{`RANDOM}};
  br_snapshots_4_23 = _RAND_177[5:0];
  _RAND_178 = {1{`RANDOM}};
  br_snapshots_4_24 = _RAND_178[5:0];
  _RAND_179 = {1{`RANDOM}};
  br_snapshots_4_25 = _RAND_179[5:0];
  _RAND_180 = {1{`RANDOM}};
  br_snapshots_4_26 = _RAND_180[5:0];
  _RAND_181 = {1{`RANDOM}};
  br_snapshots_4_27 = _RAND_181[5:0];
  _RAND_182 = {1{`RANDOM}};
  br_snapshots_4_28 = _RAND_182[5:0];
  _RAND_183 = {1{`RANDOM}};
  br_snapshots_4_29 = _RAND_183[5:0];
  _RAND_184 = {1{`RANDOM}};
  br_snapshots_4_30 = _RAND_184[5:0];
  _RAND_185 = {1{`RANDOM}};
  br_snapshots_4_31 = _RAND_185[5:0];
  _RAND_186 = {1{`RANDOM}};
  br_snapshots_5_1 = _RAND_186[5:0];
  _RAND_187 = {1{`RANDOM}};
  br_snapshots_5_2 = _RAND_187[5:0];
  _RAND_188 = {1{`RANDOM}};
  br_snapshots_5_3 = _RAND_188[5:0];
  _RAND_189 = {1{`RANDOM}};
  br_snapshots_5_4 = _RAND_189[5:0];
  _RAND_190 = {1{`RANDOM}};
  br_snapshots_5_5 = _RAND_190[5:0];
  _RAND_191 = {1{`RANDOM}};
  br_snapshots_5_6 = _RAND_191[5:0];
  _RAND_192 = {1{`RANDOM}};
  br_snapshots_5_7 = _RAND_192[5:0];
  _RAND_193 = {1{`RANDOM}};
  br_snapshots_5_8 = _RAND_193[5:0];
  _RAND_194 = {1{`RANDOM}};
  br_snapshots_5_9 = _RAND_194[5:0];
  _RAND_195 = {1{`RANDOM}};
  br_snapshots_5_10 = _RAND_195[5:0];
  _RAND_196 = {1{`RANDOM}};
  br_snapshots_5_11 = _RAND_196[5:0];
  _RAND_197 = {1{`RANDOM}};
  br_snapshots_5_12 = _RAND_197[5:0];
  _RAND_198 = {1{`RANDOM}};
  br_snapshots_5_13 = _RAND_198[5:0];
  _RAND_199 = {1{`RANDOM}};
  br_snapshots_5_14 = _RAND_199[5:0];
  _RAND_200 = {1{`RANDOM}};
  br_snapshots_5_15 = _RAND_200[5:0];
  _RAND_201 = {1{`RANDOM}};
  br_snapshots_5_16 = _RAND_201[5:0];
  _RAND_202 = {1{`RANDOM}};
  br_snapshots_5_17 = _RAND_202[5:0];
  _RAND_203 = {1{`RANDOM}};
  br_snapshots_5_18 = _RAND_203[5:0];
  _RAND_204 = {1{`RANDOM}};
  br_snapshots_5_19 = _RAND_204[5:0];
  _RAND_205 = {1{`RANDOM}};
  br_snapshots_5_20 = _RAND_205[5:0];
  _RAND_206 = {1{`RANDOM}};
  br_snapshots_5_21 = _RAND_206[5:0];
  _RAND_207 = {1{`RANDOM}};
  br_snapshots_5_22 = _RAND_207[5:0];
  _RAND_208 = {1{`RANDOM}};
  br_snapshots_5_23 = _RAND_208[5:0];
  _RAND_209 = {1{`RANDOM}};
  br_snapshots_5_24 = _RAND_209[5:0];
  _RAND_210 = {1{`RANDOM}};
  br_snapshots_5_25 = _RAND_210[5:0];
  _RAND_211 = {1{`RANDOM}};
  br_snapshots_5_26 = _RAND_211[5:0];
  _RAND_212 = {1{`RANDOM}};
  br_snapshots_5_27 = _RAND_212[5:0];
  _RAND_213 = {1{`RANDOM}};
  br_snapshots_5_28 = _RAND_213[5:0];
  _RAND_214 = {1{`RANDOM}};
  br_snapshots_5_29 = _RAND_214[5:0];
  _RAND_215 = {1{`RANDOM}};
  br_snapshots_5_30 = _RAND_215[5:0];
  _RAND_216 = {1{`RANDOM}};
  br_snapshots_5_31 = _RAND_216[5:0];
  _RAND_217 = {1{`RANDOM}};
  br_snapshots_6_1 = _RAND_217[5:0];
  _RAND_218 = {1{`RANDOM}};
  br_snapshots_6_2 = _RAND_218[5:0];
  _RAND_219 = {1{`RANDOM}};
  br_snapshots_6_3 = _RAND_219[5:0];
  _RAND_220 = {1{`RANDOM}};
  br_snapshots_6_4 = _RAND_220[5:0];
  _RAND_221 = {1{`RANDOM}};
  br_snapshots_6_5 = _RAND_221[5:0];
  _RAND_222 = {1{`RANDOM}};
  br_snapshots_6_6 = _RAND_222[5:0];
  _RAND_223 = {1{`RANDOM}};
  br_snapshots_6_7 = _RAND_223[5:0];
  _RAND_224 = {1{`RANDOM}};
  br_snapshots_6_8 = _RAND_224[5:0];
  _RAND_225 = {1{`RANDOM}};
  br_snapshots_6_9 = _RAND_225[5:0];
  _RAND_226 = {1{`RANDOM}};
  br_snapshots_6_10 = _RAND_226[5:0];
  _RAND_227 = {1{`RANDOM}};
  br_snapshots_6_11 = _RAND_227[5:0];
  _RAND_228 = {1{`RANDOM}};
  br_snapshots_6_12 = _RAND_228[5:0];
  _RAND_229 = {1{`RANDOM}};
  br_snapshots_6_13 = _RAND_229[5:0];
  _RAND_230 = {1{`RANDOM}};
  br_snapshots_6_14 = _RAND_230[5:0];
  _RAND_231 = {1{`RANDOM}};
  br_snapshots_6_15 = _RAND_231[5:0];
  _RAND_232 = {1{`RANDOM}};
  br_snapshots_6_16 = _RAND_232[5:0];
  _RAND_233 = {1{`RANDOM}};
  br_snapshots_6_17 = _RAND_233[5:0];
  _RAND_234 = {1{`RANDOM}};
  br_snapshots_6_18 = _RAND_234[5:0];
  _RAND_235 = {1{`RANDOM}};
  br_snapshots_6_19 = _RAND_235[5:0];
  _RAND_236 = {1{`RANDOM}};
  br_snapshots_6_20 = _RAND_236[5:0];
  _RAND_237 = {1{`RANDOM}};
  br_snapshots_6_21 = _RAND_237[5:0];
  _RAND_238 = {1{`RANDOM}};
  br_snapshots_6_22 = _RAND_238[5:0];
  _RAND_239 = {1{`RANDOM}};
  br_snapshots_6_23 = _RAND_239[5:0];
  _RAND_240 = {1{`RANDOM}};
  br_snapshots_6_24 = _RAND_240[5:0];
  _RAND_241 = {1{`RANDOM}};
  br_snapshots_6_25 = _RAND_241[5:0];
  _RAND_242 = {1{`RANDOM}};
  br_snapshots_6_26 = _RAND_242[5:0];
  _RAND_243 = {1{`RANDOM}};
  br_snapshots_6_27 = _RAND_243[5:0];
  _RAND_244 = {1{`RANDOM}};
  br_snapshots_6_28 = _RAND_244[5:0];
  _RAND_245 = {1{`RANDOM}};
  br_snapshots_6_29 = _RAND_245[5:0];
  _RAND_246 = {1{`RANDOM}};
  br_snapshots_6_30 = _RAND_246[5:0];
  _RAND_247 = {1{`RANDOM}};
  br_snapshots_6_31 = _RAND_247[5:0];
  _RAND_248 = {1{`RANDOM}};
  br_snapshots_7_1 = _RAND_248[5:0];
  _RAND_249 = {1{`RANDOM}};
  br_snapshots_7_2 = _RAND_249[5:0];
  _RAND_250 = {1{`RANDOM}};
  br_snapshots_7_3 = _RAND_250[5:0];
  _RAND_251 = {1{`RANDOM}};
  br_snapshots_7_4 = _RAND_251[5:0];
  _RAND_252 = {1{`RANDOM}};
  br_snapshots_7_5 = _RAND_252[5:0];
  _RAND_253 = {1{`RANDOM}};
  br_snapshots_7_6 = _RAND_253[5:0];
  _RAND_254 = {1{`RANDOM}};
  br_snapshots_7_7 = _RAND_254[5:0];
  _RAND_255 = {1{`RANDOM}};
  br_snapshots_7_8 = _RAND_255[5:0];
  _RAND_256 = {1{`RANDOM}};
  br_snapshots_7_9 = _RAND_256[5:0];
  _RAND_257 = {1{`RANDOM}};
  br_snapshots_7_10 = _RAND_257[5:0];
  _RAND_258 = {1{`RANDOM}};
  br_snapshots_7_11 = _RAND_258[5:0];
  _RAND_259 = {1{`RANDOM}};
  br_snapshots_7_12 = _RAND_259[5:0];
  _RAND_260 = {1{`RANDOM}};
  br_snapshots_7_13 = _RAND_260[5:0];
  _RAND_261 = {1{`RANDOM}};
  br_snapshots_7_14 = _RAND_261[5:0];
  _RAND_262 = {1{`RANDOM}};
  br_snapshots_7_15 = _RAND_262[5:0];
  _RAND_263 = {1{`RANDOM}};
  br_snapshots_7_16 = _RAND_263[5:0];
  _RAND_264 = {1{`RANDOM}};
  br_snapshots_7_17 = _RAND_264[5:0];
  _RAND_265 = {1{`RANDOM}};
  br_snapshots_7_18 = _RAND_265[5:0];
  _RAND_266 = {1{`RANDOM}};
  br_snapshots_7_19 = _RAND_266[5:0];
  _RAND_267 = {1{`RANDOM}};
  br_snapshots_7_20 = _RAND_267[5:0];
  _RAND_268 = {1{`RANDOM}};
  br_snapshots_7_21 = _RAND_268[5:0];
  _RAND_269 = {1{`RANDOM}};
  br_snapshots_7_22 = _RAND_269[5:0];
  _RAND_270 = {1{`RANDOM}};
  br_snapshots_7_23 = _RAND_270[5:0];
  _RAND_271 = {1{`RANDOM}};
  br_snapshots_7_24 = _RAND_271[5:0];
  _RAND_272 = {1{`RANDOM}};
  br_snapshots_7_25 = _RAND_272[5:0];
  _RAND_273 = {1{`RANDOM}};
  br_snapshots_7_26 = _RAND_273[5:0];
  _RAND_274 = {1{`RANDOM}};
  br_snapshots_7_27 = _RAND_274[5:0];
  _RAND_275 = {1{`RANDOM}};
  br_snapshots_7_28 = _RAND_275[5:0];
  _RAND_276 = {1{`RANDOM}};
  br_snapshots_7_29 = _RAND_276[5:0];
  _RAND_277 = {1{`RANDOM}};
  br_snapshots_7_30 = _RAND_277[5:0];
  _RAND_278 = {1{`RANDOM}};
  br_snapshots_7_31 = _RAND_278[5:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
