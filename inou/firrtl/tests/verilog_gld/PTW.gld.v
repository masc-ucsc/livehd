module Arbiter_19(
  output        io_in_0_ready,
  input         io_in_0_valid,
  input         io_in_0_bits_valid,
  input  [26:0] io_in_0_bits_bits_addr,
  output        io_in_1_ready,
  input         io_in_1_valid,
  input         io_in_1_bits_valid,
  input  [26:0] io_in_1_bits_bits_addr,
  output        io_in_2_ready,
  input         io_in_2_valid,
  input         io_in_2_bits_valid,
  input  [26:0] io_in_2_bits_bits_addr,
  input         io_out_ready,
  output        io_out_valid,
  output        io_out_bits_valid,
  output [26:0] io_out_bits_bits_addr,
  output [1:0]  io_chosen
);
  wire [1:0] _GEN_0 = io_in_1_valid ? 2'h1 : 2'h2; // @[Arbiter.scala 123:13 126:27 127:17]
  wire [26:0] _GEN_1 = io_in_1_valid ? io_in_1_bits_bits_addr : io_in_2_bits_bits_addr; // @[Arbiter.scala 124:15 126:27 128:19]
  wire  _GEN_2 = io_in_1_valid ? io_in_1_bits_valid : io_in_2_bits_valid; // @[Arbiter.scala 124:15 126:27 128:19]
  wire  grant_1 = ~io_in_0_valid; // @[Arbiter.scala 31:78]
  wire  grant_2 = ~(io_in_0_valid | io_in_1_valid); // @[Arbiter.scala 31:78]
  assign io_in_0_ready = io_out_ready; // @[Arbiter.scala 134:19]
  assign io_in_1_ready = grant_1 & io_out_ready; // @[Arbiter.scala 134:19]
  assign io_in_2_ready = grant_2 & io_out_ready; // @[Arbiter.scala 134:19]
  assign io_out_valid = ~grant_2 | io_in_2_valid; // @[Arbiter.scala 135:31]
  assign io_out_bits_valid = io_in_0_valid ? io_in_0_bits_valid : _GEN_2; // @[Arbiter.scala 126:27 128:19]
  assign io_out_bits_bits_addr = io_in_0_valid ? io_in_0_bits_bits_addr : _GEN_1; // @[Arbiter.scala 126:27 128:19]
  assign io_chosen = io_in_0_valid ? 2'h0 : _GEN_0; // @[Arbiter.scala 126:27 127:17]
endmodule
module package_Anon_61(
  input  [53:0] io_x_ppn,
  input  [1:0]  io_x_reserved_for_software,
  input         io_x_d,
  input         io_x_a,
  input         io_x_g,
  input         io_x_u,
  input         io_x_x,
  input         io_x_w,
  input         io_x_r,
  input         io_x_v,
  output [53:0] io_y_ppn,
  output [1:0]  io_y_reserved_for_software,
  output        io_y_d,
  output        io_y_a,
  output        io_y_g,
  output        io_y_u,
  output        io_y_x,
  output        io_y_w,
  output        io_y_r,
  output        io_y_v
);
  assign io_y_ppn = io_x_ppn; // @[package.scala 218:12]
  assign io_y_reserved_for_software = io_x_reserved_for_software; // @[package.scala 218:12]
  assign io_y_d = io_x_d; // @[package.scala 218:12]
  assign io_y_a = io_x_a; // @[package.scala 218:12]
  assign io_y_g = io_x_g; // @[package.scala 218:12]
  assign io_y_u = io_x_u; // @[package.scala 218:12]
  assign io_y_x = io_x_x; // @[package.scala 218:12]
  assign io_y_w = io_x_w; // @[package.scala 218:12]
  assign io_y_r = io_x_r; // @[package.scala 218:12]
  assign io_y_v = io_x_v; // @[package.scala 218:12]
endmodule
module package_Anon_60(
  input  [2:0] io_x,
  output [2:0] io_y
);
  assign io_y = io_x; // @[package.scala 218:12]
