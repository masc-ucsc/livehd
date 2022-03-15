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
  wire  max = io_cmd == 5'hd | io_cmd == 5'hf; // @[AMOALU.scala 64:33]
  wire  min = io_cmd == 5'hc | io_cmd == 5'he; // @[AMOALU.scala 65:33]
  wire [4:0] _T_18 = io_cmd & 5'h2; // @[AMOALU.scala 86:17]
  wire  _T_20 = _T_18 == 5'h0; // @[AMOALU.scala 86:25]
  wire  _T_32 = io_lhs[31:0] < io_rhs[31:0]; // @[AMOALU.scala 79:35]
  wire  _T_34 = io_lhs[63:32] < io_rhs[63:32] | io_lhs[63:32] == io_rhs[63:32] & _T_32; // @[AMOALU.scala 80:38]
  wire  _T_37 = _T_20 ? io_lhs[63] : io_rhs[63]; // @[AMOALU.scala 88:58]
  wire  _T_38 = io_lhs[63] == io_rhs[63] ? _T_34 : _T_37; // @[AMOALU.scala 88:10]
  wire  _T_52 = _T_20 ? io_lhs[31] : io_rhs[31]; // @[AMOALU.scala 88:58]
  wire  _T_53 = io_lhs[31] == io_rhs[31] ? _T_32 : _T_52; // @[AMOALU.scala 88:10]
  wire  less = io_mask[4] ? _T_38 : _T_53; // @[Mux.scala 47:69]
  wire  _T_54 = less ? min : max; // @[AMOALU.scala 94:23]
  assign io_out = {{63'd0}, _T_54}; // @[AMOALU.scala 104:10]
  assign io_out_unmasked = {{63'd0}, _T_54}; // @[AMOALU.scala 105:19]
endmodule
