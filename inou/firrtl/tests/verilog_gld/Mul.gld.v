module Mul(
  input        clock,
  input        reset,
  input  [1:0] io_x,
  input  [1:0] io_y,
  output [1:0] io_z
);
  wire [3:0] _GEN_16 = {io_x, 2'h0}; // @[Mul.scala 25:21]
  wire [4:0] _T = {{1'd0}, _GEN_16}; // @[Mul.scala 25:21]
  wire [4:0] _GEN_17 = {{3'd0}, io_y}; // @[Mul.scala 25:29]
  wire [4:0] _T_1 = _T | _GEN_17; // @[Mul.scala 25:29]
  wire [1:0] _GEN_5 = 4'h5 == _T_1[3:0] ? 2'h1 : 2'h0; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_6 = 4'h6 == _T_1[3:0] ? 2'h2 : _GEN_5; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_7 = 4'h7 == _T_1[3:0] ? 2'h3 : _GEN_6; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_8 = 4'h8 == _T_1[3:0] ? 2'h0 : _GEN_7; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_9 = 4'h9 == _T_1[3:0] ? 2'h2 : _GEN_8; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_10 = 4'ha == _T_1[3:0] ? 2'h1 : _GEN_9; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_11 = 4'hb == _T_1[3:0] ? 2'h2 : _GEN_10; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_12 = 4'hc == _T_1[3:0] ? 2'h0 : _GEN_11; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_13 = 4'hd == _T_1[3:0] ? 2'h1 : _GEN_12; // @[Mul.scala 25:{8,8}]
  wire [1:0] _GEN_14 = 4'he == _T_1[3:0] ? 2'h2 : _GEN_13; // @[Mul.scala 25:{8,8}]
  assign io_z = 4'hf == _T_1[3:0] ? 2'h3 : _GEN_14; // @[Mul.scala 25:{8,8}]
endmodule
