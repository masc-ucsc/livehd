module BranchMaskGenerationLogic(
  input         clock,
  input         reset,
  input         io_is_branch_0,
  input         io_will_fire_0,
  output [2:0]  io_br_tag_0,
  output [7:0]  io_br_mask_0,
  output        io_is_full_0,
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
  input         io_flush_pipeline,
  output [7:0]  io_debug_branch_mask
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [7:0] branch_mask; // @[decode.scala 741:28]
  wire [2:0] _GEN_0 = ~branch_mask[7] ? 3'h7 : 3'h0; // @[decode.scala 755:16 759:32 760:20]
  wire [7:0] _GEN_1 = ~branch_mask[7] ? 8'h80 : 8'h0; // @[decode.scala 756:18 759:32 761:22]
  wire [2:0] _GEN_2 = ~branch_mask[6] ? 3'h6 : _GEN_0; // @[decode.scala 759:32 760:20]
  wire [7:0] _GEN_3 = ~branch_mask[6] ? 8'h40 : _GEN_1; // @[decode.scala 759:32 761:22]
  wire [2:0] _GEN_4 = ~branch_mask[5] ? 3'h5 : _GEN_2; // @[decode.scala 759:32 760:20]
  wire [7:0] _GEN_5 = ~branch_mask[5] ? 8'h20 : _GEN_3; // @[decode.scala 759:32 761:22]
  wire [2:0] _GEN_6 = ~branch_mask[4] ? 3'h4 : _GEN_4; // @[decode.scala 759:32 760:20]
  wire [7:0] _GEN_7 = ~branch_mask[4] ? 8'h10 : _GEN_5; // @[decode.scala 759:32 761:22]
  wire [2:0] _GEN_8 = ~branch_mask[3] ? 3'h3 : _GEN_6; // @[decode.scala 759:32 760:20]
  wire [7:0] _GEN_9 = ~branch_mask[3] ? 8'h8 : _GEN_7; // @[decode.scala 759:32 761:22]
  wire [2:0] _GEN_10 = ~branch_mask[2] ? 3'h2 : _GEN_8; // @[decode.scala 759:32 760:20]
  wire [7:0] _GEN_11 = ~branch_mask[2] ? 8'h4 : _GEN_9; // @[decode.scala 759:32 761:22]
  wire [2:0] _GEN_12 = ~branch_mask[1] ? 3'h1 : _GEN_10; // @[decode.scala 759:32 760:20]
  wire [7:0] _GEN_13 = ~branch_mask[1] ? 8'h2 : _GEN_11; // @[decode.scala 759:32 761:22]
  wire [7:0] tag_masks_0 = ~branch_mask[0] ? 8'h1 : _GEN_13; // @[decode.scala 759:32 761:22]
  wire [7:0] _T_28 = tag_masks_0 | branch_mask; // @[decode.scala 766:55]
  wire [7:0] _T_29 = ~io_brupdate_b1_resolve_mask; // @[util.scala 89:23]
  wire [7:0] curr_mask = io_will_fire_0 ? _T_28 : branch_mask; // @[decode.scala 776:20]
  wire [7:0] _T_33 = io_brupdate_b2_mispredict ? io_brupdate_b2_uop_br_mask : 8'hff; // @[decode.scala 785:19]
  wire [7:0] _T_35 = curr_mask & _T_29; // @[util.scala 89:21]
  wire [7:0] _T_36 = _T_35 & _T_33; // @[decode.scala 788:57]
  assign io_br_tag_0 = ~branch_mask[0] ? 3'h0 : _GEN_12; // @[decode.scala 759:32 760:20]
  assign io_br_mask_0 = branch_mask & _T_29; // @[util.scala 89:21]
  assign io_is_full_0 = branch_mask == 8'hff & io_is_branch_0; // @[decode.scala 751:63]
  assign io_debug_branch_mask = branch_mask; // @[decode.scala 791:24]
  always @(posedge clock) begin
    if (reset) begin // @[decode.scala 741:28]
      branch_mask <= 8'h0; // @[decode.scala 741:28]
    end else if (io_flush_pipeline) begin // @[decode.scala 782:28]
      branch_mask <= 8'h0; // @[decode.scala 783:17]
    end else begin
      branch_mask <= _T_36; // @[decode.scala 788:17]
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
  branch_mask = _RAND_0[7:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
