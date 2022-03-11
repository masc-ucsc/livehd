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
  assign io_out = ~_T_11; // @[AMOALU.scala 72:16]
  assign io_out_unmasked = ~_T_11; // @[AMOALU.scala 72:16]
endmodule
