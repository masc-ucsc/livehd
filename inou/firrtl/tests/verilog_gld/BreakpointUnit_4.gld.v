module BreakpointUnit_4(
  input         clock,
  input         reset,
  input         io_status_debug,
  input         io_status_cease,
  input         io_status_wfi,
  input  [31:0] io_status_isa,
  input  [1:0]  io_status_dprv,
  input  [1:0]  io_status_prv,
  input         io_status_sd,
  input  [26:0] io_status_zero2,
  input  [1:0]  io_status_sxl,
  input  [1:0]  io_status_uxl,
  input         io_status_sd_rv32,
  input  [7:0]  io_status_zero1,
  input         io_status_tsr,
  input         io_status_tw,
  input         io_status_tvm,
  input         io_status_mxr,
  input         io_status_sum,
  input         io_status_mprv,
  input  [1:0]  io_status_xs,
  input  [1:0]  io_status_fs,
  input  [1:0]  io_status_mpp,
  input  [1:0]  io_status_vs,
  input         io_status_spp,
  input         io_status_mpie,
  input         io_status_hpie,
  input         io_status_spie,
  input         io_status_upie,
  input         io_status_mie,
  input         io_status_hie,
  input         io_status_sie,
  input         io_status_uie,
  input  [38:0] io_pc,
  input  [38:0] io_ea,
  output        io_xcpt_if,
  output        io_xcpt_ld,
  output        io_xcpt_st,
  output        io_debug_if,
  output        io_debug_ld,
  output        io_debug_st
);
  assign io_xcpt_if = 1'h0; // @[Breakpoint.scala 74:14]
  assign io_xcpt_ld = 1'h0; // @[Breakpoint.scala 75:14]
  assign io_xcpt_st = 1'h0; // @[Breakpoint.scala 76:14]
  assign io_debug_if = 1'h0; // @[Breakpoint.scala 77:15]
  assign io_debug_ld = 1'h0; // @[Breakpoint.scala 78:15]
  assign io_debug_st = 1'h0; // @[Breakpoint.scala 79:15]
endmodule
