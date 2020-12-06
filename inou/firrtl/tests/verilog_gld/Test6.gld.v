module Test6(
  input         clock,
  input         reset,
  input  [15:0] io_in_0,
  input  [15:0] io_in_1,
  input  [15:0] io_in_2,
  input  [15:0] io_in_3,
  input  [15:0] io_in_4,
  input  [15:0] io_in_5,
  input  [15:0] io_in_6,
  input  [15:0] io_in_7,
  input  [15:0] io_in_8,
  input  [15:0] io_in_9,
  input  [15:0] io_in_10,
  input  [15:0] io_in_11,
  input  [15:0] io_in_12,
  input  [15:0] io_in_13,
  input  [15:0] io_in_14,
  input  [15:0] io_in_15,
  input  [15:0] io_in_16,
  input  [15:0] io_in_17,
  input  [15:0] io_in_18,
  input  [15:0] io_in_19,
  input  [4:0]  io_addr,
  output [15:0] io_out
);
  wire [15:0] _GEN_1 = 5'h1 == io_addr ? io_in_1 : io_in_0; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_2 = 5'h2 == io_addr ? io_in_2 : _GEN_1; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_3 = 5'h3 == io_addr ? io_in_3 : _GEN_2; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_4 = 5'h4 == io_addr ? io_in_4 : _GEN_3; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_5 = 5'h5 == io_addr ? io_in_5 : _GEN_4; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_6 = 5'h6 == io_addr ? io_in_6 : _GEN_5; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_7 = 5'h7 == io_addr ? io_in_7 : _GEN_6; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_8 = 5'h8 == io_addr ? io_in_8 : _GEN_7; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_9 = 5'h9 == io_addr ? io_in_9 : _GEN_8; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_10 = 5'ha == io_addr ? io_in_10 : _GEN_9; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_11 = 5'hb == io_addr ? io_in_11 : _GEN_10; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_12 = 5'hc == io_addr ? io_in_12 : _GEN_11; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_13 = 5'hd == io_addr ? io_in_13 : _GEN_12; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_14 = 5'he == io_addr ? io_in_14 : _GEN_13; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_15 = 5'hf == io_addr ? io_in_15 : _GEN_14; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_16 = 5'h10 == io_addr ? io_in_16 : _GEN_15; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_17 = 5'h11 == io_addr ? io_in_17 : _GEN_16; // @[Test6.scala 17:10 Test6.scala 17:10]
  wire [15:0] _GEN_18 = 5'h12 == io_addr ? io_in_18 : _GEN_17; // @[Test6.scala 17:10 Test6.scala 17:10]
  assign io_out = 5'h13 == io_addr ? io_in_19 : _GEN_18; // @[Test6.scala 17:10 Test6.scala 17:10]
endmodule
