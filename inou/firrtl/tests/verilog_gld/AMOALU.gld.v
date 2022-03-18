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
  wire [7:0] _T_70 = io_mask[0] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_72 = io_mask[1] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_74 = io_mask[2] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_76 = io_mask[3] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_78 = io_mask[4] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_80 = io_mask[5] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_82 = io_mask[6] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_84 = io_mask[7] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [63:0] wmask = {_T_84,_T_82,_T_80,_T_78,_T_76,_T_74,_T_72,_T_70}; // @[Cat.scala 29:58]
  assign io_out = ~wmask; // @[AMOALU.scala 104:27]
  assign io_out_unmasked = ~wmask; // @[AMOALU.scala 104:27]
endmodule
