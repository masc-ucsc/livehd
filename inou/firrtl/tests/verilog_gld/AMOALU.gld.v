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
  assign io_out = {{32'd0}, io_lhs[63:32]}; // @[AMOALU.scala 104:10]
  assign io_out_unmasked = {{32'd0}, io_lhs[63:32]}; // @[AMOALU.scala 105:19]
endmodule
