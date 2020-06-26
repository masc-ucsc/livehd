module Test1(
  input         clock,
  input         reset,
  input  [15:0] io_mux_value1,
  input  [15:0] io_mux_value2,
  input  [15:0] io_mux_value3,
  input         io_mux_sel1,
  input         io_mux_sel2,
  input         io_vec_set,
  input  [1:0]  io_vec_subAcc,
  input  [15:0] io_vec_0,
  input  [15:0] io_vec_1,
  input  [15:0] io_vec_2,
  input  [15:0] io_vec_3,
  output [15:0] io_mux_out,
  output [15:0] io_vec_subInd_out,
  output [15:0] io_vec_subAcc_out
);
  wire [15:0] _T = io_mux_sel2 ? io_mux_value2 : io_mux_value3; // @[Test1.scala 31:52]
  wire [15:0] _GEN_1 = 2'h1 == io_vec_subAcc ? io_vec_1 : io_vec_0; // @[Test1.scala 35:23]
  wire [15:0] _GEN_2 = 2'h2 == io_vec_subAcc ? io_vec_2 : _GEN_1; // @[Test1.scala 35:23]
  wire [15:0] _GEN_3 = 2'h3 == io_vec_subAcc ? io_vec_3 : _GEN_2; // @[Test1.scala 35:23]
  assign io_mux_out = io_mux_sel1 ? io_mux_value1 : _T; // @[Test1.scala 31:14]
  assign io_vec_subInd_out = io_vec_set ? io_vec_1 : 16'h0; // @[Test1.scala 34:23 Test1.scala 37:23]
  assign io_vec_subAcc_out = io_vec_set ? _GEN_3 : 16'h0; // @[Test1.scala 35:23 Test1.scala 38:23]
endmodule
