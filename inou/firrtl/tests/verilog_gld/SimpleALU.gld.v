module SimpleALU(
  input        clock,
  input        reset,
  input  [3:0] io_a,
  input  [3:0] io_b,
  input  [1:0] io_opcode,
  output [3:0] io_out
);
  wire [3:0] _T_2 = io_a + io_b; // @[SimpleALU.scala 46:20]
  wire [3:0] _T_5 = io_a - io_b; // @[SimpleALU.scala 48:20]
  wire [3:0] _GEN_0 = io_opcode == 2'h2 ? io_a : io_b; // @[SimpleALU.scala 49:35 SimpleALU.scala 50:12 SimpleALU.scala 52:12]
  wire [3:0] _GEN_1 = io_opcode == 2'h1 ? _T_5 : _GEN_0; // @[SimpleALU.scala 47:35 SimpleALU.scala 48:12]
  assign io_out = io_opcode == 2'h0 ? _T_2 : _GEN_1; // @[SimpleALU.scala 45:28 SimpleALU.scala 46:12]
endmodule