endmodule
module PTW(
  input         clock,
  input         reset,
  output        io_requestor_0_req_ready,
  input         io_requestor_0_req_valid,
  input         io_requestor_0_req_bits_valid,
  input  [26:0] io_requestor_0_req_bits_bits_addr,
  output        io_requestor_0_resp_valid,
  output        io_requestor_0_resp_bits_ae,
  output [53:0] io_requestor_0_resp_bits_pte_ppn,
  output [1:0]  io_requestor_0_resp_bits_pte_reserved_for_software,
  output        io_requestor_0_resp_bits_pte_d,
  output        io_requestor_0_resp_bits_pte_a,
  output        io_requestor_0_resp_bits_pte_g,
  output        io_requestor_0_resp_bits_pte_u,
  output        io_requestor_0_resp_bits_pte_x,
  output        io_requestor_0_resp_bits_pte_w,
  output        io_requestor_0_resp_bits_pte_r,
  output        io_requestor_0_resp_bits_pte_v,
  output [1:0]  io_requestor_0_resp_bits_level,
  output        io_requestor_0_resp_bits_fragmented_superpage,
  output        io_requestor_0_resp_bits_homogeneous,
  output [3:0]  io_requestor_0_ptbr_mode,
  output [15:0] io_requestor_0_ptbr_asid,
  output [43:0] io_requestor_0_ptbr_ppn,
  output        io_requestor_0_status_debug,
  output        io_requestor_0_status_cease,
  output        io_requestor_0_status_wfi,
  output [31:0] io_requestor_0_status_isa,
  output [1:0]  io_requestor_0_status_dprv,
  output [1:0]  io_requestor_0_status_prv,
  output        io_requestor_0_status_sd,
  output [26:0] io_requestor_0_status_zero2,
  output [1:0]  io_requestor_0_status_sxl,
  output [1:0]  io_requestor_0_status_uxl,
  output        io_requestor_0_status_sd_rv32,
  output [7:0]  io_requestor_0_status_zero1,
  output        io_requestor_0_status_tsr,
  output        io_requestor_0_status_tw,
  output        io_requestor_0_status_tvm,
  output        io_requestor_0_status_mxr,
  output        io_requestor_0_status_sum,
  output        io_requestor_0_status_mprv,
  output [1:0]  io_requestor_0_status_xs,
  output [1:0]  io_requestor_0_status_fs,
  output [1:0]  io_requestor_0_status_mpp,
  output [1:0]  io_requestor_0_status_vs,
  output        io_requestor_0_status_spp,
  output        io_requestor_0_status_mpie,
  output        io_requestor_0_status_hpie,
  output        io_requestor_0_status_spie,
  output        io_requestor_0_status_upie,
  output        io_requestor_0_status_mie,
  output        io_requestor_0_status_hie,
  output        io_requestor_0_status_sie,
  output        io_requestor_0_status_uie,
  output        io_requestor_0_pmp_0_cfg_l,
  output [1:0]  io_requestor_0_pmp_0_cfg_res,
  output [1:0]  io_requestor_0_pmp_0_cfg_a,
  output        io_requestor_0_pmp_0_cfg_x,
  output        io_requestor_0_pmp_0_cfg_w,
  output        io_requestor_0_pmp_0_cfg_r,
  output [29:0] io_requestor_0_pmp_0_addr,
  output [31:0] io_requestor_0_pmp_0_mask,
  output        io_requestor_0_pmp_1_cfg_l,
  output [1:0]  io_requestor_0_pmp_1_cfg_res,
  output [1:0]  io_requestor_0_pmp_1_cfg_a,
  output        io_requestor_0_pmp_1_cfg_x,
  output        io_requestor_0_pmp_1_cfg_w,
  output        io_requestor_0_pmp_1_cfg_r,
  output [29:0] io_requestor_0_pmp_1_addr,
  output [31:0] io_requestor_0_pmp_1_mask,
  output        io_requestor_0_pmp_2_cfg_l,
  output [1:0]  io_requestor_0_pmp_2_cfg_res,
  output [1:0]  io_requestor_0_pmp_2_cfg_a,
  output        io_requestor_0_pmp_2_cfg_x,
  output        io_requestor_0_pmp_2_cfg_w,
  output        io_requestor_0_pmp_2_cfg_r,
  output [29:0] io_requestor_0_pmp_2_addr,
  output [31:0] io_requestor_0_pmp_2_mask,
  output        io_requestor_0_pmp_3_cfg_l,
  output [1:0]  io_requestor_0_pmp_3_cfg_res,
  output [1:0]  io_requestor_0_pmp_3_cfg_a,
  output        io_requestor_0_pmp_3_cfg_x,
  output        io_requestor_0_pmp_3_cfg_w,
  output        io_requestor_0_pmp_3_cfg_r,
  output [29:0] io_requestor_0_pmp_3_addr,
  output [31:0] io_requestor_0_pmp_3_mask,
  output        io_requestor_0_pmp_4_cfg_l,
  output [1:0]  io_requestor_0_pmp_4_cfg_res,
  output [1:0]  io_requestor_0_pmp_4_cfg_a,
  output        io_requestor_0_pmp_4_cfg_x,
  output        io_requestor_0_pmp_4_cfg_w,
  output        io_requestor_0_pmp_4_cfg_r,
  output [29:0] io_requestor_0_pmp_4_addr,
  output [31:0] io_requestor_0_pmp_4_mask,
  output        io_requestor_0_pmp_5_cfg_l,
  output [1:0]  io_requestor_0_pmp_5_cfg_res,
  output [1:0]  io_requestor_0_pmp_5_cfg_a,
  output        io_requestor_0_pmp_5_cfg_x,
  output        io_requestor_0_pmp_5_cfg_w,
  output        io_requestor_0_pmp_5_cfg_r,
  output [29:0] io_requestor_0_pmp_5_addr,
  output [31:0] io_requestor_0_pmp_5_mask,
  output        io_requestor_0_pmp_6_cfg_l,
  output [1:0]  io_requestor_0_pmp_6_cfg_res,
  output [1:0]  io_requestor_0_pmp_6_cfg_a,
  output        io_requestor_0_pmp_6_cfg_x,
  output        io_requestor_0_pmp_6_cfg_w,
  output        io_requestor_0_pmp_6_cfg_r,
  output [29:0] io_requestor_0_pmp_6_addr,
  output [31:0] io_requestor_0_pmp_6_mask,
  output        io_requestor_0_pmp_7_cfg_l,
  output [1:0]  io_requestor_0_pmp_7_cfg_res,
  output [1:0]  io_requestor_0_pmp_7_cfg_a,
  output        io_requestor_0_pmp_7_cfg_x,
  output        io_requestor_0_pmp_7_cfg_w,
  output        io_requestor_0_pmp_7_cfg_r,
  output [29:0] io_requestor_0_pmp_7_addr,
  output [31:0] io_requestor_0_pmp_7_mask,
  output        io_requestor_0_customCSRs_csrs_0_wen,
  output [63:0] io_requestor_0_customCSRs_csrs_0_wdata,
  output [63:0] io_requestor_0_customCSRs_csrs_0_value,
  output        io_requestor_1_req_ready,
  input         io_requestor_1_req_valid,
  input         io_requestor_1_req_bits_valid,
  input  [26:0] io_requestor_1_req_bits_bits_addr,
  output        io_requestor_1_resp_valid,
  output        io_requestor_1_resp_bits_ae,
  output [53:0] io_requestor_1_resp_bits_pte_ppn,
  output [1:0]  io_requestor_1_resp_bits_pte_reserved_for_software,
  output        io_requestor_1_resp_bits_pte_d,
  output        io_requestor_1_resp_bits_pte_a,
  output        io_requestor_1_resp_bits_pte_g,
  output        io_requestor_1_resp_bits_pte_u,
  output        io_requestor_1_resp_bits_pte_x,
  output        io_requestor_1_resp_bits_pte_w,
  output        io_requestor_1_resp_bits_pte_r,
  output        io_requestor_1_resp_bits_pte_v,
  output [1:0]  io_requestor_1_resp_bits_level,
  output        io_requestor_1_resp_bits_fragmented_superpage,
  output        io_requestor_1_resp_bits_homogeneous,
  output [3:0]  io_requestor_1_ptbr_mode,
  output [15:0] io_requestor_1_ptbr_asid,
  output [43:0] io_requestor_1_ptbr_ppn,
  output        io_requestor_1_status_debug,
  output        io_requestor_1_status_cease,
  output        io_requestor_1_status_wfi,
  output [31:0] io_requestor_1_status_isa,
  output [1:0]  io_requestor_1_status_dprv,
  output [1:0]  io_requestor_1_status_prv,
  output        io_requestor_1_status_sd,
  output [26:0] io_requestor_1_status_zero2,
  output [1:0]  io_requestor_1_status_sxl,
  output [1:0]  io_requestor_1_status_uxl,
  output        io_requestor_1_status_sd_rv32,
  output [7:0]  io_requestor_1_status_zero1,
  output        io_requestor_1_status_tsr,
  output        io_requestor_1_status_tw,
  output        io_requestor_1_status_tvm,
  output        io_requestor_1_status_mxr,
  output        io_requestor_1_status_sum,
  output        io_requestor_1_status_mprv,
  output [1:0]  io_requestor_1_status_xs,
  output [1:0]  io_requestor_1_status_fs,
  output [1:0]  io_requestor_1_status_mpp,
  output [1:0]  io_requestor_1_status_vs,
  output        io_requestor_1_status_spp,
  output        io_requestor_1_status_mpie,
  output        io_requestor_1_status_hpie,
  output        io_requestor_1_status_spie,
  output        io_requestor_1_status_upie,
  output        io_requestor_1_status_mie,
  output        io_requestor_1_status_hie,
  output        io_requestor_1_status_sie,
  output        io_requestor_1_status_uie,
  output        io_requestor_1_pmp_0_cfg_l,
  output [1:0]  io_requestor_1_pmp_0_cfg_res,
  output [1:0]  io_requestor_1_pmp_0_cfg_a,
  output        io_requestor_1_pmp_0_cfg_x,
  output        io_requestor_1_pmp_0_cfg_w,
  output        io_requestor_1_pmp_0_cfg_r,
  output [29:0] io_requestor_1_pmp_0_addr,
  output [31:0] io_requestor_1_pmp_0_mask,
  output        io_requestor_1_pmp_1_cfg_l,
  output [1:0]  io_requestor_1_pmp_1_cfg_res,
  output [1:0]  io_requestor_1_pmp_1_cfg_a,
  output        io_requestor_1_pmp_1_cfg_x,
  output        io_requestor_1_pmp_1_cfg_w,
  output        io_requestor_1_pmp_1_cfg_r,
  output [29:0] io_requestor_1_pmp_1_addr,
  output [31:0] io_requestor_1_pmp_1_mask,
  output        io_requestor_1_pmp_2_cfg_l,
  output [1:0]  io_requestor_1_pmp_2_cfg_res,
  output [1:0]  io_requestor_1_pmp_2_cfg_a,
  output        io_requestor_1_pmp_2_cfg_x,
  output        io_requestor_1_pmp_2_cfg_w,
  output        io_requestor_1_pmp_2_cfg_r,
  output [29:0] io_requestor_1_pmp_2_addr,
  output [31:0] io_requestor_1_pmp_2_mask,
  output        io_requestor_1_pmp_3_cfg_l,
  output [1:0]  io_requestor_1_pmp_3_cfg_res,
  output [1:0]  io_requestor_1_pmp_3_cfg_a,
  output        io_requestor_1_pmp_3_cfg_x,
  output        io_requestor_1_pmp_3_cfg_w,
  output        io_requestor_1_pmp_3_cfg_r,
  output [29:0] io_requestor_1_pmp_3_addr,
  output [31:0] io_requestor_1_pmp_3_mask,
  output        io_requestor_1_pmp_4_cfg_l,
  output [1:0]  io_requestor_1_pmp_4_cfg_res,
  output [1:0]  io_requestor_1_pmp_4_cfg_a,
  output        io_requestor_1_pmp_4_cfg_x,
  output        io_requestor_1_pmp_4_cfg_w,
  output        io_requestor_1_pmp_4_cfg_r,
  output [29:0] io_requestor_1_pmp_4_addr,
  output [31:0] io_requestor_1_pmp_4_mask,
  output        io_requestor_1_pmp_5_cfg_l,
  output [1:0]  io_requestor_1_pmp_5_cfg_res,
  output [1:0]  io_requestor_1_pmp_5_cfg_a,
  output        io_requestor_1_pmp_5_cfg_x,
  output        io_requestor_1_pmp_5_cfg_w,
  output        io_requestor_1_pmp_5_cfg_r,
  output [29:0] io_requestor_1_pmp_5_addr,
  output [31:0] io_requestor_1_pmp_5_mask,
  output        io_requestor_1_pmp_6_cfg_l,
  output [1:0]  io_requestor_1_pmp_6_cfg_res,
  output [1:0]  io_requestor_1_pmp_6_cfg_a,
  output        io_requestor_1_pmp_6_cfg_x,
  output        io_requestor_1_pmp_6_cfg_w,
  output        io_requestor_1_pmp_6_cfg_r,
  output [29:0] io_requestor_1_pmp_6_addr,
  output [31:0] io_requestor_1_pmp_6_mask,
  output        io_requestor_1_pmp_7_cfg_l,
  output [1:0]  io_requestor_1_pmp_7_cfg_res,
  output [1:0]  io_requestor_1_pmp_7_cfg_a,
  output        io_requestor_1_pmp_7_cfg_x,
  output        io_requestor_1_pmp_7_cfg_w,
  output        io_requestor_1_pmp_7_cfg_r,
  output [29:0] io_requestor_1_pmp_7_addr,
  output [31:0] io_requestor_1_pmp_7_mask,
  output        io_requestor_1_customCSRs_csrs_0_wen,
  output [63:0] io_requestor_1_customCSRs_csrs_0_wdata,
  output [63:0] io_requestor_1_customCSRs_csrs_0_value,
  output        io_requestor_2_req_ready,
  input         io_requestor_2_req_valid,
  input         io_requestor_2_req_bits_valid,
  input  [26:0] io_requestor_2_req_bits_bits_addr,
  output        io_requestor_2_resp_valid,
  output        io_requestor_2_resp_bits_ae,
  output [53:0] io_requestor_2_resp_bits_pte_ppn,
  output [1:0]  io_requestor_2_resp_bits_pte_reserved_for_software,
  output        io_requestor_2_resp_bits_pte_d,
  output        io_requestor_2_resp_bits_pte_a,
  output        io_requestor_2_resp_bits_pte_g,
  output        io_requestor_2_resp_bits_pte_u,
  output        io_requestor_2_resp_bits_pte_x,
  output        io_requestor_2_resp_bits_pte_w,
  output        io_requestor_2_resp_bits_pte_r,
  output        io_requestor_2_resp_bits_pte_v,
  output [1:0]  io_requestor_2_resp_bits_level,
  output        io_requestor_2_resp_bits_fragmented_superpage,
  output        io_requestor_2_resp_bits_homogeneous,
  output [3:0]  io_requestor_2_ptbr_mode,
  output [15:0] io_requestor_2_ptbr_asid,
  output [43:0] io_requestor_2_ptbr_ppn,
  output        io_requestor_2_status_debug,
  output        io_requestor_2_status_cease,
  output        io_requestor_2_status_wfi,
  output [31:0] io_requestor_2_status_isa,
  output [1:0]  io_requestor_2_status_dprv,
  output [1:0]  io_requestor_2_status_prv,
  output        io_requestor_2_status_sd,
  output [26:0] io_requestor_2_status_zero2,
  output [1:0]  io_requestor_2_status_sxl,
  output [1:0]  io_requestor_2_status_uxl,
  output        io_requestor_2_status_sd_rv32,
  output [7:0]  io_requestor_2_status_zero1,
  output        io_requestor_2_status_tsr,
  output        io_requestor_2_status_tw,
  output        io_requestor_2_status_tvm,
  output        io_requestor_2_status_mxr,
  output        io_requestor_2_status_sum,
  output        io_requestor_2_status_mprv,
  output [1:0]  io_requestor_2_status_xs,
  output [1:0]  io_requestor_2_status_fs,
  output [1:0]  io_requestor_2_status_mpp,
  output [1:0]  io_requestor_2_status_vs,
  output        io_requestor_2_status_spp,
  output        io_requestor_2_status_mpie,
  output        io_requestor_2_status_hpie,
  output        io_requestor_2_status_spie,
  output        io_requestor_2_status_upie,
  output        io_requestor_2_status_mie,
  output        io_requestor_2_status_hie,
  output        io_requestor_2_status_sie,
  output        io_requestor_2_status_uie,
  output        io_requestor_2_pmp_0_cfg_l,
  output [1:0]  io_requestor_2_pmp_0_cfg_res,
  output [1:0]  io_requestor_2_pmp_0_cfg_a,
  output        io_requestor_2_pmp_0_cfg_x,
  output        io_requestor_2_pmp_0_cfg_w,
  output        io_requestor_2_pmp_0_cfg_r,
  output [29:0] io_requestor_2_pmp_0_addr,
  output [31:0] io_requestor_2_pmp_0_mask,
  output        io_requestor_2_pmp_1_cfg_l,
  output [1:0]  io_requestor_2_pmp_1_cfg_res,
  output [1:0]  io_requestor_2_pmp_1_cfg_a,
  output        io_requestor_2_pmp_1_cfg_x,
  output        io_requestor_2_pmp_1_cfg_w,
  output        io_requestor_2_pmp_1_cfg_r,
  output [29:0] io_requestor_2_pmp_1_addr,
  output [31:0] io_requestor_2_pmp_1_mask,
  output        io_requestor_2_pmp_2_cfg_l,
  output [1:0]  io_requestor_2_pmp_2_cfg_res,
  output [1:0]  io_requestor_2_pmp_2_cfg_a,
  output        io_requestor_2_pmp_2_cfg_x,
  output        io_requestor_2_pmp_2_cfg_w,
  output        io_requestor_2_pmp_2_cfg_r,
  output [29:0] io_requestor_2_pmp_2_addr,
  output [31:0] io_requestor_2_pmp_2_mask,
  output        io_requestor_2_pmp_3_cfg_l,
  output [1:0]  io_requestor_2_pmp_3_cfg_res,
  output [1:0]  io_requestor_2_pmp_3_cfg_a,
  output        io_requestor_2_pmp_3_cfg_x,
  output        io_requestor_2_pmp_3_cfg_w,
  output        io_requestor_2_pmp_3_cfg_r,
  output [29:0] io_requestor_2_pmp_3_addr,
  output [31:0] io_requestor_2_pmp_3_mask,
  output        io_requestor_2_pmp_4_cfg_l,
  output [1:0]  io_requestor_2_pmp_4_cfg_res,
  output [1:0]  io_requestor_2_pmp_4_cfg_a,
  output        io_requestor_2_pmp_4_cfg_x,
  output        io_requestor_2_pmp_4_cfg_w,
  output        io_requestor_2_pmp_4_cfg_r,
  output [29:0] io_requestor_2_pmp_4_addr,
  output [31:0] io_requestor_2_pmp_4_mask,
  output        io_requestor_2_pmp_5_cfg_l,
  output [1:0]  io_requestor_2_pmp_5_cfg_res,
  output [1:0]  io_requestor_2_pmp_5_cfg_a,
  output        io_requestor_2_pmp_5_cfg_x,
  output        io_requestor_2_pmp_5_cfg_w,
  output        io_requestor_2_pmp_5_cfg_r,
  output [29:0] io_requestor_2_pmp_5_addr,
  output [31:0] io_requestor_2_pmp_5_mask,
  output        io_requestor_2_pmp_6_cfg_l,
  output [1:0]  io_requestor_2_pmp_6_cfg_res,
  output [1:0]  io_requestor_2_pmp_6_cfg_a,
  output        io_requestor_2_pmp_6_cfg_x,
  output        io_requestor_2_pmp_6_cfg_w,
  output        io_requestor_2_pmp_6_cfg_r,
  output [29:0] io_requestor_2_pmp_6_addr,
  output [31:0] io_requestor_2_pmp_6_mask,
  output        io_requestor_2_pmp_7_cfg_l,
  output [1:0]  io_requestor_2_pmp_7_cfg_res,
  output [1:0]  io_requestor_2_pmp_7_cfg_a,
  output        io_requestor_2_pmp_7_cfg_x,
  output        io_requestor_2_pmp_7_cfg_w,
  output        io_requestor_2_pmp_7_cfg_r,
  output [29:0] io_requestor_2_pmp_7_addr,
  output [31:0] io_requestor_2_pmp_7_mask,
  output        io_requestor_2_customCSRs_csrs_0_wen,
  output [63:0] io_requestor_2_customCSRs_csrs_0_wdata,
  output [63:0] io_requestor_2_customCSRs_csrs_0_value,
  input         io_mem_req_ready,
  output        io_mem_req_valid,
  output [39:0] io_mem_req_bits_addr,
  output [6:0]  io_mem_req_bits_tag,
  output [4:0]  io_mem_req_bits_cmd,
  output [1:0]  io_mem_req_bits_size,
  output        io_mem_req_bits_signed,
  output [1:0]  io_mem_req_bits_dprv,
  output        io_mem_req_bits_phys,
  output        io_mem_req_bits_no_alloc,
  output        io_mem_req_bits_no_xcpt,
  output [63:0] io_mem_req_bits_data,
  output [7:0]  io_mem_req_bits_mask,
  output        io_mem_s1_kill,
  output [63:0] io_mem_s1_data_data,
  output [7:0]  io_mem_s1_data_mask,
  input         io_mem_s2_nack,
  input         io_mem_s2_nack_cause_raw,
  output        io_mem_s2_kill,
  input         io_mem_s2_uncached,
  input  [31:0] io_mem_s2_paddr,
  input         io_mem_resp_valid,
  input  [39:0] io_mem_resp_bits_addr,
  input  [6:0]  io_mem_resp_bits_tag,
  input  [4:0]  io_mem_resp_bits_cmd,
  input  [1:0]  io_mem_resp_bits_size,
  input         io_mem_resp_bits_signed,
  input  [1:0]  io_mem_resp_bits_dprv,
  input  [63:0] io_mem_resp_bits_data,
  input  [7:0]  io_mem_resp_bits_mask,
  input         io_mem_resp_bits_replay,
  input         io_mem_resp_bits_has_data,
  input  [63:0] io_mem_resp_bits_data_word_bypass,
  input  [63:0] io_mem_resp_bits_data_raw,
  input  [63:0] io_mem_resp_bits_store_data,
  input         io_mem_replay_next,
  input         io_mem_s2_xcpt_ma_ld,
  input         io_mem_s2_xcpt_ma_st,
  input         io_mem_s2_xcpt_pf_ld,
  input         io_mem_s2_xcpt_pf_st,
  input         io_mem_s2_xcpt_ae_ld,
  input         io_mem_s2_xcpt_ae_st,
  input         io_mem_ordered,
  input         io_mem_perf_acquire,
  input         io_mem_perf_release,
  input         io_mem_perf_grant,
  input         io_mem_perf_tlbMiss,
  input         io_mem_perf_blocked,
  input         io_mem_perf_canAcceptStoreThenLoad,
  input         io_mem_perf_canAcceptStoreThenRMW,
  input         io_mem_perf_canAcceptLoadThenLoad,
  input         io_mem_perf_storeBufferEmptyAfterLoad,
  input         io_mem_perf_storeBufferEmptyAfterStore,
  output        io_mem_keep_clock_enabled,
  input         io_mem_clock_enabled,
  input  [3:0]  io_dpath_ptbr_mode,
  input  [15:0] io_dpath_ptbr_asid,
  input  [43:0] io_dpath_ptbr_ppn,
  input         io_dpath_sfence_valid,
  input         io_dpath_sfence_bits_rs1,
  input         io_dpath_sfence_bits_rs2,
  input  [38:0] io_dpath_sfence_bits_addr,
  input         io_dpath_sfence_bits_asid,
  input         io_dpath_status_debug,
  input         io_dpath_status_cease,
  input         io_dpath_status_wfi,
  input  [31:0] io_dpath_status_isa,
  input  [1:0]  io_dpath_status_dprv,
  input  [1:0]  io_dpath_status_prv,
  input         io_dpath_status_sd,
  input  [26:0] io_dpath_status_zero2,
  input  [1:0]  io_dpath_status_sxl,
  input  [1:0]  io_dpath_status_uxl,
  input         io_dpath_status_sd_rv32,
  input  [7:0]  io_dpath_status_zero1,
  input         io_dpath_status_tsr,
  input         io_dpath_status_tw,
  input         io_dpath_status_tvm,
  input         io_dpath_status_mxr,
  input         io_dpath_status_sum,
  input         io_dpath_status_mprv,
  input  [1:0]  io_dpath_status_xs,
  input  [1:0]  io_dpath_status_fs,
  input  [1:0]  io_dpath_status_mpp,
  input  [1:0]  io_dpath_status_vs,
  input         io_dpath_status_spp,
  input         io_dpath_status_mpie,
  input         io_dpath_status_hpie,
  input         io_dpath_status_spie,
  input         io_dpath_status_upie,
  input         io_dpath_status_mie,
  input         io_dpath_status_hie,
  input         io_dpath_status_sie,
  input         io_dpath_status_uie,
  input         io_dpath_pmp_0_cfg_l,
  input  [1:0]  io_dpath_pmp_0_cfg_res,
  input  [1:0]  io_dpath_pmp_0_cfg_a,
  input         io_dpath_pmp_0_cfg_x,
  input         io_dpath_pmp_0_cfg_w,
  input         io_dpath_pmp_0_cfg_r,
  input  [29:0] io_dpath_pmp_0_addr,
  input  [31:0] io_dpath_pmp_0_mask,
  input         io_dpath_pmp_1_cfg_l,
  input  [1:0]  io_dpath_pmp_1_cfg_res,
  input  [1:0]  io_dpath_pmp_1_cfg_a,
  input         io_dpath_pmp_1_cfg_x,
  input         io_dpath_pmp_1_cfg_w,
  input         io_dpath_pmp_1_cfg_r,
  input  [29:0] io_dpath_pmp_1_addr,
  input  [31:0] io_dpath_pmp_1_mask,
  input         io_dpath_pmp_2_cfg_l,
  input  [1:0]  io_dpath_pmp_2_cfg_res,
  input  [1:0]  io_dpath_pmp_2_cfg_a,
  input         io_dpath_pmp_2_cfg_x,
  input         io_dpath_pmp_2_cfg_w,
  input         io_dpath_pmp_2_cfg_r,
  input  [29:0] io_dpath_pmp_2_addr,
  input  [31:0] io_dpath_pmp_2_mask,
  input         io_dpath_pmp_3_cfg_l,
  input  [1:0]  io_dpath_pmp_3_cfg_res,
  input  [1:0]  io_dpath_pmp_3_cfg_a,
  input         io_dpath_pmp_3_cfg_x,
  input         io_dpath_pmp_3_cfg_w,
  input         io_dpath_pmp_3_cfg_r,
  input  [29:0] io_dpath_pmp_3_addr,
  input  [31:0] io_dpath_pmp_3_mask,
  input         io_dpath_pmp_4_cfg_l,
  input  [1:0]  io_dpath_pmp_4_cfg_res,
  input  [1:0]  io_dpath_pmp_4_cfg_a,
  input         io_dpath_pmp_4_cfg_x,
  input         io_dpath_pmp_4_cfg_w,
  input         io_dpath_pmp_4_cfg_r,
  input  [29:0] io_dpath_pmp_4_addr,
  input  [31:0] io_dpath_pmp_4_mask,
  input         io_dpath_pmp_5_cfg_l,
  input  [1:0]  io_dpath_pmp_5_cfg_res,
  input  [1:0]  io_dpath_pmp_5_cfg_a,
  input         io_dpath_pmp_5_cfg_x,
  input         io_dpath_pmp_5_cfg_w,
  input         io_dpath_pmp_5_cfg_r,
  input  [29:0] io_dpath_pmp_5_addr,
  input  [31:0] io_dpath_pmp_5_mask,
  input         io_dpath_pmp_6_cfg_l,
  input  [1:0]  io_dpath_pmp_6_cfg_res,
  input  [1:0]  io_dpath_pmp_6_cfg_a,
  input         io_dpath_pmp_6_cfg_x,
  input         io_dpath_pmp_6_cfg_w,
  input         io_dpath_pmp_6_cfg_r,
  input  [29:0] io_dpath_pmp_6_addr,
  input  [31:0] io_dpath_pmp_6_mask,
  input         io_dpath_pmp_7_cfg_l,
  input  [1:0]  io_dpath_pmp_7_cfg_res,
  input  [1:0]  io_dpath_pmp_7_cfg_a,
  input         io_dpath_pmp_7_cfg_x,
  input         io_dpath_pmp_7_cfg_w,
  input         io_dpath_pmp_7_cfg_r,
  input  [29:0] io_dpath_pmp_7_addr,
  input  [31:0] io_dpath_pmp_7_mask,
  output        io_dpath_perf_l2miss,
  input         io_dpath_customCSRs_csrs_0_wen,
  input  [63:0] io_dpath_customCSRs_csrs_0_wdata,
  input  [63:0] io_dpath_customCSRs_csrs_0_value,
  output        io_dpath_clock_enabled
);
`ifdef RANDOMIZE_MEM_INIT
  reg [63:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
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
  reg [63:0] _RAND_12;
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
  reg [63:0] _RAND_23;
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
  reg [1023:0] _RAND_43;
  reg [1023:0] _RAND_44;
  reg [31:0] _RAND_45;
  reg [31:0] _RAND_46;
  reg [63:0] _RAND_47;
  reg [31:0] _RAND_48;
  reg [31:0] _RAND_49;
`endif // RANDOMIZE_REG_INIT
  wire  arb_io_in_0_ready; // @[PTW.scala 105:19]
  wire  arb_io_in_0_valid; // @[PTW.scala 105:19]
  wire  arb_io_in_0_bits_valid; // @[PTW.scala 105:19]
  wire [26:0] arb_io_in_0_bits_bits_addr; // @[PTW.scala 105:19]
  wire  arb_io_in_1_ready; // @[PTW.scala 105:19]
  wire  arb_io_in_1_valid; // @[PTW.scala 105:19]
  wire  arb_io_in_1_bits_valid; // @[PTW.scala 105:19]
  wire [26:0] arb_io_in_1_bits_bits_addr; // @[PTW.scala 105:19]
  wire  arb_io_in_2_ready; // @[PTW.scala 105:19]
  wire  arb_io_in_2_valid; // @[PTW.scala 105:19]
  wire  arb_io_in_2_bits_valid; // @[PTW.scala 105:19]
  wire [26:0] arb_io_in_2_bits_bits_addr; // @[PTW.scala 105:19]
  wire  arb_io_out_ready; // @[PTW.scala 105:19]
  wire  arb_io_out_valid; // @[PTW.scala 105:19]
  wire  arb_io_out_bits_valid; // @[PTW.scala 105:19]
  wire [26:0] arb_io_out_bits_bits_addr; // @[PTW.scala 105:19]
  wire [1:0] arb_io_chosen; // @[PTW.scala 105:19]
  reg [43:0] l2_tlb_ram [0:1023]; // @[DescribedSRAM.scala 23:26]
  wire  l2_tlb_ram_s1_rdata_en; // @[DescribedSRAM.scala 23:26]
  wire [9:0] l2_tlb_ram_s1_rdata_addr; // @[DescribedSRAM.scala 23:26]
  wire [43:0] l2_tlb_ram_s1_rdata_data; // @[DescribedSRAM.scala 23:26]
  wire [43:0] l2_tlb_ram__T_207_data; // @[DescribedSRAM.scala 23:26]
  wire [9:0] l2_tlb_ram__T_207_addr; // @[DescribedSRAM.scala 23:26]
  wire  l2_tlb_ram__T_207_mask; // @[DescribedSRAM.scala 23:26]
  wire  l2_tlb_ram__T_207_en; // @[DescribedSRAM.scala 23:26]
  reg  l2_tlb_ram_s1_rdata_en_pipe_0;
  reg [9:0] l2_tlb_ram_s1_rdata_addr_pipe_0;
  wire [2:0] package_Anon_io_x; // @[package.scala 213:21]
  wire [2:0] package_Anon_io_y; // @[package.scala 213:21]
  wire [53:0] package_Anon_1_io_x_ppn; // @[package.scala 213:21]
  wire [1:0] package_Anon_1_io_x_reserved_for_software; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_d; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_a; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_g; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_u; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_x; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_w; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_r; // @[package.scala 213:21]
  wire  package_Anon_1_io_x_v; // @[package.scala 213:21]
  wire [53:0] package_Anon_1_io_y_ppn; // @[package.scala 213:21]
  wire [1:0] package_Anon_1_io_y_reserved_for_software; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_d; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_a; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_g; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_u; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_x; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_w; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_r; // @[package.scala 213:21]
  wire  package_Anon_1_io_y_v; // @[package.scala 213:21]
  reg [2:0] state; // @[PTW.scala 103:18]
  reg  resp_valid_0; // @[PTW.scala 109:23]
  reg  resp_valid_1; // @[PTW.scala 109:23]
  reg  resp_valid_2; // @[PTW.scala 109:23]
  wire  _T_2 = state != 3'h0; // @[PTW.scala 111:24]
  reg  invalidated; // @[PTW.scala 118:24]
  reg [1:0] count; // @[PTW.scala 119:18]
  reg  resp_ae; // @[PTW.scala 120:24]
  reg [26:0] r_req_addr; // @[PTW.scala 123:18]
  reg [1:0] r_req_dest; // @[PTW.scala 124:23]
  reg [53:0] r_pte_ppn; // @[PTW.scala 125:18]
  reg [1:0] r_pte_reserved_for_software; // @[PTW.scala 125:18]
  reg  r_pte_d; // @[PTW.scala 125:18]
  reg  r_pte_a; // @[PTW.scala 125:18]
  reg  r_pte_g; // @[PTW.scala 125:18]
  reg  r_pte_u; // @[PTW.scala 125:18]
  reg  r_pte_x; // @[PTW.scala 125:18]
  reg  r_pte_w; // @[PTW.scala 125:18]
  reg  r_pte_r; // @[PTW.scala 125:18]
  reg  r_pte_v; // @[PTW.scala 125:18]
  reg  mem_resp_valid; // @[PTW.scala 127:31]
  reg [63:0] mem_resp_data; // @[PTW.scala 128:30]
  wire  tmp_v = mem_resp_data[0]; // @[PTW.scala 139:33]
  wire  tmp_r = mem_resp_data[1]; // @[PTW.scala 139:33]
  wire  tmp_w = mem_resp_data[2]; // @[PTW.scala 139:33]
  wire  tmp_x = mem_resp_data[3]; // @[PTW.scala 139:33]
  wire  tmp_u = mem_resp_data[4]; // @[PTW.scala 139:33]
  wire  tmp_g = mem_resp_data[5]; // @[PTW.scala 139:33]
  wire  tmp_a = mem_resp_data[6]; // @[PTW.scala 139:33]
  wire  tmp_d = mem_resp_data[7]; // @[PTW.scala 139:33]
  wire [1:0] tmp_reserved_for_software = mem_resp_data[9:8]; // @[PTW.scala 139:33]
  wire [53:0] tmp_ppn = mem_resp_data[63:10]; // @[PTW.scala 139:33]
  wire  _GEN_0 = count <= 2'h0 & tmp_ppn[17:9] != 9'h0 ? 1'h0 : tmp_v; // @[PTW.scala 145:{102,110}]
  wire  _GEN_1 = count <= 2'h1 & tmp_ppn[8:0] != 9'h0 ? 1'h0 : _GEN_0; // @[PTW.scala 145:{102,110}]
  wire  res_v = tmp_r | tmp_w | tmp_x ? _GEN_1 : tmp_v; // @[PTW.scala 142:36]
  wire  invalid_paddr = tmp_ppn[53:20] != 34'h0; // @[PTW.scala 147:32]
  wire  _T_35 = res_v & ~tmp_r & ~tmp_w & ~tmp_x; // @[PTW.scala 68:45]
  wire  _T_36 = ~invalid_paddr; // @[PTW.scala 149:33]
  wire  _T_38 = count < 2'h2; // @[PTW.scala 149:57]
  wire  traverse = _T_35 & ~invalid_paddr & count < 2'h2; // @[PTW.scala 149:48]
  wire [8:0] vpn_idxs_0 = r_req_addr[26:18]; // @[PTW.scala 151:60]
  wire [8:0] vpn_idxs_1 = r_req_addr[17:9]; // @[PTW.scala 151:90]
  wire [8:0] vpn_idxs_2 = r_req_addr[8:0]; // @[PTW.scala 151:90]
  wire [8:0] _T_43 = count == 2'h1 ? vpn_idxs_1 : vpn_idxs_0; // @[package.scala 32:76]
  wire  _T_44 = count == 2'h2; // @[package.scala 32:86]
  wire [8:0] _T_45 = count == 2'h2 ? vpn_idxs_2 : _T_43; // @[package.scala 32:76]
  wire [8:0] vpn_idx = count == 2'h3 ? vpn_idxs_2 : _T_45; // @[package.scala 32:76]
  wire [62:0] _T_47 = {r_pte_ppn,vpn_idx}; // @[Cat.scala 29:58]
  wire [65:0] pte_addr = {_T_47, 3'h0}; // @[PTW.scala 153:29]
  wire [53:0] choices_0 = {r_pte_ppn[53:18],r_req_addr[17:0]}; // @[Cat.scala 29:58]
  wire [53:0] choices_1 = {r_pte_ppn[53:9],vpn_idxs_2}; // @[Cat.scala 29:58]
  wire [53:0] fragmented_superpage_ppn = count[0] ? choices_1 : choices_0; // @[package.scala 32:76]
  wire  _T_55 = arb_io_out_ready & arb_io_out_valid; // @[Decoupled.scala 40:37]
  reg [6:0] _T_56; // @[Replacement.scala 42:30]
  reg [7:0] valid; // @[PTW.scala 168:24]
  reg [31:0] tags_0; // @[PTW.scala 169:19]
  reg [31:0] tags_1; // @[PTW.scala 169:19]
  reg [31:0] tags_2; // @[PTW.scala 169:19]
  reg [31:0] tags_3; // @[PTW.scala 169:19]
  reg [31:0] tags_4; // @[PTW.scala 169:19]
  reg [31:0] tags_5; // @[PTW.scala 169:19]
  reg [31:0] tags_6; // @[PTW.scala 169:19]
  reg [31:0] tags_7; // @[PTW.scala 169:19]
  reg [19:0] data_0; // @[PTW.scala 170:19]
  reg [19:0] data_1; // @[PTW.scala 170:19]
  reg [19:0] data_2; // @[PTW.scala 170:19]
  reg [19:0] data_3; // @[PTW.scala 170:19]
  reg [19:0] data_4; // @[PTW.scala 170:19]
  reg [19:0] data_5; // @[PTW.scala 170:19]
  reg [19:0] data_6; // @[PTW.scala 170:19]
  reg [19:0] data_7; // @[PTW.scala 170:19]
  wire [65:0] _GEN_138 = {{34'd0}, tags_0}; // @[PTW.scala 172:27]
  wire  _T_57 = _GEN_138 == pte_addr; // @[PTW.scala 172:27]
  wire [65:0] _GEN_139 = {{34'd0}, tags_1}; // @[PTW.scala 172:27]
  wire  _T_58 = _GEN_139 == pte_addr; // @[PTW.scala 172:27]
  wire [65:0] _GEN_140 = {{34'd0}, tags_2}; // @[PTW.scala 172:27]
  wire  _T_59 = _GEN_140 == pte_addr; // @[PTW.scala 172:27]
  wire [65:0] _GEN_141 = {{34'd0}, tags_3}; // @[PTW.scala 172:27]
  wire  _T_60 = _GEN_141 == pte_addr; // @[PTW.scala 172:27]
  wire [65:0] _GEN_142 = {{34'd0}, tags_4}; // @[PTW.scala 172:27]
  wire  _T_61 = _GEN_142 == pte_addr; // @[PTW.scala 172:27]
  wire [65:0] _GEN_143 = {{34'd0}, tags_5}; // @[PTW.scala 172:27]
  wire  _T_62 = _GEN_143 == pte_addr; // @[PTW.scala 172:27]
  wire [65:0] _GEN_144 = {{34'd0}, tags_6}; // @[PTW.scala 172:27]
  wire  _T_63 = _GEN_144 == pte_addr; // @[PTW.scala 172:27]
  wire [65:0] _GEN_145 = {{34'd0}, tags_7}; // @[PTW.scala 172:27]
  wire  _T_64 = _GEN_145 == pte_addr; // @[PTW.scala 172:27]
  wire [7:0] _T_71 = {_T_64,_T_63,_T_62,_T_61,_T_60,_T_59,_T_58,_T_57}; // @[Cat.scala 29:58]
  wire [7:0] hits = _T_71 & valid; // @[PTW.scala 172:48]
  wire  hit = |hits; // @[PTW.scala 173:20]
  wire  _T_75 = ~invalidated; // @[PTW.scala 174:49]
  wire [7:0] _T_78 = {_T_56, 1'h0}; // @[Replacement.scala 61:31]
  wire [7:0] _T_82 = {{1'd0}, _T_78[7:1]}; // @[Replacement.scala 65:48]
  wire [1:0] _T_85 = {1'h1,_T_82[0]}; // @[Cat.scala 29:58]
  wire [7:0] _T_89 = _T_78 >> _T_85; // @[Replacement.scala 65:48]
  wire [2:0] _T_92 = {1'h1,_T_82[0],_T_89[0]}; // @[Cat.scala 29:58]
  wire [7:0] _T_96 = _T_78 >> _T_92; // @[Replacement.scala 65:48]
  wire [3:0] _T_99 = {1'h1,_T_82[0],_T_89[0],_T_96[0]}; // @[Cat.scala 29:58]
  wire [7:0] _T_101 = ~valid; // @[PTW.scala 175:61]
  wire [2:0] _T_110 = _T_101[6] ? 3'h6 : 3'h7; // @[Mux.scala 47:69]
  wire [2:0] _T_111 = _T_101[5] ? 3'h5 : _T_110; // @[Mux.scala 47:69]
  wire [2:0] _T_112 = _T_101[4] ? 3'h4 : _T_111; // @[Mux.scala 47:69]
  wire [2:0] _T_113 = _T_101[3] ? 3'h3 : _T_112; // @[Mux.scala 47:69]
  wire [2:0] _T_114 = _T_101[2] ? 3'h2 : _T_113; // @[Mux.scala 47:69]
  wire [2:0] _T_115 = _T_101[1] ? 3'h1 : _T_114; // @[Mux.scala 47:69]
  wire [2:0] _T_116 = _T_101[0] ? 3'h0 : _T_115; // @[Mux.scala 47:69]
  wire [2:0] r = &valid ? _T_99[2:0] : _T_116; // @[PTW.scala 175:18]
  wire [7:0] _T_117 = 8'h1 << r; // @[OneHot.scala 58:35]
  wire [7:0] _T_118 = valid | _T_117; // @[PTW.scala 176:22]
  wire [53:0] res_ppn = {{34'd0}, tmp_ppn[19:0]};
  wire  _T_119 = state == 3'h1; // @[PTW.scala 180:24]
  wire  _T_123 = |hits[7:4]; // @[OneHot.scala 32:14]
  wire [3:0] _T_124 = hits[7:4] | hits[3:0]; // @[OneHot.scala 32:28]
  wire  _T_127 = |_T_124[3:2]; // @[OneHot.scala 32:14]
  wire [1:0] _T_128 = _T_124[3:2] | _T_124[1:0]; // @[OneHot.scala 32:28]
  wire [2:0] _T_131 = {_T_123,_T_127,_T_128[1]}; // @[Cat.scala 29:58]
  wire [7:0] _T_136 = _T_78 | 8'h2; // @[Replacement.scala 54:37]
  wire [7:0] _T_137 = ~_T_78; // @[Replacement.scala 54:37]
  wire [7:0] _T_138 = _T_137 | 8'h2; // @[Replacement.scala 54:37]
  wire [7:0] _T_139 = ~_T_138; // @[Replacement.scala 54:37]
  wire [7:0] _T_140 = ~_T_131[2] ? _T_136 : _T_139; // @[Replacement.scala 54:37]
  wire [1:0] _T_141 = {1'h1,_T_131[2]}; // @[Cat.scala 29:58]
  wire [3:0] _T_144 = 4'h1 << _T_141; // @[Replacement.scala 54:37]
  wire [7:0] _GEN_147 = {{4'd0}, _T_144}; // @[Replacement.scala 54:37]
  wire [7:0] _T_145 = _T_140 | _GEN_147; // @[Replacement.scala 54:37]
  wire [7:0] _T_146 = ~_T_140; // @[Replacement.scala 54:37]
  wire [7:0] _T_147 = _T_146 | _GEN_147; // @[Replacement.scala 54:37]
  wire [7:0] _T_148 = ~_T_147; // @[Replacement.scala 54:37]
  wire [7:0] _T_149 = ~_T_131[1] ? _T_145 : _T_148; // @[Replacement.scala 54:37]
  wire [2:0] _T_150 = {1'h1,_T_131[2],_T_131[1]}; // @[Cat.scala 29:58]
  wire [7:0] _T_153 = 8'h1 << _T_150; // @[Replacement.scala 54:37]
  wire [7:0] _T_154 = _T_149 | _T_153; // @[Replacement.scala 54:37]
  wire [7:0] _T_155 = ~_T_149; // @[Replacement.scala 54:37]
  wire [7:0] _T_156 = _T_155 | _T_153; // @[Replacement.scala 54:37]
  wire [7:0] _T_157 = ~_T_156; // @[Replacement.scala 54:37]
  wire [7:0] _T_158 = ~_T_131[0] ? _T_154 : _T_157; // @[Replacement.scala 54:37]
  wire  pte_cache_hit = hit & _T_38; // @[PTW.scala 186:10]
  wire [19:0] _T_180 = hits[0] ? data_0 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_181 = hits[1] ? data_1 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_182 = hits[2] ? data_2 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_183 = hits[3] ? data_3 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_184 = hits[4] ? data_4 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_185 = hits[5] ? data_5 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_186 = hits[6] ? data_6 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_187 = hits[7] ? data_7 : 20'h0; // @[Mux.scala 27:72]
  wire [19:0] _T_188 = _T_180 | _T_181; // @[Mux.scala 27:72]
  wire [19:0] _T_189 = _T_188 | _T_182; // @[Mux.scala 27:72]
  wire [19:0] _T_190 = _T_189 | _T_183; // @[Mux.scala 27:72]
  wire [19:0] _T_191 = _T_190 | _T_184; // @[Mux.scala 27:72]
  wire [19:0] _T_192 = _T_191 | _T_185; // @[Mux.scala 27:72]
  wire [19:0] _T_193 = _T_192 | _T_186; // @[Mux.scala 27:72]
  wire [19:0] pte_cache_data = _T_193 | _T_187; // @[Mux.scala 27:72]
  reg  l2_refill; // @[PTW.scala 189:26]
  reg [1023:0] g; // @[PTW.scala 203:16]
  reg [1023:0] valid_1; // @[PTW.scala 204:24]
  wire [16:0] r_tag = r_req_addr[26:10]; // @[package.scala 120:13]
  wire [9:0] r_idx = r_req_addr[9:0]; // @[package.scala 120:13]
  wire [19:0] entry_ppn = r_pte_ppn[19:0]; // @[PTW.scala 207:23 208:13]
  wire [42:0] _T_203 = {r_tag,entry_ppn,r_pte_d,r_pte_a,r_pte_u,r_pte_x,r_pte_w,r_pte_r}; // @[PTW.scala 210:42]
  wire  _T_204 = ^_T_203; // @[ECC.scala 64:59]
  wire [1023:0] mask = 1024'h1 << r_idx; // @[OneHot.scala 58:35]
  wire [1023:0] _T_208 = valid_1 | mask; // @[PTW.scala 213:22]
  wire [1023:0] _T_209 = g | mask; // @[PTW.scala 214:27]
  wire [1023:0] _T_210 = ~mask; // @[PTW.scala 214:39]
  wire [1023:0] _T_211 = g & _T_210; // @[PTW.scala 214:37]
  wire [1023:0] _T_214 = 1024'h1 << io_dpath_sfence_bits_addr[21:12]; // @[OneHot.scala 58:35]
  wire [1023:0] _T_215 = ~_T_214; // @[PTW.scala 218:47]
  wire [1023:0] _T_216 = valid_1 & _T_215; // @[PTW.scala 218:45]
  wire [1023:0] _T_217 = valid_1 & g; // @[PTW.scala 219:45]
  wire [1023:0] _T_218 = io_dpath_sfence_bits_rs2 ? _T_217 : 1024'h0; // @[PTW.scala 219:12]
  wire  _T_220 = ~l2_refill; // @[PTW.scala 222:20]
  wire  s0_valid = ~l2_refill & _T_55; // @[PTW.scala 222:31]
  reg  s1_valid; // @[PTW.scala 223:27]
  reg  s2_valid; // @[PTW.scala 224:27]
  reg [43:0] _T_227; // @[Reg.scala 15:16]
  wire  l2_error = ^_T_227; // @[ECC.scala 70:27]
  wire [1023:0] _T_230 = valid_1 >> r_idx; // @[PTW.scala 227:39]
  reg  s2_valid_bit; // @[Reg.scala 15:16]
  wire [1023:0] _T_232 = g >> r_idx; // @[PTW.scala 228:27]
  reg  s2_g; // @[Reg.scala 15:16]
  wire  _T_234 = s2_valid & s2_valid_bit; // @[PTW.scala 229:20]
  wire  s2_entry_r = _T_227[0]; // @[PTW.scala 231:49]
  wire  s2_entry_w = _T_227[1]; // @[PTW.scala 231:49]
  wire  s2_entry_x = _T_227[2]; // @[PTW.scala 231:49]
  wire  s2_entry_u = _T_227[3]; // @[PTW.scala 231:49]
  wire  s2_entry_a = _T_227[4]; // @[PTW.scala 231:49]
  wire  s2_entry_d = _T_227[5]; // @[PTW.scala 231:49]
  wire [19:0] s2_entry_ppn = _T_227[25:6]; // @[PTW.scala 231:49]
  wire [16:0] s2_entry_tag = _T_227[42:26]; // @[PTW.scala 231:49]
  wire  _T_247 = r_tag == s2_entry_tag; // @[PTW.scala 232:52]
  wire  s2_hit = _T_234 & r_tag == s2_entry_tag; // @[PTW.scala 232:43]
  wire [65:0] _T_266 = pte_addr ^ 66'hc000000; // @[Parameters.scala 137:31]
  wire [66:0] _T_267 = {1'b0,$signed(_T_266)}; // @[Parameters.scala 137:49]
  wire [66:0] _T_269 = $signed(_T_267) & -67'sh4000000; // @[Parameters.scala 137:52]
  wire  _T_270 = $signed(_T_269) == 67'sh0; // @[Parameters.scala 137:67]
  wire [65:0] _T_271 = pte_addr ^ 66'h80000000; // @[Parameters.scala 137:31]
  wire [66:0] _T_272 = {1'b0,$signed(_T_271)}; // @[Parameters.scala 137:49]
  wire [66:0] _T_274 = $signed(_T_272) & -67'sh10000000; // @[Parameters.scala 137:52]
  wire  _T_275 = $signed(_T_274) == 67'sh0; // @[Parameters.scala 137:67]
  wire  pmaPgLevelHomogeneous_1 = _T_270 | _T_275; // @[TLBPermissions.scala 98:65]
  wire [66:0] _T_280 = {1'b0,$signed(pte_addr)}; // @[Parameters.scala 137:49]
  wire [65:0] _T_295 = pte_addr ^ 66'h3000; // @[Parameters.scala 137:31]
  wire [66:0] _T_296 = {1'b0,$signed(_T_295)}; // @[Parameters.scala 137:49]
  wire [66:0] _T_298 = $signed(_T_296) & -67'sh1000; // @[Parameters.scala 137:52]
  wire  _T_299 = $signed(_T_298) == 67'sh0; // @[Parameters.scala 137:67]
  wire [65:0] _T_300 = pte_addr ^ 66'h2010000; // @[Parameters.scala 137:31]
  wire [66:0] _T_301 = {1'b0,$signed(_T_300)}; // @[Parameters.scala 137:49]
  wire [66:0] _T_303 = $signed(_T_301) & -67'sh1000; // @[Parameters.scala 137:52]
  wire  _T_304 = $signed(_T_303) == 67'sh0; // @[Parameters.scala 137:67]
  wire [65:0] _T_305 = pte_addr ^ 66'h54000000; // @[Parameters.scala 137:31]
  wire [66:0] _T_306 = {1'b0,$signed(_T_305)}; // @[Parameters.scala 137:49]
  wire [66:0] _T_308 = $signed(_T_306) & -67'sh1000; // @[Parameters.scala 137:52]
  wire  _T_309 = $signed(_T_308) == 67'sh0; // @[Parameters.scala 137:67]
  wire [65:0] _T_315 = pte_addr ^ 66'h2000000; // @[Parameters.scala 137:31]
  wire [66:0] _T_316 = {1'b0,$signed(_T_315)}; // @[Parameters.scala 137:49]
  wire [66:0] _T_318 = $signed(_T_316) & -67'sh10000; // @[Parameters.scala 137:52]
  wire  _T_319 = $signed(_T_318) == 67'sh0; // @[Parameters.scala 137:67]
  wire [65:0] _T_325 = pte_addr ^ 66'h10000; // @[Parameters.scala 137:31]
  wire [66:0] _T_326 = {1'b0,$signed(_T_325)}; // @[Parameters.scala 137:49]
  wire [66:0] _T_328 = $signed(_T_326) & -67'sh10000; // @[Parameters.scala 137:52]
  wire  _T_329 = $signed(_T_328) == 67'sh0; // @[Parameters.scala 137:67]
  wire [66:0] _T_333 = $signed(_T_280) & -67'sh1000; // @[Parameters.scala 137:52]
  wire  _T_334 = $signed(_T_333) == 67'sh0; // @[Parameters.scala 137:67]
  wire  pmaPgLevelHomogeneous_2 = _T_299 | _T_304 | _T_309 | _T_270 | _T_319 | _T_275 | _T_329 | _T_334; // @[TLBPermissions.scala 98:65]
  wire  _T_385 = count == 2'h2 ? pmaPgLevelHomogeneous_2 : count == 2'h1 & pmaPgLevelHomogeneous_1; // @[package.scala 32:76]
  wire  pmaHomogeneous = count == 2'h3 ? pmaPgLevelHomogeneous_2 : _T_385; // @[package.scala 32:76]
  wire [65:0] _T_388 = {pte_addr[65:12], 12'h0}; // @[PTW.scala 268:92]
  wire  _T_395 = count == 2'h1 ? io_dpath_pmp_0_mask[20] : io_dpath_pmp_0_mask[29]; // @[package.scala 32:76]
  wire  _T_397 = count == 2'h2 ? io_dpath_pmp_0_mask[11] : _T_395; // @[package.scala 32:76]
  wire  _T_399 = count == 2'h3 ? io_dpath_pmp_0_mask[11] : _T_397; // @[package.scala 32:76]
  wire [31:0] _T_400 = {io_dpath_pmp_0_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_401 = ~_T_400; // @[PMP.scala 62:29]
  wire [31:0] _T_402 = _T_401 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_403 = ~_T_402; // @[PMP.scala 62:27]
  wire [65:0] _GEN_149 = {{34'd0}, _T_403}; // @[PMP.scala 100:53]
  wire [65:0] _T_404 = _T_388 ^ _GEN_149; // @[PMP.scala 100:53]
  wire  _T_406 = _T_404[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_413 = _T_404[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_420 = _T_404[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_422 = count == 2'h1 ? _T_413 : _T_406; // @[package.scala 32:76]
  wire  _T_424 = count == 2'h2 ? _T_420 : _T_422; // @[package.scala 32:76]
  wire  _T_426 = count == 2'h3 ? _T_420 : _T_424; // @[package.scala 32:76]
  wire  _T_427 = _T_399 | _T_426; // @[PMP.scala 100:21]
  wire  _T_441 = ~(_T_388 < _GEN_149); // @[PMP.scala 109:28]
  wire [31:0] _T_443 = count == 2'h1 ? 32'hffe00000 : 32'hc0000000; // @[package.scala 32:76]
  wire [31:0] _T_445 = count == 2'h2 ? 32'hfffff000 : _T_443; // @[package.scala 32:76]
  wire [31:0] _T_447 = count == 2'h3 ? 32'hfffff000 : _T_445; // @[package.scala 32:76]
  wire [65:0] _GEN_153 = {{34'd0}, _T_447}; // @[PMP.scala 112:30]
  wire [65:0] _T_448 = _T_388 & _GEN_153; // @[PMP.scala 112:30]
  wire [31:0] _T_460 = _T_403 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_155 = {{34'd0}, _T_460}; // @[PMP.scala 113:40]
  wire  _T_461 = _T_448 < _GEN_155; // @[PMP.scala 113:40]
  wire  _T_464 = _T_441 | _T_461; // @[PMP.scala 115:41]
  wire  _T_466 = io_dpath_pmp_0_cfg_a[1] ? _T_427 : ~io_dpath_pmp_0_cfg_a[0] | _T_464; // @[PMP.scala 120:8]
  wire  _T_473 = count == 2'h1 ? io_dpath_pmp_1_mask[20] : io_dpath_pmp_1_mask[29]; // @[package.scala 32:76]
  wire  _T_475 = count == 2'h2 ? io_dpath_pmp_1_mask[11] : _T_473; // @[package.scala 32:76]
  wire  _T_477 = count == 2'h3 ? io_dpath_pmp_1_mask[11] : _T_475; // @[package.scala 32:76]
  wire [31:0] _T_478 = {io_dpath_pmp_1_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_479 = ~_T_478; // @[PMP.scala 62:29]
  wire [31:0] _T_480 = _T_479 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_481 = ~_T_480; // @[PMP.scala 62:27]
  wire [65:0] _GEN_156 = {{34'd0}, _T_481}; // @[PMP.scala 100:53]
  wire [65:0] _T_482 = _T_388 ^ _GEN_156; // @[PMP.scala 100:53]
  wire  _T_484 = _T_482[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_491 = _T_482[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_498 = _T_482[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_500 = count == 2'h1 ? _T_491 : _T_484; // @[package.scala 32:76]
  wire  _T_502 = count == 2'h2 ? _T_498 : _T_500; // @[package.scala 32:76]
  wire  _T_504 = count == 2'h3 ? _T_498 : _T_502; // @[package.scala 32:76]
  wire  _T_505 = _T_477 | _T_504; // @[PMP.scala 100:21]
  wire  _T_519 = ~(_T_388 < _GEN_156); // @[PMP.scala 109:28]
  wire [31:0] _T_538 = _T_481 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_164 = {{34'd0}, _T_538}; // @[PMP.scala 113:40]
  wire  _T_539 = _T_448 < _GEN_164; // @[PMP.scala 113:40]
  wire  _T_542 = _T_461 | _T_519 | _T_441 & _T_539; // @[PMP.scala 115:41]
  wire  _T_544 = io_dpath_pmp_1_cfg_a[1] ? _T_505 : ~io_dpath_pmp_1_cfg_a[0] | _T_542; // @[PMP.scala 120:8]
  wire  _T_551 = count == 2'h1 ? io_dpath_pmp_2_mask[20] : io_dpath_pmp_2_mask[29]; // @[package.scala 32:76]
  wire  _T_553 = count == 2'h2 ? io_dpath_pmp_2_mask[11] : _T_551; // @[package.scala 32:76]
  wire  _T_555 = count == 2'h3 ? io_dpath_pmp_2_mask[11] : _T_553; // @[package.scala 32:76]
  wire [31:0] _T_556 = {io_dpath_pmp_2_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_557 = ~_T_556; // @[PMP.scala 62:29]
  wire [31:0] _T_558 = _T_557 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_559 = ~_T_558; // @[PMP.scala 62:27]
  wire [65:0] _GEN_165 = {{34'd0}, _T_559}; // @[PMP.scala 100:53]
  wire [65:0] _T_560 = _T_388 ^ _GEN_165; // @[PMP.scala 100:53]
  wire  _T_562 = _T_560[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_569 = _T_560[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_576 = _T_560[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_578 = count == 2'h1 ? _T_569 : _T_562; // @[package.scala 32:76]
  wire  _T_580 = count == 2'h2 ? _T_576 : _T_578; // @[package.scala 32:76]
  wire  _T_582 = count == 2'h3 ? _T_576 : _T_580; // @[package.scala 32:76]
  wire  _T_583 = _T_555 | _T_582; // @[PMP.scala 100:21]
  wire  _T_597 = ~(_T_388 < _GEN_165); // @[PMP.scala 109:28]
  wire [31:0] _T_616 = _T_559 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_173 = {{34'd0}, _T_616}; // @[PMP.scala 113:40]
  wire  _T_617 = _T_448 < _GEN_173; // @[PMP.scala 113:40]
  wire  _T_620 = _T_539 | _T_597 | _T_519 & _T_617; // @[PMP.scala 115:41]
  wire  _T_622 = io_dpath_pmp_2_cfg_a[1] ? _T_583 : ~io_dpath_pmp_2_cfg_a[0] | _T_620; // @[PMP.scala 120:8]
  wire  _T_629 = count == 2'h1 ? io_dpath_pmp_3_mask[20] : io_dpath_pmp_3_mask[29]; // @[package.scala 32:76]
  wire  _T_631 = count == 2'h2 ? io_dpath_pmp_3_mask[11] : _T_629; // @[package.scala 32:76]
  wire  _T_633 = count == 2'h3 ? io_dpath_pmp_3_mask[11] : _T_631; // @[package.scala 32:76]
  wire [31:0] _T_634 = {io_dpath_pmp_3_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_635 = ~_T_634; // @[PMP.scala 62:29]
  wire [31:0] _T_636 = _T_635 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_637 = ~_T_636; // @[PMP.scala 62:27]
  wire [65:0] _GEN_174 = {{34'd0}, _T_637}; // @[PMP.scala 100:53]
  wire [65:0] _T_638 = _T_388 ^ _GEN_174; // @[PMP.scala 100:53]
  wire  _T_640 = _T_638[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_647 = _T_638[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_654 = _T_638[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_656 = count == 2'h1 ? _T_647 : _T_640; // @[package.scala 32:76]
  wire  _T_658 = count == 2'h2 ? _T_654 : _T_656; // @[package.scala 32:76]
  wire  _T_660 = count == 2'h3 ? _T_654 : _T_658; // @[package.scala 32:76]
  wire  _T_661 = _T_633 | _T_660; // @[PMP.scala 100:21]
  wire  _T_675 = ~(_T_388 < _GEN_174); // @[PMP.scala 109:28]
  wire [31:0] _T_694 = _T_637 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_182 = {{34'd0}, _T_694}; // @[PMP.scala 113:40]
  wire  _T_695 = _T_448 < _GEN_182; // @[PMP.scala 113:40]
  wire  _T_698 = _T_617 | _T_675 | _T_597 & _T_695; // @[PMP.scala 115:41]
  wire  _T_700 = io_dpath_pmp_3_cfg_a[1] ? _T_661 : ~io_dpath_pmp_3_cfg_a[0] | _T_698; // @[PMP.scala 120:8]
  wire  _T_707 = count == 2'h1 ? io_dpath_pmp_4_mask[20] : io_dpath_pmp_4_mask[29]; // @[package.scala 32:76]
  wire  _T_709 = count == 2'h2 ? io_dpath_pmp_4_mask[11] : _T_707; // @[package.scala 32:76]
  wire  _T_711 = count == 2'h3 ? io_dpath_pmp_4_mask[11] : _T_709; // @[package.scala 32:76]
  wire [31:0] _T_712 = {io_dpath_pmp_4_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_713 = ~_T_712; // @[PMP.scala 62:29]
  wire [31:0] _T_714 = _T_713 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_715 = ~_T_714; // @[PMP.scala 62:27]
  wire [65:0] _GEN_183 = {{34'd0}, _T_715}; // @[PMP.scala 100:53]
  wire [65:0] _T_716 = _T_388 ^ _GEN_183; // @[PMP.scala 100:53]
  wire  _T_718 = _T_716[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_725 = _T_716[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_732 = _T_716[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_734 = count == 2'h1 ? _T_725 : _T_718; // @[package.scala 32:76]
  wire  _T_736 = count == 2'h2 ? _T_732 : _T_734; // @[package.scala 32:76]
  wire  _T_738 = count == 2'h3 ? _T_732 : _T_736; // @[package.scala 32:76]
  wire  _T_739 = _T_711 | _T_738; // @[PMP.scala 100:21]
  wire  _T_753 = ~(_T_388 < _GEN_183); // @[PMP.scala 109:28]
  wire [31:0] _T_772 = _T_715 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_191 = {{34'd0}, _T_772}; // @[PMP.scala 113:40]
  wire  _T_773 = _T_448 < _GEN_191; // @[PMP.scala 113:40]
  wire  _T_776 = _T_695 | _T_753 | _T_675 & _T_773; // @[PMP.scala 115:41]
  wire  _T_778 = io_dpath_pmp_4_cfg_a[1] ? _T_739 : ~io_dpath_pmp_4_cfg_a[0] | _T_776; // @[PMP.scala 120:8]
  wire  _T_785 = count == 2'h1 ? io_dpath_pmp_5_mask[20] : io_dpath_pmp_5_mask[29]; // @[package.scala 32:76]
  wire  _T_787 = count == 2'h2 ? io_dpath_pmp_5_mask[11] : _T_785; // @[package.scala 32:76]
  wire  _T_789 = count == 2'h3 ? io_dpath_pmp_5_mask[11] : _T_787; // @[package.scala 32:76]
  wire [31:0] _T_790 = {io_dpath_pmp_5_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_791 = ~_T_790; // @[PMP.scala 62:29]
  wire [31:0] _T_792 = _T_791 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_793 = ~_T_792; // @[PMP.scala 62:27]
  wire [65:0] _GEN_192 = {{34'd0}, _T_793}; // @[PMP.scala 100:53]
  wire [65:0] _T_794 = _T_388 ^ _GEN_192; // @[PMP.scala 100:53]
  wire  _T_796 = _T_794[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_803 = _T_794[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_810 = _T_794[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_812 = count == 2'h1 ? _T_803 : _T_796; // @[package.scala 32:76]
  wire  _T_814 = count == 2'h2 ? _T_810 : _T_812; // @[package.scala 32:76]
  wire  _T_816 = count == 2'h3 ? _T_810 : _T_814; // @[package.scala 32:76]
  wire  _T_817 = _T_789 | _T_816; // @[PMP.scala 100:21]
  wire  _T_831 = ~(_T_388 < _GEN_192); // @[PMP.scala 109:28]
  wire [31:0] _T_850 = _T_793 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_200 = {{34'd0}, _T_850}; // @[PMP.scala 113:40]
  wire  _T_851 = _T_448 < _GEN_200; // @[PMP.scala 113:40]
  wire  _T_854 = _T_773 | _T_831 | _T_753 & _T_851; // @[PMP.scala 115:41]
  wire  _T_856 = io_dpath_pmp_5_cfg_a[1] ? _T_817 : ~io_dpath_pmp_5_cfg_a[0] | _T_854; // @[PMP.scala 120:8]
  wire  _T_863 = count == 2'h1 ? io_dpath_pmp_6_mask[20] : io_dpath_pmp_6_mask[29]; // @[package.scala 32:76]
  wire  _T_865 = count == 2'h2 ? io_dpath_pmp_6_mask[11] : _T_863; // @[package.scala 32:76]
  wire  _T_867 = count == 2'h3 ? io_dpath_pmp_6_mask[11] : _T_865; // @[package.scala 32:76]
  wire [31:0] _T_868 = {io_dpath_pmp_6_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_869 = ~_T_868; // @[PMP.scala 62:29]
  wire [31:0] _T_870 = _T_869 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_871 = ~_T_870; // @[PMP.scala 62:27]
  wire [65:0] _GEN_201 = {{34'd0}, _T_871}; // @[PMP.scala 100:53]
  wire [65:0] _T_872 = _T_388 ^ _GEN_201; // @[PMP.scala 100:53]
  wire  _T_874 = _T_872[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_881 = _T_872[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_888 = _T_872[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_890 = count == 2'h1 ? _T_881 : _T_874; // @[package.scala 32:76]
  wire  _T_892 = count == 2'h2 ? _T_888 : _T_890; // @[package.scala 32:76]
  wire  _T_894 = count == 2'h3 ? _T_888 : _T_892; // @[package.scala 32:76]
  wire  _T_895 = _T_867 | _T_894; // @[PMP.scala 100:21]
  wire  _T_909 = ~(_T_388 < _GEN_201); // @[PMP.scala 109:28]
  wire [31:0] _T_928 = _T_871 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_209 = {{34'd0}, _T_928}; // @[PMP.scala 113:40]
  wire  _T_929 = _T_448 < _GEN_209; // @[PMP.scala 113:40]
  wire  _T_932 = _T_851 | _T_909 | _T_831 & _T_929; // @[PMP.scala 115:41]
  wire  _T_934 = io_dpath_pmp_6_cfg_a[1] ? _T_895 : ~io_dpath_pmp_6_cfg_a[0] | _T_932; // @[PMP.scala 120:8]
  wire  _T_941 = count == 2'h1 ? io_dpath_pmp_7_mask[20] : io_dpath_pmp_7_mask[29]; // @[package.scala 32:76]
  wire  _T_943 = count == 2'h2 ? io_dpath_pmp_7_mask[11] : _T_941; // @[package.scala 32:76]
  wire  _T_945 = count == 2'h3 ? io_dpath_pmp_7_mask[11] : _T_943; // @[package.scala 32:76]
  wire [31:0] _T_946 = {io_dpath_pmp_7_addr, 2'h0}; // @[PMP.scala 62:36]
  wire [31:0] _T_947 = ~_T_946; // @[PMP.scala 62:29]
  wire [31:0] _T_948 = _T_947 | 32'h3; // @[PMP.scala 62:48]
  wire [31:0] _T_949 = ~_T_948; // @[PMP.scala 62:27]
  wire [65:0] _GEN_210 = {{34'd0}, _T_949}; // @[PMP.scala 100:53]
  wire [65:0] _T_950 = _T_388 ^ _GEN_210; // @[PMP.scala 100:53]
  wire  _T_952 = _T_950[65:30] != 36'h0; // @[PMP.scala 100:78]
  wire  _T_959 = _T_950[65:21] != 45'h0; // @[PMP.scala 100:78]
  wire  _T_966 = _T_950[65:12] != 54'h0; // @[PMP.scala 100:78]
  wire  _T_968 = count == 2'h1 ? _T_959 : _T_952; // @[package.scala 32:76]
  wire  _T_970 = count == 2'h2 ? _T_966 : _T_968; // @[package.scala 32:76]
  wire  _T_972 = count == 2'h3 ? _T_966 : _T_970; // @[package.scala 32:76]
  wire  _T_973 = _T_945 | _T_972; // @[PMP.scala 100:21]
  wire  _T_987 = ~(_T_388 < _GEN_210); // @[PMP.scala 109:28]
  wire [31:0] _T_1006 = _T_949 & _T_447; // @[PMP.scala 113:53]
  wire [65:0] _GEN_218 = {{34'd0}, _T_1006}; // @[PMP.scala 113:40]
  wire  _T_1007 = _T_448 < _GEN_218; // @[PMP.scala 113:40]
  wire  _T_1010 = _T_929 | _T_987 | _T_909 & _T_1007; // @[PMP.scala 115:41]
  wire  _T_1012 = io_dpath_pmp_7_cfg_a[1] ? _T_973 : ~io_dpath_pmp_7_cfg_a[0] | _T_1010; // @[PMP.scala 120:8]
  wire  pmpHomogeneous = _T_466 & _T_544 & _T_622 & _T_700 & _T_778 & _T_856 & _T_934 & _T_1012; // @[PMP.scala 140:10]
  wire  homogeneous = pmaHomogeneous & pmpHomogeneous; // @[PTW.scala 269:36]
  wire  _T_1019 = 3'h0 == state; // @[Conditional.scala 37:30]
  wire [2:0] _T_1021 = arb_io_out_bits_valid ? 3'h1 : 3'h0; // @[PTW.scala 291:26]
  wire [2:0] _GEN_56 = _T_55 ? _T_1021 : state; // @[PTW.scala 290:32 291:20]
  wire  _T_1024 = 3'h1 == state; // @[Conditional.scala 37:30]
  wire [1:0] _T_1026 = count + 2'h1; // @[PTW.scala 297:24]
  wire [2:0] _T_1027 = io_mem_req_ready ? 3'h2 : 3'h1; // @[PTW.scala 299:26]
  wire [1:0] _GEN_57 = pte_cache_hit ? _T_1026 : count; // @[PTW.scala 296:28 297:15 119:18]
  wire [2:0] _GEN_58 = pte_cache_hit ? state : _T_1027; // @[PTW.scala 296:28 299:20]
  wire  _T_1028 = 3'h2 == state; // @[Conditional.scala 37:30]
  wire [2:0] _T_1029 = s2_hit ? 3'h1 : 3'h4; // @[PTW.scala 304:24]
  wire  _T_1030 = 3'h4 == state; // @[Conditional.scala 37:30]
  wire  _GEN_59 = 2'h0 == r_req_dest; // @[PTW.scala 109:23 311:{32,32}]
  wire  _GEN_60 = 2'h1 == r_req_dest; // @[PTW.scala 109:23 311:{32,32}]
  wire  _GEN_61 = 2'h2 == r_req_dest; // @[PTW.scala 109:23 311:{32,32}]
  wire [2:0] _GEN_63 = io_mem_s2_xcpt_ae_ld ? 3'h0 : 3'h5; // @[PTW.scala 307:18 308:35 310:20]
  wire  _GEN_64 = io_mem_s2_xcpt_ae_ld & _GEN_59; // @[PTW.scala 109:23 308:35]
  wire  _GEN_65 = io_mem_s2_xcpt_ae_ld & _GEN_60; // @[PTW.scala 109:23 308:35]
  wire  _GEN_66 = io_mem_s2_xcpt_ae_ld & _GEN_61; // @[PTW.scala 109:23 308:35]
  wire  _T_1033 = 3'h7 == state; // @[Conditional.scala 37:30]
  wire  _T_1036 = ~homogeneous; // @[PTW.scala 318:13]
  wire [1:0] _GEN_70 = ~homogeneous ? 2'h2 : count; // @[PTW.scala 318:27 319:15 119:18]
  wire [2:0] _GEN_72 = _T_1033 ? 3'h0 : state; // @[Conditional.scala 39:67 PTW.scala 315:18]
  wire  _GEN_73 = _T_1033 & _GEN_59; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire  _GEN_74 = _T_1033 & _GEN_60; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire  _GEN_75 = _T_1033 & _GEN_61; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire [1:0] _GEN_77 = _T_1033 ? _GEN_70 : count; // @[Conditional.scala 39:67 PTW.scala 119:18]
  wire [2:0] _GEN_79 = _T_1030 ? _GEN_63 : _GEN_72; // @[Conditional.scala 39:67]
  wire  _GEN_81 = _T_1030 ? _GEN_64 : _GEN_73; // @[Conditional.scala 39:67]
  wire  _GEN_82 = _T_1030 ? _GEN_65 : _GEN_74; // @[Conditional.scala 39:67]
  wire  _GEN_83 = _T_1030 ? _GEN_66 : _GEN_75; // @[Conditional.scala 39:67]
  wire [1:0] _GEN_84 = _T_1030 ? count : _GEN_77; // @[Conditional.scala 39:67 PTW.scala 119:18]
  wire [2:0] _GEN_86 = _T_1028 ? _T_1029 : _GEN_79; // @[Conditional.scala 39:67 PTW.scala 304:18]
  wire  _GEN_87 = _T_1028 ? 1'h0 : _T_1030 & io_mem_s2_xcpt_ae_ld; // @[Conditional.scala 39:67 PTW.scala 120:24]
  wire  _GEN_88 = _T_1028 ? 1'h0 : _GEN_81; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire  _GEN_89 = _T_1028 ? 1'h0 : _GEN_82; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire  _GEN_90 = _T_1028 ? 1'h0 : _GEN_83; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire [1:0] _GEN_91 = _T_1028 ? count : _GEN_84; // @[Conditional.scala 39:67 PTW.scala 119:18]
  wire [1:0] _GEN_93 = _T_1024 ? _GEN_57 : _GEN_91; // @[Conditional.scala 39:67]
  wire [2:0] _GEN_94 = _T_1024 ? _GEN_58 : _GEN_86; // @[Conditional.scala 39:67]
  wire  _GEN_95 = _T_1024 ? 1'h0 : _GEN_87; // @[Conditional.scala 39:67 PTW.scala 120:24]
  wire  _GEN_96 = _T_1024 ? 1'h0 : _GEN_88; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire  _GEN_97 = _T_1024 ? 1'h0 : _GEN_89; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire  _GEN_98 = _T_1024 ? 1'h0 : _GEN_90; // @[Conditional.scala 39:67 PTW.scala 109:23]
  wire [2:0] _GEN_100 = _T_1019 ? _GEN_56 : _GEN_94; // @[Conditional.scala 40:58]
  wire [1:0] _GEN_101 = _T_1019 ? 2'h0 : _GEN_93; // @[Conditional.scala 40:58 PTW.scala 293:13]
  wire  _GEN_102 = _T_1019 ? 1'h0 : _GEN_95; // @[Conditional.scala 40:58 PTW.scala 120:24]
  wire  _GEN_103 = _T_1019 ? 1'h0 : _GEN_96; // @[Conditional.scala 40:58 PTW.scala 109:23]
  wire  _GEN_104 = _T_1019 ? 1'h0 : _GEN_97; // @[Conditional.scala 40:58 PTW.scala 109:23]
  wire  _GEN_105 = _T_1019 ? 1'h0 : _GEN_98; // @[Conditional.scala 40:58 PTW.scala 109:23]
  wire  _T_1038 = s2_hit & ~l2_error; // @[PTW.scala 332:16]
  wire [53:0] pte_2_ppn = {{10'd0}, io_dpath_ptbr_ppn};
  wire [53:0] _T_1045_ppn = _T_55 ? pte_2_ppn : r_pte_ppn; // @[PTW.scala 335:8]
  wire [53:0] pte_1_ppn = {{34'd0}, pte_cache_data};
  wire [53:0] _T_1046_ppn = _T_119 & pte_cache_hit ? pte_1_ppn : _T_1045_ppn; // @[PTW.scala 334:8]
  wire [1:0] _T_1046_reserved_for_software = _T_119 & pte_cache_hit ? 2'h0 : r_pte_reserved_for_software; // @[PTW.scala 334:8]
  wire  _T_1046_d = _T_119 & pte_cache_hit ? s2_entry_d : r_pte_d; // @[PTW.scala 334:8]
  wire  _T_1046_a = _T_119 & pte_cache_hit ? s2_entry_a : r_pte_a; // @[PTW.scala 334:8]
  wire  _T_1046_g = _T_119 & pte_cache_hit ? s2_g : r_pte_g; // @[PTW.scala 334:8]
  wire  _T_1046_u = _T_119 & pte_cache_hit ? s2_entry_u : r_pte_u; // @[PTW.scala 334:8]
  wire  _T_1046_x = _T_119 & pte_cache_hit ? s2_entry_x : r_pte_x; // @[PTW.scala 334:8]
  wire  _T_1046_w = _T_119 & pte_cache_hit ? s2_entry_w : r_pte_w; // @[PTW.scala 334:8]
  wire  _T_1046_r = _T_119 & pte_cache_hit ? s2_entry_r : r_pte_r; // @[PTW.scala 334:8]
  wire  _T_1046_v = _T_119 & pte_cache_hit | r_pte_v; // @[PTW.scala 334:8]
  wire [53:0] _T_1047_ppn = state == 3'h7 & _T_1036 ? fragmented_superpage_ppn : _T_1046_ppn; // @[PTW.scala 333:8]
  wire [1:0] _T_1047_reserved_for_software = state == 3'h7 & _T_1036 ? r_pte_reserved_for_software :
    _T_1046_reserved_for_software; // @[PTW.scala 333:8]
  wire  _T_1047_d = state == 3'h7 & _T_1036 ? r_pte_d : _T_1046_d; // @[PTW.scala 333:8]
  wire  _T_1047_a = state == 3'h7 & _T_1036 ? r_pte_a : _T_1046_a; // @[PTW.scala 333:8]
  wire  _T_1047_g = state == 3'h7 & _T_1036 ? r_pte_g : _T_1046_g; // @[PTW.scala 333:8]
  wire  _T_1047_u = state == 3'h7 & _T_1036 ? r_pte_u : _T_1046_u; // @[PTW.scala 333:8]
  wire  _T_1047_x = state == 3'h7 & _T_1036 ? r_pte_x : _T_1046_x; // @[PTW.scala 333:8]
  wire  _T_1047_w = state == 3'h7 & _T_1036 ? r_pte_w : _T_1046_w; // @[PTW.scala 333:8]
  wire  _T_1047_r = state == 3'h7 & _T_1036 ? r_pte_r : _T_1046_r; // @[PTW.scala 333:8]
  wire  _T_1047_v = state == 3'h7 & _T_1036 ? r_pte_v : _T_1046_v; // @[PTW.scala 333:8]
  wire [53:0] s2_pte_ppn = {{34'd0}, s2_entry_ppn}; // @[PTW.scala 234:22 235:12]
  wire [53:0] _T_1048_ppn = s2_hit & ~l2_error ? s2_pte_ppn : _T_1047_ppn; // @[PTW.scala 332:8]
  wire [1:0] _T_1048_reserved_for_software = s2_hit & ~l2_error ? 2'h0 : _T_1047_reserved_for_software; // @[PTW.scala 332:8]
  wire  _T_1048_d = s2_hit & ~l2_error ? s2_entry_d : _T_1047_d; // @[PTW.scala 332:8]
  wire  _T_1048_a = s2_hit & ~l2_error ? s2_entry_a : _T_1047_a; // @[PTW.scala 332:8]
  wire  _T_1048_g = s2_hit & ~l2_error ? s2_g : _T_1047_g; // @[PTW.scala 332:8]
  wire  _T_1048_u = s2_hit & ~l2_error ? s2_entry_u : _T_1047_u; // @[PTW.scala 332:8]
  wire  _T_1048_x = s2_hit & ~l2_error ? s2_entry_x : _T_1047_x; // @[PTW.scala 332:8]
  wire  _T_1048_w = s2_hit & ~l2_error ? s2_entry_w : _T_1047_w; // @[PTW.scala 332:8]
  wire  _T_1048_r = s2_hit & ~l2_error ? s2_entry_r : _T_1047_r; // @[PTW.scala 332:8]
  wire  _T_1048_v = s2_hit & ~l2_error | _T_1047_v; // @[PTW.scala 332:8]
  wire  _GEN_107 = _GEN_59 | _GEN_103; // @[PTW.scala 341:{28,28}]
  wire  _GEN_108 = _GEN_60 | _GEN_104; // @[PTW.scala 341:{28,28}]
  wire  _GEN_109 = _GEN_61 | _GEN_105; // @[PTW.scala 341:{28,28}]
  wire [2:0] _GEN_110 = _T_1038 ? 3'h0 : _GEN_100; // @[PTW.scala 338:30 340:16]
  wire  _GEN_111 = _T_1038 ? _GEN_107 : _GEN_103; // @[PTW.scala 338:30]
  wire  _GEN_112 = _T_1038 ? _GEN_108 : _GEN_104; // @[PTW.scala 338:30]
  wire  _GEN_113 = _T_1038 ? _GEN_109 : _GEN_105; // @[PTW.scala 338:30]
  wire  _GEN_114 = _T_1038 ? 1'h0 : _GEN_102; // @[PTW.scala 338:30 342:13]
  wire [1:0] _GEN_115 = _T_1038 ? 2'h2 : _GEN_101; // @[PTW.scala 338:30 343:11]
  wire  ae = res_v & invalid_paddr; // @[PTW.scala 352:22]
  wire  _GEN_116 = _GEN_59 | _GEN_111; // @[PTW.scala 358:{32,32}]
  wire  _GEN_117 = _GEN_60 | _GEN_112; // @[PTW.scala 358:{32,32}]
  wire  _GEN_118 = _GEN_61 | _GEN_113; // @[PTW.scala 358:{32,32}]
  wire [2:0] _GEN_123 = traverse ? 3'h1 : 3'h0; // @[PTW.scala 347:21 348:18]
  wire  _GEN_125 = traverse ? 1'h0 : res_v & _T_36 & _T_44; // @[PTW.scala 347:21 189:26 351:17]
  wire [2:0] _GEN_130 = mem_resp_valid ? _GEN_123 : _GEN_110; // @[PTW.scala 345:25]
  Arbiter_19 arb ( // @[PTW.scala 105:19]
    .io_in_0_ready(arb_io_in_0_ready),
    .io_in_0_valid(arb_io_in_0_valid),
    .io_in_0_bits_valid(arb_io_in_0_bits_valid),
    .io_in_0_bits_bits_addr(arb_io_in_0_bits_bits_addr),
    .io_in_1_ready(arb_io_in_1_ready),
    .io_in_1_valid(arb_io_in_1_valid),
    .io_in_1_bits_valid(arb_io_in_1_bits_valid),
    .io_in_1_bits_bits_addr(arb_io_in_1_bits_bits_addr),
    .io_in_2_ready(arb_io_in_2_ready),
    .io_in_2_valid(arb_io_in_2_valid),
    .io_in_2_bits_valid(arb_io_in_2_bits_valid),
    .io_in_2_bits_bits_addr(arb_io_in_2_bits_bits_addr),
    .io_out_ready(arb_io_out_ready),
    .io_out_valid(arb_io_out_valid),
    .io_out_bits_valid(arb_io_out_bits_valid),
    .io_out_bits_bits_addr(arb_io_out_bits_bits_addr),
    .io_chosen(arb_io_chosen)
  );
  package_Anon_60 package_Anon ( // @[package.scala 213:21]
    .io_x(package_Anon_io_x),
    .io_y(package_Anon_io_y)
  );
  package_Anon_61 package_Anon_1 ( // @[package.scala 213:21]
    .io_x_ppn(package_Anon_1_io_x_ppn),
    .io_x_reserved_for_software(package_Anon_1_io_x_reserved_for_software),
    .io_x_d(package_Anon_1_io_x_d),
    .io_x_a(package_Anon_1_io_x_a),
    .io_x_g(package_Anon_1_io_x_g),
    .io_x_u(package_Anon_1_io_x_u),
    .io_x_x(package_Anon_1_io_x_x),
    .io_x_w(package_Anon_1_io_x_w),
    .io_x_r(package_Anon_1_io_x_r),
    .io_x_v(package_Anon_1_io_x_v),
    .io_y_ppn(package_Anon_1_io_y_ppn),
    .io_y_reserved_for_software(package_Anon_1_io_y_reserved_for_software),
    .io_y_d(package_Anon_1_io_y_d),
    .io_y_a(package_Anon_1_io_y_a),
    .io_y_g(package_Anon_1_io_y_g),
    .io_y_u(package_Anon_1_io_y_u),
    .io_y_x(package_Anon_1_io_y_x),
    .io_y_w(package_Anon_1_io_y_w),
    .io_y_r(package_Anon_1_io_y_r),
    .io_y_v(package_Anon_1_io_y_v)
  );
  assign l2_tlb_ram_s1_rdata_en = l2_tlb_ram_s1_rdata_en_pipe_0;
  assign l2_tlb_ram_s1_rdata_addr = l2_tlb_ram_s1_rdata_addr_pipe_0;
  assign l2_tlb_ram_s1_rdata_data = l2_tlb_ram[l2_tlb_ram_s1_rdata_addr]; // @[DescribedSRAM.scala 23:26]
  assign l2_tlb_ram__T_207_data = {_T_204,_T_203};
  assign l2_tlb_ram__T_207_addr = r_req_addr[9:0];
  assign l2_tlb_ram__T_207_mask = 1'h1;
  assign l2_tlb_ram__T_207_en = l2_refill & _T_75;
  assign io_requestor_0_req_ready = arb_io_in_0_ready; // @[PTW.scala 106:13]
  assign io_requestor_0_resp_valid = resp_valid_0; // @[PTW.scala 272:32]
  assign io_requestor_0_resp_bits_ae = resp_ae; // @[PTW.scala 273:34]
  assign io_requestor_0_resp_bits_pte_ppn = r_pte_ppn; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_reserved_for_software = r_pte_reserved_for_software; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_d = r_pte_d; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_a = r_pte_a; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_g = r_pte_g; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_u = r_pte_u; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_x = r_pte_x; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_w = r_pte_w; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_r = r_pte_r; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_pte_v = r_pte_v; // @[PTW.scala 274:35]
  assign io_requestor_0_resp_bits_level = count; // @[PTW.scala 275:37]
  assign io_requestor_0_resp_bits_fragmented_superpage = 1'h0; // @[PTW.scala 277:81]
  assign io_requestor_0_resp_bits_homogeneous = pmaHomogeneous & pmpHomogeneous; // @[PTW.scala 269:36]
  assign io_requestor_0_ptbr_mode = io_dpath_ptbr_mode; // @[PTW.scala 278:26]
  assign io_requestor_0_ptbr_asid = io_dpath_ptbr_asid; // @[PTW.scala 278:26]
  assign io_requestor_0_ptbr_ppn = io_dpath_ptbr_ppn; // @[PTW.scala 278:26]
  assign io_requestor_0_status_debug = io_dpath_status_debug; // @[PTW.scala 280:28]
  assign io_requestor_0_status_cease = io_dpath_status_cease; // @[PTW.scala 280:28]
  assign io_requestor_0_status_wfi = io_dpath_status_wfi; // @[PTW.scala 280:28]
  assign io_requestor_0_status_isa = io_dpath_status_isa; // @[PTW.scala 280:28]
  assign io_requestor_0_status_dprv = io_dpath_status_dprv; // @[PTW.scala 280:28]
  assign io_requestor_0_status_prv = io_dpath_status_prv; // @[PTW.scala 280:28]
  assign io_requestor_0_status_sd = io_dpath_status_sd; // @[PTW.scala 280:28]
  assign io_requestor_0_status_zero2 = io_dpath_status_zero2; // @[PTW.scala 280:28]
  assign io_requestor_0_status_sxl = io_dpath_status_sxl; // @[PTW.scala 280:28]
  assign io_requestor_0_status_uxl = io_dpath_status_uxl; // @[PTW.scala 280:28]
  assign io_requestor_0_status_sd_rv32 = io_dpath_status_sd_rv32; // @[PTW.scala 280:28]
  assign io_requestor_0_status_zero1 = io_dpath_status_zero1; // @[PTW.scala 280:28]
  assign io_requestor_0_status_tsr = io_dpath_status_tsr; // @[PTW.scala 280:28]
  assign io_requestor_0_status_tw = io_dpath_status_tw; // @[PTW.scala 280:28]
  assign io_requestor_0_status_tvm = io_dpath_status_tvm; // @[PTW.scala 280:28]
  assign io_requestor_0_status_mxr = io_dpath_status_mxr; // @[PTW.scala 280:28]
  assign io_requestor_0_status_sum = io_dpath_status_sum; // @[PTW.scala 280:28]
  assign io_requestor_0_status_mprv = io_dpath_status_mprv; // @[PTW.scala 280:28]
  assign io_requestor_0_status_xs = io_dpath_status_xs; // @[PTW.scala 280:28]
  assign io_requestor_0_status_fs = io_dpath_status_fs; // @[PTW.scala 280:28]
  assign io_requestor_0_status_mpp = io_dpath_status_mpp; // @[PTW.scala 280:28]
  assign io_requestor_0_status_vs = io_dpath_status_vs; // @[PTW.scala 280:28]
  assign io_requestor_0_status_spp = io_dpath_status_spp; // @[PTW.scala 280:28]
  assign io_requestor_0_status_mpie = io_dpath_status_mpie; // @[PTW.scala 280:28]
  assign io_requestor_0_status_hpie = io_dpath_status_hpie; // @[PTW.scala 280:28]
  assign io_requestor_0_status_spie = io_dpath_status_spie; // @[PTW.scala 280:28]
  assign io_requestor_0_status_upie = io_dpath_status_upie; // @[PTW.scala 280:28]
  assign io_requestor_0_status_mie = io_dpath_status_mie; // @[PTW.scala 280:28]
  assign io_requestor_0_status_hie = io_dpath_status_hie; // @[PTW.scala 280:28]
  assign io_requestor_0_status_sie = io_dpath_status_sie; // @[PTW.scala 280:28]
  assign io_requestor_0_status_uie = io_dpath_status_uie; // @[PTW.scala 280:28]
  assign io_requestor_0_pmp_0_cfg_l = io_dpath_pmp_0_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_0_cfg_res = io_dpath_pmp_0_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_0_cfg_a = io_dpath_pmp_0_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_0_cfg_x = io_dpath_pmp_0_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_0_cfg_w = io_dpath_pmp_0_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_0_cfg_r = io_dpath_pmp_0_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_0_addr = io_dpath_pmp_0_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_0_mask = io_dpath_pmp_0_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_cfg_l = io_dpath_pmp_1_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_cfg_res = io_dpath_pmp_1_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_cfg_a = io_dpath_pmp_1_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_cfg_x = io_dpath_pmp_1_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_cfg_w = io_dpath_pmp_1_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_cfg_r = io_dpath_pmp_1_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_addr = io_dpath_pmp_1_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_1_mask = io_dpath_pmp_1_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_cfg_l = io_dpath_pmp_2_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_cfg_res = io_dpath_pmp_2_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_cfg_a = io_dpath_pmp_2_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_cfg_x = io_dpath_pmp_2_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_cfg_w = io_dpath_pmp_2_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_cfg_r = io_dpath_pmp_2_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_addr = io_dpath_pmp_2_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_2_mask = io_dpath_pmp_2_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_cfg_l = io_dpath_pmp_3_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_cfg_res = io_dpath_pmp_3_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_cfg_a = io_dpath_pmp_3_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_cfg_x = io_dpath_pmp_3_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_cfg_w = io_dpath_pmp_3_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_cfg_r = io_dpath_pmp_3_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_addr = io_dpath_pmp_3_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_3_mask = io_dpath_pmp_3_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_cfg_l = io_dpath_pmp_4_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_cfg_res = io_dpath_pmp_4_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_cfg_a = io_dpath_pmp_4_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_cfg_x = io_dpath_pmp_4_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_cfg_w = io_dpath_pmp_4_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_cfg_r = io_dpath_pmp_4_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_addr = io_dpath_pmp_4_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_4_mask = io_dpath_pmp_4_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_cfg_l = io_dpath_pmp_5_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_cfg_res = io_dpath_pmp_5_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_cfg_a = io_dpath_pmp_5_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_cfg_x = io_dpath_pmp_5_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_cfg_w = io_dpath_pmp_5_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_cfg_r = io_dpath_pmp_5_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_addr = io_dpath_pmp_5_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_5_mask = io_dpath_pmp_5_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_cfg_l = io_dpath_pmp_6_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_cfg_res = io_dpath_pmp_6_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_cfg_a = io_dpath_pmp_6_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_cfg_x = io_dpath_pmp_6_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_cfg_w = io_dpath_pmp_6_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_cfg_r = io_dpath_pmp_6_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_addr = io_dpath_pmp_6_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_6_mask = io_dpath_pmp_6_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_cfg_l = io_dpath_pmp_7_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_cfg_res = io_dpath_pmp_7_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_cfg_a = io_dpath_pmp_7_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_cfg_x = io_dpath_pmp_7_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_cfg_w = io_dpath_pmp_7_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_cfg_r = io_dpath_pmp_7_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_addr = io_dpath_pmp_7_addr; // @[PTW.scala 281:25]
  assign io_requestor_0_pmp_7_mask = io_dpath_pmp_7_mask; // @[PTW.scala 281:25]
  assign io_requestor_0_customCSRs_csrs_0_wen = io_dpath_customCSRs_csrs_0_wen; // @[PTW.scala 279:32]
  assign io_requestor_0_customCSRs_csrs_0_wdata = io_dpath_customCSRs_csrs_0_wdata; // @[PTW.scala 279:32]
  assign io_requestor_0_customCSRs_csrs_0_value = io_dpath_customCSRs_csrs_0_value; // @[PTW.scala 279:32]
  assign io_requestor_1_req_ready = arb_io_in_1_ready; // @[PTW.scala 106:13]
  assign io_requestor_1_resp_valid = resp_valid_1; // @[PTW.scala 272:32]
  assign io_requestor_1_resp_bits_ae = resp_ae; // @[PTW.scala 273:34]
  assign io_requestor_1_resp_bits_pte_ppn = r_pte_ppn; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_reserved_for_software = r_pte_reserved_for_software; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_d = r_pte_d; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_a = r_pte_a; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_g = r_pte_g; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_u = r_pte_u; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_x = r_pte_x; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_w = r_pte_w; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_r = r_pte_r; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_pte_v = r_pte_v; // @[PTW.scala 274:35]
  assign io_requestor_1_resp_bits_level = count; // @[PTW.scala 275:37]
  assign io_requestor_1_resp_bits_fragmented_superpage = 1'h0; // @[PTW.scala 277:81]
  assign io_requestor_1_resp_bits_homogeneous = pmaHomogeneous & pmpHomogeneous; // @[PTW.scala 269:36]
  assign io_requestor_1_ptbr_mode = io_dpath_ptbr_mode; // @[PTW.scala 278:26]
  assign io_requestor_1_ptbr_asid = io_dpath_ptbr_asid; // @[PTW.scala 278:26]
  assign io_requestor_1_ptbr_ppn = io_dpath_ptbr_ppn; // @[PTW.scala 278:26]
  assign io_requestor_1_status_debug = io_dpath_status_debug; // @[PTW.scala 280:28]
  assign io_requestor_1_status_cease = io_dpath_status_cease; // @[PTW.scala 280:28]
  assign io_requestor_1_status_wfi = io_dpath_status_wfi; // @[PTW.scala 280:28]
  assign io_requestor_1_status_isa = io_dpath_status_isa; // @[PTW.scala 280:28]
  assign io_requestor_1_status_dprv = io_dpath_status_dprv; // @[PTW.scala 280:28]
  assign io_requestor_1_status_prv = io_dpath_status_prv; // @[PTW.scala 280:28]
  assign io_requestor_1_status_sd = io_dpath_status_sd; // @[PTW.scala 280:28]
  assign io_requestor_1_status_zero2 = io_dpath_status_zero2; // @[PTW.scala 280:28]
  assign io_requestor_1_status_sxl = io_dpath_status_sxl; // @[PTW.scala 280:28]
  assign io_requestor_1_status_uxl = io_dpath_status_uxl; // @[PTW.scala 280:28]
  assign io_requestor_1_status_sd_rv32 = io_dpath_status_sd_rv32; // @[PTW.scala 280:28]
  assign io_requestor_1_status_zero1 = io_dpath_status_zero1; // @[PTW.scala 280:28]
  assign io_requestor_1_status_tsr = io_dpath_status_tsr; // @[PTW.scala 280:28]
  assign io_requestor_1_status_tw = io_dpath_status_tw; // @[PTW.scala 280:28]
  assign io_requestor_1_status_tvm = io_dpath_status_tvm; // @[PTW.scala 280:28]
  assign io_requestor_1_status_mxr = io_dpath_status_mxr; // @[PTW.scala 280:28]
  assign io_requestor_1_status_sum = io_dpath_status_sum; // @[PTW.scala 280:28]
  assign io_requestor_1_status_mprv = io_dpath_status_mprv; // @[PTW.scala 280:28]
  assign io_requestor_1_status_xs = io_dpath_status_xs; // @[PTW.scala 280:28]
  assign io_requestor_1_status_fs = io_dpath_status_fs; // @[PTW.scala 280:28]
  assign io_requestor_1_status_mpp = io_dpath_status_mpp; // @[PTW.scala 280:28]
  assign io_requestor_1_status_vs = io_dpath_status_vs; // @[PTW.scala 280:28]
  assign io_requestor_1_status_spp = io_dpath_status_spp; // @[PTW.scala 280:28]
  assign io_requestor_1_status_mpie = io_dpath_status_mpie; // @[PTW.scala 280:28]
  assign io_requestor_1_status_hpie = io_dpath_status_hpie; // @[PTW.scala 280:28]
  assign io_requestor_1_status_spie = io_dpath_status_spie; // @[PTW.scala 280:28]
  assign io_requestor_1_status_upie = io_dpath_status_upie; // @[PTW.scala 280:28]
  assign io_requestor_1_status_mie = io_dpath_status_mie; // @[PTW.scala 280:28]
  assign io_requestor_1_status_hie = io_dpath_status_hie; // @[PTW.scala 280:28]
  assign io_requestor_1_status_sie = io_dpath_status_sie; // @[PTW.scala 280:28]
  assign io_requestor_1_status_uie = io_dpath_status_uie; // @[PTW.scala 280:28]
  assign io_requestor_1_pmp_0_cfg_l = io_dpath_pmp_0_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_0_cfg_res = io_dpath_pmp_0_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_0_cfg_a = io_dpath_pmp_0_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_0_cfg_x = io_dpath_pmp_0_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_0_cfg_w = io_dpath_pmp_0_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_0_cfg_r = io_dpath_pmp_0_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_0_addr = io_dpath_pmp_0_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_0_mask = io_dpath_pmp_0_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_cfg_l = io_dpath_pmp_1_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_cfg_res = io_dpath_pmp_1_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_cfg_a = io_dpath_pmp_1_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_cfg_x = io_dpath_pmp_1_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_cfg_w = io_dpath_pmp_1_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_cfg_r = io_dpath_pmp_1_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_addr = io_dpath_pmp_1_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_1_mask = io_dpath_pmp_1_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_cfg_l = io_dpath_pmp_2_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_cfg_res = io_dpath_pmp_2_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_cfg_a = io_dpath_pmp_2_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_cfg_x = io_dpath_pmp_2_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_cfg_w = io_dpath_pmp_2_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_cfg_r = io_dpath_pmp_2_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_addr = io_dpath_pmp_2_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_2_mask = io_dpath_pmp_2_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_cfg_l = io_dpath_pmp_3_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_cfg_res = io_dpath_pmp_3_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_cfg_a = io_dpath_pmp_3_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_cfg_x = io_dpath_pmp_3_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_cfg_w = io_dpath_pmp_3_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_cfg_r = io_dpath_pmp_3_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_addr = io_dpath_pmp_3_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_3_mask = io_dpath_pmp_3_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_cfg_l = io_dpath_pmp_4_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_cfg_res = io_dpath_pmp_4_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_cfg_a = io_dpath_pmp_4_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_cfg_x = io_dpath_pmp_4_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_cfg_w = io_dpath_pmp_4_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_cfg_r = io_dpath_pmp_4_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_addr = io_dpath_pmp_4_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_4_mask = io_dpath_pmp_4_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_cfg_l = io_dpath_pmp_5_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_cfg_res = io_dpath_pmp_5_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_cfg_a = io_dpath_pmp_5_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_cfg_x = io_dpath_pmp_5_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_cfg_w = io_dpath_pmp_5_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_cfg_r = io_dpath_pmp_5_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_addr = io_dpath_pmp_5_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_5_mask = io_dpath_pmp_5_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_cfg_l = io_dpath_pmp_6_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_cfg_res = io_dpath_pmp_6_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_cfg_a = io_dpath_pmp_6_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_cfg_x = io_dpath_pmp_6_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_cfg_w = io_dpath_pmp_6_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_cfg_r = io_dpath_pmp_6_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_addr = io_dpath_pmp_6_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_6_mask = io_dpath_pmp_6_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_cfg_l = io_dpath_pmp_7_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_cfg_res = io_dpath_pmp_7_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_cfg_a = io_dpath_pmp_7_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_cfg_x = io_dpath_pmp_7_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_cfg_w = io_dpath_pmp_7_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_cfg_r = io_dpath_pmp_7_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_addr = io_dpath_pmp_7_addr; // @[PTW.scala 281:25]
  assign io_requestor_1_pmp_7_mask = io_dpath_pmp_7_mask; // @[PTW.scala 281:25]
  assign io_requestor_1_customCSRs_csrs_0_wen = io_dpath_customCSRs_csrs_0_wen; // @[PTW.scala 279:32]
  assign io_requestor_1_customCSRs_csrs_0_wdata = io_dpath_customCSRs_csrs_0_wdata; // @[PTW.scala 279:32]
  assign io_requestor_1_customCSRs_csrs_0_value = io_dpath_customCSRs_csrs_0_value; // @[PTW.scala 279:32]
  assign io_requestor_2_req_ready = arb_io_in_2_ready; // @[PTW.scala 106:13]
  assign io_requestor_2_resp_valid = resp_valid_2; // @[PTW.scala 272:32]
  assign io_requestor_2_resp_bits_ae = resp_ae; // @[PTW.scala 273:34]
  assign io_requestor_2_resp_bits_pte_ppn = r_pte_ppn; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_reserved_for_software = r_pte_reserved_for_software; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_d = r_pte_d; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_a = r_pte_a; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_g = r_pte_g; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_u = r_pte_u; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_x = r_pte_x; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_w = r_pte_w; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_r = r_pte_r; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_pte_v = r_pte_v; // @[PTW.scala 274:35]
  assign io_requestor_2_resp_bits_level = count; // @[PTW.scala 275:37]
  assign io_requestor_2_resp_bits_fragmented_superpage = 1'h0; // @[PTW.scala 277:81]
  assign io_requestor_2_resp_bits_homogeneous = pmaHomogeneous & pmpHomogeneous; // @[PTW.scala 269:36]
  assign io_requestor_2_ptbr_mode = io_dpath_ptbr_mode; // @[PTW.scala 278:26]
  assign io_requestor_2_ptbr_asid = io_dpath_ptbr_asid; // @[PTW.scala 278:26]
  assign io_requestor_2_ptbr_ppn = io_dpath_ptbr_ppn; // @[PTW.scala 278:26]
  assign io_requestor_2_status_debug = io_dpath_status_debug; // @[PTW.scala 280:28]
  assign io_requestor_2_status_cease = io_dpath_status_cease; // @[PTW.scala 280:28]
  assign io_requestor_2_status_wfi = io_dpath_status_wfi; // @[PTW.scala 280:28]
  assign io_requestor_2_status_isa = io_dpath_status_isa; // @[PTW.scala 280:28]
  assign io_requestor_2_status_dprv = io_dpath_status_dprv; // @[PTW.scala 280:28]
  assign io_requestor_2_status_prv = io_dpath_status_prv; // @[PTW.scala 280:28]
  assign io_requestor_2_status_sd = io_dpath_status_sd; // @[PTW.scala 280:28]
  assign io_requestor_2_status_zero2 = io_dpath_status_zero2; // @[PTW.scala 280:28]
  assign io_requestor_2_status_sxl = io_dpath_status_sxl; // @[PTW.scala 280:28]
  assign io_requestor_2_status_uxl = io_dpath_status_uxl; // @[PTW.scala 280:28]
  assign io_requestor_2_status_sd_rv32 = io_dpath_status_sd_rv32; // @[PTW.scala 280:28]
  assign io_requestor_2_status_zero1 = io_dpath_status_zero1; // @[PTW.scala 280:28]
  assign io_requestor_2_status_tsr = io_dpath_status_tsr; // @[PTW.scala 280:28]
  assign io_requestor_2_status_tw = io_dpath_status_tw; // @[PTW.scala 280:28]
  assign io_requestor_2_status_tvm = io_dpath_status_tvm; // @[PTW.scala 280:28]
  assign io_requestor_2_status_mxr = io_dpath_status_mxr; // @[PTW.scala 280:28]
  assign io_requestor_2_status_sum = io_dpath_status_sum; // @[PTW.scala 280:28]
  assign io_requestor_2_status_mprv = io_dpath_status_mprv; // @[PTW.scala 280:28]
  assign io_requestor_2_status_xs = io_dpath_status_xs; // @[PTW.scala 280:28]
  assign io_requestor_2_status_fs = io_dpath_status_fs; // @[PTW.scala 280:28]
  assign io_requestor_2_status_mpp = io_dpath_status_mpp; // @[PTW.scala 280:28]
  assign io_requestor_2_status_vs = io_dpath_status_vs; // @[PTW.scala 280:28]
  assign io_requestor_2_status_spp = io_dpath_status_spp; // @[PTW.scala 280:28]
  assign io_requestor_2_status_mpie = io_dpath_status_mpie; // @[PTW.scala 280:28]
  assign io_requestor_2_status_hpie = io_dpath_status_hpie; // @[PTW.scala 280:28]
  assign io_requestor_2_status_spie = io_dpath_status_spie; // @[PTW.scala 280:28]
  assign io_requestor_2_status_upie = io_dpath_status_upie; // @[PTW.scala 280:28]
  assign io_requestor_2_status_mie = io_dpath_status_mie; // @[PTW.scala 280:28]
  assign io_requestor_2_status_hie = io_dpath_status_hie; // @[PTW.scala 280:28]
  assign io_requestor_2_status_sie = io_dpath_status_sie; // @[PTW.scala 280:28]
  assign io_requestor_2_status_uie = io_dpath_status_uie; // @[PTW.scala 280:28]
  assign io_requestor_2_pmp_0_cfg_l = io_dpath_pmp_0_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_0_cfg_res = io_dpath_pmp_0_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_0_cfg_a = io_dpath_pmp_0_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_0_cfg_x = io_dpath_pmp_0_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_0_cfg_w = io_dpath_pmp_0_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_0_cfg_r = io_dpath_pmp_0_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_0_addr = io_dpath_pmp_0_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_0_mask = io_dpath_pmp_0_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_cfg_l = io_dpath_pmp_1_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_cfg_res = io_dpath_pmp_1_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_cfg_a = io_dpath_pmp_1_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_cfg_x = io_dpath_pmp_1_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_cfg_w = io_dpath_pmp_1_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_cfg_r = io_dpath_pmp_1_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_addr = io_dpath_pmp_1_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_1_mask = io_dpath_pmp_1_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_cfg_l = io_dpath_pmp_2_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_cfg_res = io_dpath_pmp_2_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_cfg_a = io_dpath_pmp_2_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_cfg_x = io_dpath_pmp_2_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_cfg_w = io_dpath_pmp_2_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_cfg_r = io_dpath_pmp_2_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_addr = io_dpath_pmp_2_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_2_mask = io_dpath_pmp_2_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_cfg_l = io_dpath_pmp_3_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_cfg_res = io_dpath_pmp_3_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_cfg_a = io_dpath_pmp_3_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_cfg_x = io_dpath_pmp_3_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_cfg_w = io_dpath_pmp_3_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_cfg_r = io_dpath_pmp_3_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_addr = io_dpath_pmp_3_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_3_mask = io_dpath_pmp_3_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_cfg_l = io_dpath_pmp_4_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_cfg_res = io_dpath_pmp_4_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_cfg_a = io_dpath_pmp_4_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_cfg_x = io_dpath_pmp_4_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_cfg_w = io_dpath_pmp_4_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_cfg_r = io_dpath_pmp_4_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_addr = io_dpath_pmp_4_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_4_mask = io_dpath_pmp_4_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_cfg_l = io_dpath_pmp_5_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_cfg_res = io_dpath_pmp_5_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_cfg_a = io_dpath_pmp_5_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_cfg_x = io_dpath_pmp_5_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_cfg_w = io_dpath_pmp_5_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_cfg_r = io_dpath_pmp_5_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_addr = io_dpath_pmp_5_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_5_mask = io_dpath_pmp_5_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_cfg_l = io_dpath_pmp_6_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_cfg_res = io_dpath_pmp_6_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_cfg_a = io_dpath_pmp_6_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_cfg_x = io_dpath_pmp_6_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_cfg_w = io_dpath_pmp_6_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_cfg_r = io_dpath_pmp_6_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_addr = io_dpath_pmp_6_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_6_mask = io_dpath_pmp_6_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_cfg_l = io_dpath_pmp_7_cfg_l; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_cfg_res = io_dpath_pmp_7_cfg_res; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_cfg_a = io_dpath_pmp_7_cfg_a; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_cfg_x = io_dpath_pmp_7_cfg_x; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_cfg_w = io_dpath_pmp_7_cfg_w; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_cfg_r = io_dpath_pmp_7_cfg_r; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_addr = io_dpath_pmp_7_addr; // @[PTW.scala 281:25]
  assign io_requestor_2_pmp_7_mask = io_dpath_pmp_7_mask; // @[PTW.scala 281:25]
  assign io_requestor_2_customCSRs_csrs_0_wen = io_dpath_customCSRs_csrs_0_wen; // @[PTW.scala 279:32]
  assign io_requestor_2_customCSRs_csrs_0_wdata = io_dpath_customCSRs_csrs_0_wdata; // @[PTW.scala 279:32]
  assign io_requestor_2_customCSRs_csrs_0_value = io_dpath_customCSRs_csrs_0_value; // @[PTW.scala 279:32]
  assign io_mem_req_valid = _T_119 | state == 3'h3; // @[PTW.scala 247:39]
  assign io_mem_req_bits_addr = pte_addr[39:0]; // @[PTW.scala 252:24]
  assign io_mem_req_bits_tag = 7'h0;
  assign io_mem_req_bits_cmd = 5'h0; // @[PTW.scala 249:24]
  assign io_mem_req_bits_size = 2'h3; // @[PTW.scala 250:24]
  assign io_mem_req_bits_signed = 1'h0; // @[PTW.scala 251:26]
  assign io_mem_req_bits_dprv = 2'h1; // @[PTW.scala 253:24]
  assign io_mem_req_bits_phys = 1'h1; // @[PTW.scala 248:24]
  assign io_mem_req_bits_no_alloc = 1'h0;
  assign io_mem_req_bits_no_xcpt = 1'h0;
  assign io_mem_req_bits_data = 64'h0;
  assign io_mem_req_bits_mask = 8'h0;
  assign io_mem_s1_kill = s2_hit | state != 3'h2; // @[PTW.scala 254:28]
  assign io_mem_s1_data_data = 64'h0;
  assign io_mem_s1_data_mask = 8'h0;
  assign io_mem_s2_kill = 1'h0; // @[PTW.scala 255:18]
  assign io_mem_keep_clock_enabled = 1'h0;
  assign io_dpath_perf_l2miss = s2_valid & ~(s2_valid_bit & _T_247); // @[PTW.scala 233:38]
  assign io_dpath_clock_enabled = state != 3'h0 | arb_io_out_valid | io_dpath_sfence_valid |
    io_dpath_customCSRs_csrs_0_value[0]; // @[PTW.scala 111:81]
  assign arb_io_in_0_valid = io_requestor_0_req_valid; // @[PTW.scala 106:13]
  assign arb_io_in_0_bits_valid = io_requestor_0_req_bits_valid; // @[PTW.scala 106:13]
  assign arb_io_in_0_bits_bits_addr = io_requestor_0_req_bits_bits_addr; // @[PTW.scala 106:13]
  assign arb_io_in_1_valid = io_requestor_1_req_valid; // @[PTW.scala 106:13]
  assign arb_io_in_1_bits_valid = io_requestor_1_req_bits_valid; // @[PTW.scala 106:13]
  assign arb_io_in_1_bits_bits_addr = io_requestor_1_req_bits_bits_addr; // @[PTW.scala 106:13]
  assign arb_io_in_2_valid = io_requestor_2_req_valid; // @[PTW.scala 106:13]
  assign arb_io_in_2_bits_valid = io_requestor_2_req_bits_valid; // @[PTW.scala 106:13]
  assign arb_io_in_2_bits_bits_addr = io_requestor_2_req_bits_bits_addr; // @[PTW.scala 106:13]
  assign arb_io_out_ready = state == 3'h0; // @[PTW.scala 107:29]
  assign package_Anon_io_x = io_mem_s2_nack ? 3'h1 : _GEN_130; // @[PTW.scala 362:25 364:16]
  assign package_Anon_1_io_x_ppn = mem_resp_valid ? res_ppn : _T_1048_ppn; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_reserved_for_software = mem_resp_valid ? tmp_reserved_for_software :
    _T_1048_reserved_for_software; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_d = mem_resp_valid ? tmp_d : _T_1048_d; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_a = mem_resp_valid ? tmp_a : _T_1048_a; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_g = mem_resp_valid ? tmp_g : _T_1048_g; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_u = mem_resp_valid ? tmp_u : _T_1048_u; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_x = mem_resp_valid ? tmp_x : _T_1048_x; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_w = mem_resp_valid ? tmp_w : _T_1048_w; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_r = mem_resp_valid ? tmp_r : _T_1048_r; // @[PTW.scala 331:8]
  assign package_Anon_1_io_x_v = mem_resp_valid ? res_v : _T_1048_v; // @[PTW.scala 331:8]
  always @(posedge clock) begin
    if (l2_tlb_ram__T_207_en & l2_tlb_ram__T_207_mask) begin
      l2_tlb_ram[l2_tlb_ram__T_207_addr] <= l2_tlb_ram__T_207_data; // @[DescribedSRAM.scala 23:26]
    end
    l2_tlb_ram_s1_rdata_en_pipe_0 <= _T_220 & _T_55;
    if (_T_220 & _T_55) begin
      l2_tlb_ram_s1_rdata_addr_pipe_0 <= arb_io_out_bits_bits_addr[9:0];
    end
    if (reset) begin // @[PTW.scala 103:18]
      state <= 3'h0; // @[PTW.scala 103:18]
    end else begin
      state <= package_Anon_io_y; // @[PTW.scala 286:9]
    end
    if (mem_resp_valid) begin // @[PTW.scala 345:25]
      if (traverse) begin // @[PTW.scala 347:21]
        resp_valid_0 <= _GEN_111;
      end else begin
        resp_valid_0 <= _GEN_116;
      end
    end else begin
      resp_valid_0 <= _GEN_111;
    end
    if (mem_resp_valid) begin // @[PTW.scala 345:25]
      if (traverse) begin // @[PTW.scala 347:21]
        resp_valid_1 <= _GEN_112;
      end else begin
        resp_valid_1 <= _GEN_117;
      end
    end else begin
      resp_valid_1 <= _GEN_112;
    end
    if (mem_resp_valid) begin // @[PTW.scala 345:25]
      if (traverse) begin // @[PTW.scala 347:21]
        resp_valid_2 <= _GEN_113;
      end else begin
        resp_valid_2 <= _GEN_118;
      end
    end else begin
      resp_valid_2 <= _GEN_113;
    end
    invalidated <= io_dpath_sfence_valid | invalidated & _T_2; // @[PTW.scala 245:40]
    if (mem_resp_valid) begin // @[PTW.scala 345:25]
      if (traverse) begin // @[PTW.scala 347:21]
        count <= _T_1026; // @[PTW.scala 349:13]
      end else begin
        count <= _GEN_115;
      end
    end else begin
      count <= _GEN_115;
    end
    if (mem_resp_valid) begin // @[PTW.scala 345:25]
      if (traverse) begin // @[PTW.scala 347:21]
        resp_ae <= _GEN_114;
      end else begin
        resp_ae <= ae; // @[PTW.scala 353:15]
      end
    end else begin
      resp_ae <= _GEN_114;
    end
    if (_T_55) begin // @[PTW.scala 160:28]
      r_req_addr <= arb_io_out_bits_bits_addr; // @[PTW.scala 161:11]
    end
    if (_T_55) begin // @[PTW.scala 160:28]
      r_req_dest <= arb_io_chosen; // @[PTW.scala 162:16]
    end
    r_pte_ppn <= package_Anon_1_io_y_ppn; // @[PTW.scala 330:9]
    r_pte_reserved_for_software <= package_Anon_1_io_y_reserved_for_software; // @[PTW.scala 330:9]
    r_pte_d <= package_Anon_1_io_y_d; // @[PTW.scala 330:9]
    r_pte_a <= package_Anon_1_io_y_a; // @[PTW.scala 330:9]
    r_pte_g <= package_Anon_1_io_y_g; // @[PTW.scala 330:9]
    r_pte_u <= package_Anon_1_io_y_u; // @[PTW.scala 330:9]
    r_pte_x <= package_Anon_1_io_y_x; // @[PTW.scala 330:9]
    r_pte_w <= package_Anon_1_io_y_w; // @[PTW.scala 330:9]
    r_pte_r <= package_Anon_1_io_y_r; // @[PTW.scala 330:9]
    r_pte_v <= package_Anon_1_io_y_v; // @[PTW.scala 330:9]
    mem_resp_valid <= io_mem_resp_valid; // @[PTW.scala 127:31]
    mem_resp_data <= io_mem_resp_bits_data; // @[PTW.scala 128:30]
    if (hit & state == 3'h1) begin // @[PTW.scala 180:35]
      _T_56 <= _T_158[7:1]; // @[Replacement.scala 44:15]
    end
    if (reset) begin // @[PTW.scala 168:24]
      valid <= 8'h0; // @[PTW.scala 168:24]
    end else if (io_dpath_sfence_valid & ~io_dpath_sfence_bits_rs1) begin // @[PTW.scala 181:63]
      valid <= 8'h0; // @[PTW.scala 181:71]
    end else if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      valid <= _T_118; // @[PTW.scala 176:13]
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h0 == r) begin // @[PTW.scala 177:15]
        tags_0 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h1 == r) begin // @[PTW.scala 177:15]
        tags_1 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h2 == r) begin // @[PTW.scala 177:15]
        tags_2 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h3 == r) begin // @[PTW.scala 177:15]
        tags_3 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h4 == r) begin // @[PTW.scala 177:15]
        tags_4 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h5 == r) begin // @[PTW.scala 177:15]
        tags_5 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h6 == r) begin // @[PTW.scala 177:15]
        tags_6 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h7 == r) begin // @[PTW.scala 177:15]
        tags_7 <= pte_addr[31:0]; // @[PTW.scala 177:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h0 == r) begin // @[PTW.scala 178:15]
        data_0 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h1 == r) begin // @[PTW.scala 178:15]
        data_1 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h2 == r) begin // @[PTW.scala 178:15]
        data_2 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h3 == r) begin // @[PTW.scala 178:15]
        data_3 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h4 == r) begin // @[PTW.scala 178:15]
        data_4 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h5 == r) begin // @[PTW.scala 178:15]
        data_5 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h6 == r) begin // @[PTW.scala 178:15]
        data_6 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    if (mem_resp_valid & traverse & ~hit & ~invalidated) begin // @[PTW.scala 174:63]
      if (3'h7 == r) begin // @[PTW.scala 178:15]
        data_7 <= res_ppn[19:0]; // @[PTW.scala 178:15]
      end
    end
    l2_refill <= mem_resp_valid & _GEN_125; // @[PTW.scala 345:25 189:26]
    if (l2_refill & _T_75) begin // @[PTW.scala 206:38]
      if (r_pte_g) begin // @[PTW.scala 214:15]
        g <= _T_209;
      end else begin
        g <= _T_211;
      end
    end
    if (reset) begin // @[PTW.scala 204:24]
      valid_1 <= 1024'h0; // @[PTW.scala 204:24]
    end else if (s2_valid & s2_valid_bit & l2_error) begin // @[PTW.scala 229:55]
      valid_1 <= 1024'h0; // @[PTW.scala 229:63]
    end else if (io_dpath_sfence_valid) begin // @[PTW.scala 216:34]
      if (io_dpath_sfence_bits_rs1) begin // @[PTW.scala 218:12]
        valid_1 <= _T_216;
      end else begin
        valid_1 <= _T_218;
      end
    end else if (l2_refill & _T_75) begin // @[PTW.scala 206:38]
      valid_1 <= _T_208; // @[PTW.scala 213:13]
    end
    s1_valid <= s0_valid & arb_io_out_bits_valid; // @[PTW.scala 223:37]
    s2_valid <= s1_valid; // @[PTW.scala 224:27]
    if (s1_valid) begin // @[Reg.scala 16:19]
      _T_227 <= l2_tlb_ram_s1_rdata_data; // @[Reg.scala 16:23]
    end
    if (s1_valid) begin // @[Reg.scala 16:19]
      s2_valid_bit <= _T_230[0]; // @[Reg.scala 16:23]
    end
    if (s1_valid) begin // @[Reg.scala 16:19]
      s2_g <= _T_232[0]; // @[Reg.scala 16:23]
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1038 & ~(_T_119 | state == 3'h2 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at PTW.scala:339 assert(state === s_req || state === s_wait1)\n"); // @[PTW.scala 339:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1038 & ~(_T_119 | state == 3'h2 | reset)) begin
          $fatal; // @[PTW.scala 339:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (mem_resp_valid & ~(state == 3'h5 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at PTW.scala:346 assert(state === s_wait3)\n"); // @[PTW.scala 346:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (mem_resp_valid & ~(state == 3'h5 | reset)) begin
          $fatal; // @[PTW.scala 346:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_mem_s2_nack & ~(state == 3'h4 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at PTW.scala:363 assert(state === s_wait2)\n"); // @[PTW.scala 363:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_mem_s2_nack & ~(state == 3'h4 | reset)) begin
          $fatal; // @[PTW.scala 363:11]
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
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {2{`RANDOM}};
  for (initvar = 0; initvar < 1024; initvar = initvar+1)
    l2_tlb_ram[initvar] = _RAND_0[43:0];
`endif // RANDOMIZE_MEM_INIT
`ifdef RANDOMIZE_REG_INIT
  _RAND_1 = {1{`RANDOM}};
  l2_tlb_ram_s1_rdata_en_pipe_0 = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  l2_tlb_ram_s1_rdata_addr_pipe_0 = _RAND_2[9:0];
  _RAND_3 = {1{`RANDOM}};
  state = _RAND_3[2:0];
  _RAND_4 = {1{`RANDOM}};
  resp_valid_0 = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  resp_valid_1 = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  resp_valid_2 = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  invalidated = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  count = _RAND_8[1:0];
  _RAND_9 = {1{`RANDOM}};
  resp_ae = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  r_req_addr = _RAND_10[26:0];
  _RAND_11 = {1{`RANDOM}};
  r_req_dest = _RAND_11[1:0];
  _RAND_12 = {2{`RANDOM}};
  r_pte_ppn = _RAND_12[53:0];
  _RAND_13 = {1{`RANDOM}};
  r_pte_reserved_for_software = _RAND_13[1:0];
  _RAND_14 = {1{`RANDOM}};
  r_pte_d = _RAND_14[0:0];
  _RAND_15 = {1{`RANDOM}};
  r_pte_a = _RAND_15[0:0];
  _RAND_16 = {1{`RANDOM}};
  r_pte_g = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  r_pte_u = _RAND_17[0:0];
  _RAND_18 = {1{`RANDOM}};
  r_pte_x = _RAND_18[0:0];
  _RAND_19 = {1{`RANDOM}};
  r_pte_w = _RAND_19[0:0];
  _RAND_20 = {1{`RANDOM}};
  r_pte_r = _RAND_20[0:0];
  _RAND_21 = {1{`RANDOM}};
  r_pte_v = _RAND_21[0:0];
  _RAND_22 = {1{`RANDOM}};
  mem_resp_valid = _RAND_22[0:0];
  _RAND_23 = {2{`RANDOM}};
  mem_resp_data = _RAND_23[63:0];
  _RAND_24 = {1{`RANDOM}};
  _T_56 = _RAND_24[6:0];
  _RAND_25 = {1{`RANDOM}};
  valid = _RAND_25[7:0];
  _RAND_26 = {1{`RANDOM}};
  tags_0 = _RAND_26[31:0];
  _RAND_27 = {1{`RANDOM}};
  tags_1 = _RAND_27[31:0];
  _RAND_28 = {1{`RANDOM}};
  tags_2 = _RAND_28[31:0];
  _RAND_29 = {1{`RANDOM}};
  tags_3 = _RAND_29[31:0];
  _RAND_30 = {1{`RANDOM}};
  tags_4 = _RAND_30[31:0];
  _RAND_31 = {1{`RANDOM}};
  tags_5 = _RAND_31[31:0];
  _RAND_32 = {1{`RANDOM}};
  tags_6 = _RAND_32[31:0];
  _RAND_33 = {1{`RANDOM}};
  tags_7 = _RAND_33[31:0];
  _RAND_34 = {1{`RANDOM}};
  data_0 = _RAND_34[19:0];
  _RAND_35 = {1{`RANDOM}};
  data_1 = _RAND_35[19:0];
  _RAND_36 = {1{`RANDOM}};
  data_2 = _RAND_36[19:0];
  _RAND_37 = {1{`RANDOM}};
  data_3 = _RAND_37[19:0];
  _RAND_38 = {1{`RANDOM}};
  data_4 = _RAND_38[19:0];
  _RAND_39 = {1{`RANDOM}};
  data_5 = _RAND_39[19:0];
  _RAND_40 = {1{`RANDOM}};
  data_6 = _RAND_40[19:0];
  _RAND_41 = {1{`RANDOM}};
  data_7 = _RAND_41[19:0];
  _RAND_42 = {1{`RANDOM}};
  l2_refill = _RAND_42[0:0];
  _RAND_43 = {32{`RANDOM}};
  g = _RAND_43[1023:0];
  _RAND_44 = {32{`RANDOM}};
  valid_1 = _RAND_44[1023:0];
  _RAND_45 = {1{`RANDOM}};
  s1_valid = _RAND_45[0:0];
  _RAND_46 = {1{`RANDOM}};
  s2_valid = _RAND_46[0:0];
  _RAND_47 = {2{`RANDOM}};
  _T_227 = _RAND_47[43:0];
  _RAND_48 = {1{`RANDOM}};
  s2_valid_bit = _RAND_48[0:0];
  _RAND_49 = {1{`RANDOM}};
  s2_g = _RAND_49[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
