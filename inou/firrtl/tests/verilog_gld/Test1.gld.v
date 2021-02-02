module Test1(
  input         clock,
  input         reset,
  input         io_vec_set,
  input  [1:0]  io_vec_idx,
  input  [15:0] io_vec_ary_0,
  input  [15:0] io_vec_ary_1,
  input  [15:0] io_vec_ary_2,
  input  [15:0] io_vec_ary_3,
  output [15:0] io_vec_ary_out
);
  wire [15:0] _GEN_1 = 2'h1 == io_vec_idx ? io_vec_ary_1 : io_vec_ary_0; // @[Test1.scala 35:20 Test1.scala 35:20]
  wire [15:0] _GEN_2 = 2'h2 == io_vec_idx ? io_vec_ary_2 : _GEN_1; // @[Test1.scala 35:20 Test1.scala 35:20]
  wire [15:0] _GEN_3 = 2'h3 == io_vec_idx ? io_vec_ary_3 : _GEN_2; // @[Test1.scala 35:20 Test1.scala 35:20]
  assign io_vec_ary_out = io_vec_set ? _GEN_3 : 16'h0; // @[Test1.scala 33:21 Test1.scala 35:20 Test1.scala 38:20]
endmodule
