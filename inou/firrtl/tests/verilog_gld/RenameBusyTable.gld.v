module RenameBusyTable(
  input         clock,
  input         reset,
  input  [6:0]  io_ren_uops_0_uopc,
  input  [31:0] io_ren_uops_0_inst,
  input  [31:0] io_ren_uops_0_debug_inst,
  input         io_ren_uops_0_is_rvc,
  input  [39:0] io_ren_uops_0_debug_pc,
  input  [2:0]  io_ren_uops_0_iq_type,
  input  [9:0]  io_ren_uops_0_fu_code,
  input  [3:0]  io_ren_uops_0_ctrl_br_type,
  input  [1:0]  io_ren_uops_0_ctrl_op1_sel,
  input  [2:0]  io_ren_uops_0_ctrl_op2_sel,
  input  [2:0]  io_ren_uops_0_ctrl_imm_sel,
  input  [3:0]  io_ren_uops_0_ctrl_op_fcn,
  input         io_ren_uops_0_ctrl_fcn_dw,
  input  [2:0]  io_ren_uops_0_ctrl_csr_cmd,
  input         io_ren_uops_0_ctrl_is_load,
  input         io_ren_uops_0_ctrl_is_sta,
  input         io_ren_uops_0_ctrl_is_std,
  input  [1:0]  io_ren_uops_0_iw_state,
  input         io_ren_uops_0_iw_p1_poisoned,
  input         io_ren_uops_0_iw_p2_poisoned,
  input         io_ren_uops_0_is_br,
  input         io_ren_uops_0_is_jalr,
  input         io_ren_uops_0_is_jal,
  input         io_ren_uops_0_is_sfb,
  input  [7:0]  io_ren_uops_0_br_mask,
  input  [2:0]  io_ren_uops_0_br_tag,
  input  [3:0]  io_ren_uops_0_ftq_idx,
  input         io_ren_uops_0_edge_inst,
  input  [5:0]  io_ren_uops_0_pc_lob,
  input         io_ren_uops_0_taken,
  input  [19:0] io_ren_uops_0_imm_packed,
  input  [11:0] io_ren_uops_0_csr_addr,
  input  [4:0]  io_ren_uops_0_rob_idx,
  input  [2:0]  io_ren_uops_0_ldq_idx,
  input  [2:0]  io_ren_uops_0_stq_idx,
  input  [1:0]  io_ren_uops_0_rxq_idx,
  input  [5:0]  io_ren_uops_0_pdst,
  input  [5:0]  io_ren_uops_0_prs1,
  input  [5:0]  io_ren_uops_0_prs2,
  input  [5:0]  io_ren_uops_0_prs3,
  input  [3:0]  io_ren_uops_0_ppred,
  input         io_ren_uops_0_prs1_busy,
  input         io_ren_uops_0_prs2_busy,
  input         io_ren_uops_0_prs3_busy,
  input         io_ren_uops_0_ppred_busy,
  input  [5:0]  io_ren_uops_0_stale_pdst,
  input         io_ren_uops_0_exception,
  input  [63:0] io_ren_uops_0_exc_cause,
  input         io_ren_uops_0_bypassable,
  input  [4:0]  io_ren_uops_0_mem_cmd,
  input  [1:0]  io_ren_uops_0_mem_size,
  input         io_ren_uops_0_mem_signed,
  input         io_ren_uops_0_is_fence,
  input         io_ren_uops_0_is_fencei,
  input         io_ren_uops_0_is_amo,
  input         io_ren_uops_0_uses_ldq,
  input         io_ren_uops_0_uses_stq,
  input         io_ren_uops_0_is_sys_pc2epc,
  input         io_ren_uops_0_is_unique,
  input         io_ren_uops_0_flush_on_commit,
  input         io_ren_uops_0_ldst_is_rs1,
  input  [5:0]  io_ren_uops_0_ldst,
  input  [5:0]  io_ren_uops_0_lrs1,
  input  [5:0]  io_ren_uops_0_lrs2,
  input  [5:0]  io_ren_uops_0_lrs3,
  input         io_ren_uops_0_ldst_val,
  input  [1:0]  io_ren_uops_0_dst_rtype,
  input  [1:0]  io_ren_uops_0_lrs1_rtype,
  input  [1:0]  io_ren_uops_0_lrs2_rtype,
  input         io_ren_uops_0_frs3_en,
  input         io_ren_uops_0_fp_val,
  input         io_ren_uops_0_fp_single,
  input         io_ren_uops_0_xcpt_pf_if,
  input         io_ren_uops_0_xcpt_ae_if,
  input         io_ren_uops_0_xcpt_ma_if,
  input         io_ren_uops_0_bp_debug_if,
  input         io_ren_uops_0_bp_xcpt_if,
  input  [1:0]  io_ren_uops_0_debug_fsrc,
  input  [1:0]  io_ren_uops_0_debug_tsrc,
  output        io_busy_resps_0_prs1_busy,
  output        io_busy_resps_0_prs2_busy,
  output        io_busy_resps_0_prs3_busy,
  input         io_rebusy_reqs_0,
  input  [5:0]  io_wb_pdsts_0,
  input  [5:0]  io_wb_pdsts_1,
  input  [5:0]  io_wb_pdsts_2,
  input         io_wb_valids_0,
  input         io_wb_valids_1,
  input         io_wb_valids_2,
  output [51:0] io_debug_busytable
);
`ifdef RANDOMIZE_REG_INIT
  reg [63:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [51:0] busy_table; // @[rename-busytable.scala 48:27]
  wire [63:0] _T = 64'h1 << io_wb_pdsts_0; // @[OneHot.scala 58:35]
  wire [51:0] _T_2 = io_wb_valids_0 ? 52'hfffffffffffff : 52'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _GEN_0 = {{12'd0}, _T_2}; // @[rename-busytable.scala 51:48]
  wire [63:0] _T_3 = _T & _GEN_0; // @[rename-busytable.scala 51:48]
  wire [63:0] _T_4 = 64'h1 << io_wb_pdsts_1; // @[OneHot.scala 58:35]
  wire [51:0] _T_6 = io_wb_valids_1 ? 52'hfffffffffffff : 52'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _GEN_1 = {{12'd0}, _T_6}; // @[rename-busytable.scala 51:48]
  wire [63:0] _T_7 = _T_4 & _GEN_1; // @[rename-busytable.scala 51:48]
  wire [63:0] _T_8 = 64'h1 << io_wb_pdsts_2; // @[OneHot.scala 58:35]
  wire [51:0] _T_10 = io_wb_valids_2 ? 52'hfffffffffffff : 52'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _GEN_2 = {{12'd0}, _T_10}; // @[rename-busytable.scala 51:48]
  wire [63:0] _T_11 = _T_8 & _GEN_2; // @[rename-busytable.scala 51:48]
  wire [63:0] _T_12 = _T_3 | _T_7; // @[rename-busytable.scala 51:88]
  wire [63:0] _T_13 = _T_12 | _T_11; // @[rename-busytable.scala 51:88]
  wire [63:0] _T_14 = ~_T_13; // @[rename-busytable.scala 50:36]
  wire [63:0] _GEN_3 = {{12'd0}, busy_table}; // @[rename-busytable.scala 50:34]
  wire [63:0] busy_table_wb = _GEN_3 & _T_14; // @[rename-busytable.scala 50:34]
  wire [63:0] _T_15 = 64'h1 << io_ren_uops_0_pdst; // @[OneHot.scala 58:35]
  wire [51:0] _T_17 = io_rebusy_reqs_0 ? 52'hfffffffffffff : 52'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _GEN_4 = {{12'd0}, _T_17}; // @[rename-busytable.scala 54:49]
  wire [63:0] _T_18 = _T_15 & _GEN_4; // @[rename-busytable.scala 54:49]
  wire [63:0] busy_table_next = busy_table_wb | _T_18; // @[rename-busytable.scala 53:39]
  wire [51:0] _T_19 = busy_table >> io_ren_uops_0_prs1; // @[rename-busytable.scala 67:45]
  wire [51:0] _T_23 = busy_table >> io_ren_uops_0_prs2; // @[rename-busytable.scala 68:45]
  wire [63:0] _GEN_5 = reset ? 64'h0 : busy_table_next; // @[rename-busytable.scala 48:{27,27} 56:14]
  assign io_busy_resps_0_prs1_busy = _T_19[0]; // @[rename-busytable.scala 67:45]
  assign io_busy_resps_0_prs2_busy = _T_23[0]; // @[rename-busytable.scala 68:45]
  assign io_busy_resps_0_prs3_busy = 1'h0; // @[rename-busytable.scala 70:44]
  assign io_debug_busytable = busy_table; // @[rename-busytable.scala 73:22]
  always @(posedge clock) begin
    busy_table <= _GEN_5[51:0]; // @[rename-busytable.scala 48:{27,27} 56:14]
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
  _RAND_0 = {2{`RANDOM}};
  busy_table = _RAND_0[51:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
