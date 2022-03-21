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
  wire  add = io_cmd == 5'h8; // @[AMOALU.scala 66:20]
  wire  _T_4 = io_cmd == 5'ha; // @[AMOALU.scala 67:26]
  wire  logic_and = io_cmd == 5'ha | io_cmd == 5'hb; // @[AMOALU.scala 67:38]
  wire  logic_xor = io_cmd == 5'h9 | _T_4; // @[AMOALU.scala 68:39]
  wire  _T_9 = ~io_mask[3]; // @[AMOALU.scala 72:63]
  wire [31:0] _T_10 = {_T_9, 31'h0}; // @[AMOALU.scala 72:79]
  wire [63:0] _T_11 = {{32'd0}, _T_10}; // @[AMOALU.scala 72:98]
  wire [63:0] _T_12 = ~_T_11; // @[AMOALU.scala 72:16]
  wire [63:0] _T_13 = io_lhs & _T_12; // @[AMOALU.scala 73:13]
  wire [63:0] _T_14 = io_rhs & _T_12; // @[AMOALU.scala 73:31]
  wire [63:0] adder_out = _T_13 + _T_14; // @[AMOALU.scala 73:21]
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
  wire [63:0] minmax = _T_54 ? io_lhs : io_rhs; // @[AMOALU.scala 94:19]
  wire [63:0] _T_55 = io_lhs & io_rhs; // @[AMOALU.scala 96:27]
  wire [63:0] _T_56 = logic_and ? _T_55 : 64'h0; // @[AMOALU.scala 96:8]
  wire [63:0] _T_57 = io_lhs ^ io_rhs; // @[AMOALU.scala 97:27]
  wire [63:0] _T_58 = logic_xor ? _T_57 : 64'h0; // @[AMOALU.scala 97:8]
  wire [63:0] logic_ = _T_56 | _T_58; // @[AMOALU.scala 96:42]
  wire [63:0] _T_60 = logic_and | logic_xor ? logic_ : minmax; // @[AMOALU.scala 100:8]
  wire [63:0] out = add ? adder_out : _T_60; // @[AMOALU.scala 99:8]
  wire [7:0] _T_70 = io_mask[0] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_72 = io_mask[1] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_74 = io_mask[2] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_76 = io_mask[3] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_78 = io_mask[4] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_80 = io_mask[5] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_82 = io_mask[6] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [7:0] _T_84 = io_mask[7] ? 8'hff : 8'h0; // @[Bitwise.scala 72:12]
  wire [63:0] wmask = {_T_84,_T_82,_T_80,_T_78,_T_76,_T_74,_T_72,_T_70}; // @[Cat.scala 29:58]
  wire [63:0] _T_91 = wmask & out; // @[AMOALU.scala 104:19]
  wire [63:0] _T_92 = ~wmask; // @[AMOALU.scala 104:27]
  wire [63:0] _T_93 = _T_92 & io_lhs; // @[AMOALU.scala 104:34]
  assign io_out = _T_91 | _T_93; // @[AMOALU.scala 104:25]
  assign io_out_unmasked = add ? adder_out : _T_60; // @[AMOALU.scala 99:8]
endmodule
