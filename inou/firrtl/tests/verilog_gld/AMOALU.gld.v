module AMOALU(
  input         clock,
  input         reset,
  input  [7:0]  io_mask,
  input  [4:0]  io_cmd,
  input  [63:0] io_lhs,
  input  [63:0] io_rhs,
  output [63:0] io_out,
  output [63:0] io_out_unmasked
);
  wire  _T_9 = ~io_mask[3]; // @[AMOALU.scala 72:63]
  wire [31:0] _T_10 = {_T_9, 31'h0}; // @[AMOALU.scala 72:79]
  wire [63:0] _T_11 = {{32'd0}, _T_10}; // @[AMOALU.scala 72:98]
  wire [63:0] _T_12 = ~_T_11; // @[AMOALU.scala 72:16]
  wire [63:0] _T_13 = io_lhs & _T_12; // @[AMOALU.scala 73:13]
  wire [63:0] _T_14 = io_rhs & _T_12; // @[AMOALU.scala 73:31]
  wire [64:0] _T_15 = _T_13 + _T_14; // @[AMOALU.scala 73:21]
  assign io_out = _T_15[63:0]; // @[AMOALU.scala 104:10]
  assign io_out_unmasked = _T_15[63:0]; // @[AMOALU.scala 105:19]
endmodule
