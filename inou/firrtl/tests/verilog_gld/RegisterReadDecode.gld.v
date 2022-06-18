module RegisterReadDecode(
  input         clock,
  input         reset,
  input         io_iss_valid,
  input  [6:0]  io_iss_uop_uopc,
  input  [31:0] io_iss_uop_inst,
  input  [31:0] io_iss_uop_debug_inst,
  input         io_iss_uop_is_rvc,
  input  [39:0] io_iss_uop_debug_pc,
  input  [2:0]  io_iss_uop_iq_type,
  input  [9:0]  io_iss_uop_fu_code,
  input  [3:0]  io_iss_uop_ctrl_br_type,
  input  [1:0]  io_iss_uop_ctrl_op1_sel,
  input  [2:0]  io_iss_uop_ctrl_op2_sel,
  input  [2:0]  io_iss_uop_ctrl_imm_sel,
  input  [3:0]  io_iss_uop_ctrl_op_fcn,
  input         io_iss_uop_ctrl_fcn_dw,
  input  [2:0]  io_iss_uop_ctrl_csr_cmd,
  input         io_iss_uop_ctrl_is_load,
  input         io_iss_uop_ctrl_is_sta,
  input         io_iss_uop_ctrl_is_std,
  input  [1:0]  io_iss_uop_iw_state,
  input         io_iss_uop_iw_p1_poisoned,
  input         io_iss_uop_iw_p2_poisoned,
  input         io_iss_uop_is_br,
  input         io_iss_uop_is_jalr,
  input         io_iss_uop_is_jal,
  input         io_iss_uop_is_sfb,
  input  [7:0]  io_iss_uop_br_mask,
  input  [2:0]  io_iss_uop_br_tag,
  input  [3:0]  io_iss_uop_ftq_idx,
  input         io_iss_uop_edge_inst,
  input  [5:0]  io_iss_uop_pc_lob,
  input         io_iss_uop_taken,
  input  [19:0] io_iss_uop_imm_packed,
  input  [11:0] io_iss_uop_csr_addr,
  input  [4:0]  io_iss_uop_rob_idx,
  input  [2:0]  io_iss_uop_ldq_idx,
  input  [2:0]  io_iss_uop_stq_idx,
  input  [1:0]  io_iss_uop_rxq_idx,
  input  [5:0]  io_iss_uop_pdst,
  input  [5:0]  io_iss_uop_prs1,
  input  [5:0]  io_iss_uop_prs2,
  input  [5:0]  io_iss_uop_prs3,
  input  [3:0]  io_iss_uop_ppred,
  input         io_iss_uop_prs1_busy,
  input         io_iss_uop_prs2_busy,
  input         io_iss_uop_prs3_busy,
  input         io_iss_uop_ppred_busy,
  input  [5:0]  io_iss_uop_stale_pdst,
  input         io_iss_uop_exception,
  input  [63:0] io_iss_uop_exc_cause,
  input         io_iss_uop_bypassable,
  input  [4:0]  io_iss_uop_mem_cmd,
  input  [1:0]  io_iss_uop_mem_size,
  input         io_iss_uop_mem_signed,
  input         io_iss_uop_is_fence,
  input         io_iss_uop_is_fencei,
  input         io_iss_uop_is_amo,
  input         io_iss_uop_uses_ldq,
  input         io_iss_uop_uses_stq,
  input         io_iss_uop_is_sys_pc2epc,
  input         io_iss_uop_is_unique,
  input         io_iss_uop_flush_on_commit,
  input         io_iss_uop_ldst_is_rs1,
  input  [5:0]  io_iss_uop_ldst,
  input  [5:0]  io_iss_uop_lrs1,
  input  [5:0]  io_iss_uop_lrs2,
  input  [5:0]  io_iss_uop_lrs3,
  input         io_iss_uop_ldst_val,
  input  [1:0]  io_iss_uop_dst_rtype,
  input  [1:0]  io_iss_uop_lrs1_rtype,
  input  [1:0]  io_iss_uop_lrs2_rtype,
  input         io_iss_uop_frs3_en,
  input         io_iss_uop_fp_val,
  input         io_iss_uop_fp_single,
  input         io_iss_uop_xcpt_pf_if,
  input         io_iss_uop_xcpt_ae_if,
  input         io_iss_uop_xcpt_ma_if,
  input         io_iss_uop_bp_debug_if,
  input         io_iss_uop_bp_xcpt_if,
  input  [1:0]  io_iss_uop_debug_fsrc,
  input  [1:0]  io_iss_uop_debug_tsrc,
  output        io_rrd_valid,
  output [6:0]  io_rrd_uop_uopc,
  output [31:0] io_rrd_uop_inst,
  output [31:0] io_rrd_uop_debug_inst,
  output        io_rrd_uop_is_rvc,
  output [39:0] io_rrd_uop_debug_pc,
  output [2:0]  io_rrd_uop_iq_type,
  output [9:0]  io_rrd_uop_fu_code,
  output [3:0]  io_rrd_uop_ctrl_br_type,
  output [1:0]  io_rrd_uop_ctrl_op1_sel,
  output [2:0]  io_rrd_uop_ctrl_op2_sel,
  output [2:0]  io_rrd_uop_ctrl_imm_sel,
  output [3:0]  io_rrd_uop_ctrl_op_fcn,
  output        io_rrd_uop_ctrl_fcn_dw,
  output [2:0]  io_rrd_uop_ctrl_csr_cmd,
  output        io_rrd_uop_ctrl_is_load,
  output        io_rrd_uop_ctrl_is_sta,
  output        io_rrd_uop_ctrl_is_std,
  output [1:0]  io_rrd_uop_iw_state,
  output        io_rrd_uop_iw_p1_poisoned,
  output        io_rrd_uop_iw_p2_poisoned,
  output        io_rrd_uop_is_br,
  output        io_rrd_uop_is_jalr,
  output        io_rrd_uop_is_jal,
  output        io_rrd_uop_is_sfb,
  output [7:0]  io_rrd_uop_br_mask,
  output [2:0]  io_rrd_uop_br_tag,
  output [3:0]  io_rrd_uop_ftq_idx,
  output        io_rrd_uop_edge_inst,
  output [5:0]  io_rrd_uop_pc_lob,
  output        io_rrd_uop_taken,
  output [19:0] io_rrd_uop_imm_packed,
  output [11:0] io_rrd_uop_csr_addr,
  output [4:0]  io_rrd_uop_rob_idx,
  output [2:0]  io_rrd_uop_ldq_idx,
  output [2:0]  io_rrd_uop_stq_idx,
  output [1:0]  io_rrd_uop_rxq_idx,
  output [5:0]  io_rrd_uop_pdst,
  output [5:0]  io_rrd_uop_prs1,
  output [5:0]  io_rrd_uop_prs2,
  output [5:0]  io_rrd_uop_prs3,
  output [3:0]  io_rrd_uop_ppred,
  output        io_rrd_uop_prs1_busy,
  output        io_rrd_uop_prs2_busy,
  output        io_rrd_uop_prs3_busy,
  output        io_rrd_uop_ppred_busy,
  output [5:0]  io_rrd_uop_stale_pdst,
  output        io_rrd_uop_exception,
  output [63:0] io_rrd_uop_exc_cause,
  output        io_rrd_uop_bypassable,
  output [4:0]  io_rrd_uop_mem_cmd,
  output [1:0]  io_rrd_uop_mem_size,
  output        io_rrd_uop_mem_signed,
  output        io_rrd_uop_is_fence,
  output        io_rrd_uop_is_fencei,
  output        io_rrd_uop_is_amo,
  output        io_rrd_uop_uses_ldq,
  output        io_rrd_uop_uses_stq,
  output        io_rrd_uop_is_sys_pc2epc,
  output        io_rrd_uop_is_unique,
  output        io_rrd_uop_flush_on_commit,
  output        io_rrd_uop_ldst_is_rs1,
  output [5:0]  io_rrd_uop_ldst,
  output [5:0]  io_rrd_uop_lrs1,
  output [5:0]  io_rrd_uop_lrs2,
  output [5:0]  io_rrd_uop_lrs3,
  output        io_rrd_uop_ldst_val,
  output [1:0]  io_rrd_uop_dst_rtype,
  output [1:0]  io_rrd_uop_lrs1_rtype,
  output [1:0]  io_rrd_uop_lrs2_rtype,
  output        io_rrd_uop_frs3_en,
  output        io_rrd_uop_fp_val,
  output        io_rrd_uop_fp_single,
  output        io_rrd_uop_xcpt_pf_if,
  output        io_rrd_uop_xcpt_ae_if,
  output        io_rrd_uop_xcpt_ma_if,
  output        io_rrd_uop_bp_debug_if,
  output        io_rrd_uop_bp_xcpt_if,
  output [1:0]  io_rrd_uop_debug_fsrc,
  output [1:0]  io_rrd_uop_debug_tsrc
);
  wire  _T = io_rrd_uop_uopc == 7'h19; // @[Decode.scala 14:121]
  wire  _T_1 = io_rrd_uop_uopc == 7'h1a; // @[Decode.scala 14:121]
  wire  _T_2 = io_rrd_uop_uopc == 7'h1c; // @[Decode.scala 14:121]
  wire  _T_5 = _T | _T_1 | _T_2; // @[Decode.scala 15:30]
  wire [6:0] _T_6 = io_rrd_uop_uopc & 7'h7d; // @[Decode.scala 14:65]
  wire  _T_7 = _T_6 == 7'h18; // @[Decode.scala 14:121]
  wire  _T_8 = io_rrd_uop_uopc == 7'h1d; // @[Decode.scala 14:121]
  wire  _T_10 = _T_7 | _T_8; // @[Decode.scala 15:30]
  wire  _T_11 = io_rrd_uop_uopc == 7'h1b; // @[Decode.scala 14:121]
  wire [6:0] _T_12 = io_rrd_uop_uopc & 7'h7e; // @[Decode.scala 14:65]
  wire  _T_13 = _T_12 == 7'h1c; // @[Decode.scala 14:121]
  wire  _T_15 = _T_11 | _T_13; // @[Decode.scala 15:30]
  wire [1:0] _T_16 = {_T_10,_T_5}; // @[Cat.scala 29:58]
  wire [1:0] _T_17 = {1'h0,_T_15}; // @[Cat.scala 29:58]
  wire [6:0] _T_31 = io_rrd_uop_uopc & 7'h3f; // @[Decode.scala 14:65]
  wire  _T_32 = _T_31 == 7'hb; // @[Decode.scala 14:121]
  wire [6:0] _T_33 = io_rrd_uop_uopc & 7'h3e; // @[Decode.scala 14:65]
  wire  _T_34 = _T_33 == 7'hc; // @[Decode.scala 14:121]
  wire  _T_36 = _T_12 == 7'h2e; // @[Decode.scala 14:121]
  wire [6:0] _T_37 = io_rrd_uop_uopc & 7'h7c; // @[Decode.scala 14:65]
  wire  _T_38 = _T_37 == 7'h30; // @[Decode.scala 14:121]
  wire [6:0] _T_39 = io_rrd_uop_uopc & 7'h37; // @[Decode.scala 14:65]
  wire  _T_40 = _T_39 == 7'h6; // @[Decode.scala 14:121]
  wire [6:0] _T_41 = io_rrd_uop_uopc & 7'h3b; // @[Decode.scala 14:65]
  wire  _T_42 = _T_41 == 7'h13; // @[Decode.scala 14:121]
  wire [6:0] _T_43 = io_rrd_uop_uopc & 7'h6f; // @[Decode.scala 14:65]
  wire  _T_44 = _T_43 == 7'h6; // @[Decode.scala 14:121]
  wire  _T_51 = _T_32 | _T_34 | _T_36 | _T_38 | _T_40 | _T_42 | _T_44; // @[Decode.scala 15:30]
  wire  _T_53 = _T_33 == 7'h6; // @[Decode.scala 14:121]
  wire  _T_55 = _T_31 == 7'ha; // @[Decode.scala 14:121]
  wire  _T_57 = _T_31 == 7'hc; // @[Decode.scala 14:121]
  wire [6:0] _T_58 = io_rrd_uop_uopc & 7'h79; // @[Decode.scala 14:65]
  wire  _T_59 = _T_58 == 7'h10; // @[Decode.scala 14:121]
  wire  _T_61 = _T_41 == 7'h19; // @[Decode.scala 14:121]
  wire  _T_62 = io_rrd_uop_uopc == 7'h2d; // @[Decode.scala 14:121]
  wire  _T_64 = _T_12 == 7'h30; // @[Decode.scala 14:121]
  wire  _T_66 = _T_39 == 7'h13; // @[Decode.scala 14:121]
  wire  _T_68 = _T_39 == 7'h10; // @[Decode.scala 14:121]
  wire  _T_77 = _T_53 | _T_55 | _T_57 | _T_59 | _T_61 | _T_62 | _T_64 | _T_66 | _T_68; // @[Decode.scala 15:30]
  wire [6:0] _T_78 = io_rrd_uop_uopc & 7'h39; // @[Decode.scala 14:65]
  wire  _T_79 = _T_78 == 7'h11; // @[Decode.scala 14:121]
  wire [6:0] _T_80 = io_rrd_uop_uopc & 7'h5e; // @[Decode.scala 14:65]
  wire  _T_81 = _T_80 == 7'h12; // @[Decode.scala 14:121]
  wire [6:0] _T_82 = io_rrd_uop_uopc & 7'h36; // @[Decode.scala 14:65]
  wire  _T_83 = _T_82 == 7'h12; // @[Decode.scala 14:121]
  wire  _T_85 = _T_82 == 7'h14; // @[Decode.scala 14:121]
  wire [6:0] _T_86 = io_rrd_uop_uopc & 7'h3d; // @[Decode.scala 14:65]
  wire  _T_87 = _T_86 == 7'h8; // @[Decode.scala 14:121]
  wire  _T_89 = _T_41 == 7'h9; // @[Decode.scala 14:121]
  wire  _T_96 = _T_53 | _T_79 | _T_81 | _T_83 | _T_85 | _T_87 | _T_89; // @[Decode.scala 15:30]
  wire [6:0] _T_97 = io_rrd_uop_uopc & 7'h2f; // @[Decode.scala 14:65]
  wire  _T_98 = _T_97 == 7'h9; // @[Decode.scala 14:121]
  wire  _T_100 = _T_97 == 7'ha; // @[Decode.scala 14:121]
  wire  _T_102 = _T_97 == 7'hc; // @[Decode.scala 14:121]
  wire  _T_104 = _T_80 == 7'h10; // @[Decode.scala 14:121]
  wire [6:0] _T_105 = io_rrd_uop_uopc & 7'h7b; // @[Decode.scala 14:65]
  wire  _T_106 = _T_105 == 7'h12; // @[Decode.scala 14:121]
  wire [6:0] _T_107 = io_rrd_uop_uopc & 7'h3c; // @[Decode.scala 14:65]
  wire  _T_108 = _T_107 == 7'h18; // @[Decode.scala 14:121]
  wire [6:0] _T_109 = io_rrd_uop_uopc & 7'h3a; // @[Decode.scala 14:65]
  wire  _T_110 = _T_109 == 7'h18; // @[Decode.scala 14:121]
  wire  _T_118 = _T_98 | _T_100 | _T_102 | _T_104 | _T_106 | _T_108 | _T_110 | _T_62; // @[Decode.scala 15:30]
  wire [1:0] _T_119 = {_T_77,_T_51}; // @[Cat.scala 29:58]
  wire [1:0] _T_120 = {_T_118,_T_96}; // @[Cat.scala 29:58]
  wire  _T_123 = _T_105 == 7'h2b; // @[Decode.scala 14:121]
  wire  _T_125 = _T_37 == 7'h2c; // @[Decode.scala 14:121]
  wire  _T_128 = _T_123 | _T_125 | _T_38; // @[Decode.scala 15:30]
  wire [6:0] _T_130 = io_rrd_uop_uopc & 7'h1b; // @[Decode.scala 14:65]
  wire  _T_131 = _T_130 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_134 = io_rrd_uop_uopc & 7'h32; // @[Decode.scala 14:65]
  wire  _T_135 = _T_134 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_136 = io_rrd_uop_uopc & 7'h18; // @[Decode.scala 14:65]
  wire  _T_137 = _T_136 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_138 = io_rrd_uop_uopc & 7'h14; // @[Decode.scala 14:65]
  wire  _T_139 = _T_138 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_140 = io_rrd_uop_uopc & 7'h29; // @[Decode.scala 14:65]
  wire  _T_141 = _T_140 == 7'h20; // @[Decode.scala 14:121]
  wire [6:0] _T_142 = io_rrd_uop_uopc & 7'h23; // @[Decode.scala 14:65]
  wire  _T_143 = _T_142 == 7'h22; // @[Decode.scala 14:121]
  wire  _T_148 = _T_135 | _T_137 | _T_139 | _T_141 | _T_143; // @[Decode.scala 15:30]
  wire [6:0] _T_151 = io_rrd_uop_uopc & 7'h2b; // @[Decode.scala 14:65]
  wire  _T_152 = _T_151 == 7'h0; // @[Decode.scala 14:121]
  wire [6:0] _T_154 = io_rrd_uop_uopc & 7'h30; // @[Decode.scala 14:65]
  wire  _T_155 = _T_154 == 7'h10; // @[Decode.scala 14:121]
  wire  _T_157 = _T_152 | _T_155; // @[Decode.scala 15:30]
  wire [1:0] _T_158 = {1'h0,_T_157}; // @[Cat.scala 29:58]
  wire  _T_197 = io_rrd_uop_uopc == 7'h1; // @[func-unit-decode.scala 339:46]
  wire  _T_199 = io_rrd_uop_uopc == 7'h43; // @[func-unit-decode.scala 340:76]
  assign io_rrd_valid = io_iss_valid; // @[func-unit-decode.scala 356:16]
  assign io_rrd_uop_uopc = io_iss_uop_uopc; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_inst = io_iss_uop_inst; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_debug_inst = io_iss_uop_debug_inst; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_rvc = io_iss_uop_is_rvc; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_debug_pc = io_iss_uop_debug_pc; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_iq_type = io_iss_uop_iq_type; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_fu_code = io_iss_uop_fu_code; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ctrl_br_type = {_T_17,_T_16}; // @[Cat.scala 29:58]
  assign io_rrd_uop_ctrl_op1_sel = {1'h0,_T_131}; // @[Cat.scala 29:58]
  assign io_rrd_uop_ctrl_op2_sel = {2'h0,_T_148}; // @[Cat.scala 29:58]
  assign io_rrd_uop_ctrl_imm_sel = {_T_158,_T_152}; // @[Cat.scala 29:58]
  assign io_rrd_uop_ctrl_op_fcn = {_T_120,_T_119}; // @[Cat.scala 29:58]
  assign io_rrd_uop_ctrl_fcn_dw = ~_T_128; // @[Decode.scala 40:35]
  assign io_rrd_uop_ctrl_csr_cmd = 3'h0; // @[func-unit-decode.scala 349:33]
  assign io_rrd_uop_ctrl_is_load = io_rrd_uop_uopc == 7'h1; // @[func-unit-decode.scala 339:46]
  assign io_rrd_uop_ctrl_is_sta = io_rrd_uop_uopc == 7'h2 | io_rrd_uop_uopc == 7'h43; // @[func-unit-decode.scala 340:57]
  assign io_rrd_uop_ctrl_is_std = io_rrd_uop_uopc == 7'h3 | io_rrd_uop_ctrl_is_sta & io_rrd_uop_lrs2_rtype == 2'h0; // @[func-unit-decode.scala 341:57]
  assign io_rrd_uop_iw_state = io_iss_uop_iw_state; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_iw_p1_poisoned = io_iss_uop_iw_p1_poisoned; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_iw_p2_poisoned = io_iss_uop_iw_p2_poisoned; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_br = io_iss_uop_is_br; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_jalr = io_iss_uop_is_jalr; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_jal = io_iss_uop_is_jal; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_sfb = io_iss_uop_is_sfb; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_br_mask = io_iss_uop_br_mask; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_br_tag = io_iss_uop_br_tag; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ftq_idx = io_iss_uop_ftq_idx; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_edge_inst = io_iss_uop_edge_inst; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_pc_lob = io_iss_uop_pc_lob; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_taken = io_iss_uop_taken; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_imm_packed = _T_199 | _T_197 & io_rrd_uop_mem_cmd == 5'h6 ? 20'h0 : io_iss_uop_imm_packed; // @[func-unit-decode.scala 343:103 320:16 344:27]
  assign io_rrd_uop_csr_addr = io_iss_uop_csr_addr; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_rob_idx = io_iss_uop_rob_idx; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ldq_idx = io_iss_uop_ldq_idx; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_stq_idx = io_iss_uop_stq_idx; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_rxq_idx = io_iss_uop_rxq_idx; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_pdst = io_iss_uop_pdst; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_prs1 = io_iss_uop_prs1; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_prs2 = io_iss_uop_prs2; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_prs3 = io_iss_uop_prs3; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ppred = io_iss_uop_ppred; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_prs1_busy = io_iss_uop_prs1_busy; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_prs2_busy = io_iss_uop_prs2_busy; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_prs3_busy = io_iss_uop_prs3_busy; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ppred_busy = io_iss_uop_ppred_busy; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_stale_pdst = io_iss_uop_stale_pdst; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_exception = io_iss_uop_exception; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_exc_cause = io_iss_uop_exc_cause; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_bypassable = io_iss_uop_bypassable; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_mem_cmd = io_iss_uop_mem_cmd; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_mem_size = io_iss_uop_mem_size; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_mem_signed = io_iss_uop_mem_signed; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_fence = io_iss_uop_is_fence; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_fencei = io_iss_uop_is_fencei; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_amo = io_iss_uop_is_amo; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_uses_ldq = io_iss_uop_uses_ldq; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_uses_stq = io_iss_uop_uses_stq; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_sys_pc2epc = io_iss_uop_is_sys_pc2epc; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_is_unique = io_iss_uop_is_unique; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_flush_on_commit = io_iss_uop_flush_on_commit; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ldst_is_rs1 = io_iss_uop_ldst_is_rs1; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ldst = io_iss_uop_ldst; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_lrs1 = io_iss_uop_lrs1; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_lrs2 = io_iss_uop_lrs2; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_lrs3 = io_iss_uop_lrs3; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_ldst_val = io_iss_uop_ldst_val; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_dst_rtype = io_iss_uop_dst_rtype; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_lrs1_rtype = io_iss_uop_lrs1_rtype; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_lrs2_rtype = io_iss_uop_lrs2_rtype; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_frs3_en = io_iss_uop_frs3_en; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_fp_val = io_iss_uop_fp_val; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_fp_single = io_iss_uop_fp_single; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_xcpt_pf_if = io_iss_uop_xcpt_pf_if; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_xcpt_ae_if = io_iss_uop_xcpt_ae_if; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_xcpt_ma_if = io_iss_uop_xcpt_ma_if; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_bp_debug_if = io_iss_uop_bp_debug_if; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_bp_xcpt_if = io_iss_uop_bp_xcpt_if; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_debug_fsrc = io_iss_uop_debug_fsrc; // @[func-unit-decode.scala 320:16]
  assign io_rrd_uop_debug_tsrc = io_iss_uop_debug_tsrc; // @[func-unit-decode.scala 320:16]
endmodule
