module Test5(
  input         clock,
  input         reset,
  input  [31:0] io_inp1,
  input  [31:0] io_inp2,
  output [31:0] io_out,
  output [3:0]  io_out20,
  output [3:0]  io_out21,
  output [3:0]  io_out22,
  output [7:0]  io_out23,
  output [3:0]  io_out3,
  output [15:0] io_out4
);
  wire [63:0] _T = $signed(io_inp1) * $signed(io_inp2); // @[Test5.scala 28:21]
  wire [51:0] _GEN_0 = _T[63:12]; // @[Test5.scala 28:10]
  assign io_out = _GEN_0[31:0]; // @[Test5.scala 28:10]
  assign io_out20 = 4'h5; // @[Test5.scala 30:12]
  assign io_out21 = 4'sh5; // @[Test5.scala 31:12]
  assign io_out22 = -4'sh5; // @[Test5.scala 32:12]
  assign io_out23 = -8'sh20; // @[Test5.scala 33:12]
  assign io_out3 = 4'ha; // @[Test5.scala 35:11]
  assign io_out4 = 16'habcd; // @[Test5.scala 36:11]
endmodule
