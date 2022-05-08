module Arbiter_10(
  input         clock,
  input         reset,
  output        io_in_0_ready,
  input         io_in_0_valid,
  input  [5:0]  io_in_0_bits_req_0_idx,
  input  [3:0]  io_in_0_bits_req_0_way_en,
  input  [19:0] io_in_0_bits_req_0_tag,
  output        io_in_1_ready,
  input         io_in_1_valid,
  input  [5:0]  io_in_1_bits_req_0_idx,
  input  [3:0]  io_in_1_bits_req_0_way_en,
  input  [19:0] io_in_1_bits_req_0_tag,
  output        io_in_2_ready,
  input         io_in_2_valid,
  input  [5:0]  io_in_2_bits_req_0_idx,
  input  [3:0]  io_in_2_bits_req_0_way_en,
  input  [19:0] io_in_2_bits_req_0_tag,
  output        io_in_3_ready,
  input         io_in_3_valid,
  input  [5:0]  io_in_3_bits_req_0_idx,
  input  [3:0]  io_in_3_bits_req_0_way_en,
  input  [19:0] io_in_3_bits_req_0_tag,
  output        io_in_4_ready,
  input         io_in_4_valid,
  input  [5:0]  io_in_4_bits_req_0_idx,
  input  [3:0]  io_in_4_bits_req_0_way_en,
  input  [19:0] io_in_4_bits_req_0_tag,
  output        io_in_5_ready,
  input         io_in_5_valid,
  input  [5:0]  io_in_5_bits_req_0_idx,
  input  [3:0]  io_in_5_bits_req_0_way_en,
  input  [19:0] io_in_5_bits_req_0_tag,
  input         io_out_ready,
  output        io_out_valid,
  output [5:0]  io_out_bits_req_0_idx,
  output [3:0]  io_out_bits_req_0_way_en,
  output [19:0] io_out_bits_req_0_tag,
  output [2:0]  io_chosen
);
  wire [2:0] _GEN_0 = io_in_4_valid ? 3'h4 : 3'h5; // @[Arbiter.scala 123:13 126:27 127:17]
  wire [19:0] _GEN_1 = io_in_4_valid ? io_in_4_bits_req_0_tag : io_in_5_bits_req_0_tag; // @[Arbiter.scala 124:15 126:27 128:19]
  wire [3:0] _GEN_2 = io_in_4_valid ? io_in_4_bits_req_0_way_en : io_in_5_bits_req_0_way_en; // @[Arbiter.scala 124:15 126:27 128:19]
  wire [5:0] _GEN_3 = io_in_4_valid ? io_in_4_bits_req_0_idx : io_in_5_bits_req_0_idx; // @[Arbiter.scala 124:15 126:27 128:19]
  wire [2:0] _GEN_4 = io_in_3_valid ? 3'h3 : _GEN_0; // @[Arbiter.scala 126:27 127:17]
  wire [19:0] _GEN_5 = io_in_3_valid ? io_in_3_bits_req_0_tag : _GEN_1; // @[Arbiter.scala 126:27 128:19]
  wire [3:0] _GEN_6 = io_in_3_valid ? io_in_3_bits_req_0_way_en : _GEN_2; // @[Arbiter.scala 126:27 128:19]
  wire [5:0] _GEN_7 = io_in_3_valid ? io_in_3_bits_req_0_idx : _GEN_3; // @[Arbiter.scala 126:27 128:19]
  wire [2:0] _GEN_8 = io_in_2_valid ? 3'h2 : _GEN_4; // @[Arbiter.scala 126:27 127:17]
  wire [19:0] _GEN_9 = io_in_2_valid ? io_in_2_bits_req_0_tag : _GEN_5; // @[Arbiter.scala 126:27 128:19]
  wire [3:0] _GEN_10 = io_in_2_valid ? io_in_2_bits_req_0_way_en : _GEN_6; // @[Arbiter.scala 126:27 128:19]
  wire [5:0] _GEN_11 = io_in_2_valid ? io_in_2_bits_req_0_idx : _GEN_7; // @[Arbiter.scala 126:27 128:19]
  wire [2:0] _GEN_12 = io_in_1_valid ? 3'h1 : _GEN_8; // @[Arbiter.scala 126:27 127:17]
  wire [19:0] _GEN_13 = io_in_1_valid ? io_in_1_bits_req_0_tag : _GEN_9; // @[Arbiter.scala 126:27 128:19]
  wire [3:0] _GEN_14 = io_in_1_valid ? io_in_1_bits_req_0_way_en : _GEN_10; // @[Arbiter.scala 126:27 128:19]
  wire [5:0] _GEN_15 = io_in_1_valid ? io_in_1_bits_req_0_idx : _GEN_11; // @[Arbiter.scala 126:27 128:19]
  wire  grant_1 = ~io_in_0_valid; // @[Arbiter.scala 31:78]
  wire  grant_2 = ~(io_in_0_valid | io_in_1_valid); // @[Arbiter.scala 31:78]
  wire  grant_3 = ~(io_in_0_valid | io_in_1_valid | io_in_2_valid); // @[Arbiter.scala 31:78]
  wire  grant_4 = ~(io_in_0_valid | io_in_1_valid | io_in_2_valid | io_in_3_valid); // @[Arbiter.scala 31:78]
  wire  grant_5 = ~(io_in_0_valid | io_in_1_valid | io_in_2_valid | io_in_3_valid | io_in_4_valid); // @[Arbiter.scala 31:78]
  assign io_in_0_ready = io_out_ready; // @[Arbiter.scala 134:19]
  assign io_in_1_ready = grant_1 & io_out_ready; // @[Arbiter.scala 134:19]
  assign io_in_2_ready = grant_2 & io_out_ready; // @[Arbiter.scala 134:19]
  assign io_in_3_ready = grant_3 & io_out_ready; // @[Arbiter.scala 134:19]
  assign io_in_4_ready = grant_4 & io_out_ready; // @[Arbiter.scala 134:19]
  assign io_in_5_ready = grant_5 & io_out_ready; // @[Arbiter.scala 134:19]
  assign io_out_valid = ~grant_5 | io_in_5_valid; // @[Arbiter.scala 135:31]
  assign io_out_bits_req_0_idx = io_in_0_valid ? io_in_0_bits_req_0_idx : _GEN_15; // @[Arbiter.scala 126:27 128:19]
  assign io_out_bits_req_0_way_en = io_in_0_valid ? io_in_0_bits_req_0_way_en : _GEN_14; // @[Arbiter.scala 126:27 128:19]
  assign io_out_bits_req_0_tag = io_in_0_valid ? io_in_0_bits_req_0_tag : _GEN_13; // @[Arbiter.scala 126:27 128:19]
  assign io_chosen = io_in_0_valid ? 3'h0 : _GEN_12; // @[Arbiter.scala 126:27 127:17]
endmodule
