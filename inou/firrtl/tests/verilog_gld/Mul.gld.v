module Mul(
  input        clock,
  input        reset,
  input  [1:0] io_x,
  input  [1:0] io_y,
  output [3:0] io_z
);
  wire [3:0] _GEN_16 = {io_x, 2'h0}; // @[Mul.scala 25:21]
  wire [4:0] _io_z_T = {{1'd0}, _GEN_16}; // @[Mul.scala 25:21]
  wire [4:0] _GEN_17 = {{3'd0}, io_y}; // @[Mul.scala 25:29]
  wire [4:0] _io_z_T_1 = _io_z_T | _GEN_17; // @[Mul.scala 25:29]
  wire [3:0] _GEN_5 = 4'h5 == _io_z_T_1[3:0] ? 4'h1 : 4'h0; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_6 = 4'h6 == _io_z_T_1[3:0] ? 4'h2 : _GEN_5; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_7 = 4'h7 == _io_z_T_1[3:0] ? 4'h3 : _GEN_6; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_8 = 4'h8 == _io_z_T_1[3:0] ? 4'h0 : _GEN_7; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_9 = 4'h9 == _io_z_T_1[3:0] ? 4'h2 : _GEN_8; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_10 = 4'ha == _io_z_T_1[3:0] ? 4'h4 : _GEN_9; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_11 = 4'hb == _io_z_T_1[3:0] ? 4'h6 : _GEN_10; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_12 = 4'hc == _io_z_T_1[3:0] ? 4'h0 : _GEN_11; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_13 = 4'hd == _io_z_T_1[3:0] ? 4'h3 : _GEN_12; // @[Mul.scala 25:8 Mul.scala 25:8]
  wire [3:0] _GEN_14 = 4'he == _io_z_T_1[3:0] ? 4'h6 : _GEN_13; // @[Mul.scala 25:8 Mul.scala 25:8]
  assign io_z = 4'hf == _io_z_T_1[3:0] ? 4'h9 : _GEN_14; // @[Mul.scala 25:8 Mul.scala 25:8]
endmodule
