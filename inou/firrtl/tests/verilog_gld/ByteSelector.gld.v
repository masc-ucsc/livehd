module ByteSelector(
  input         clock,
  input         reset,
  input  [31:0] io_in,
  input  [1:0]  io_offset,
  output [7:0]  io_out
);
  wire [7:0] _GEN_0 = io_offset == 2'h2 ? io_in[23:16] : io_in[31:24]; // @[ByteSelector.scala 17:35 ByteSelector.scala 18:12 ByteSelector.scala 20:12]
  wire [7:0] _GEN_1 = io_offset == 2'h1 ? io_in[15:8] : _GEN_0; // @[ByteSelector.scala 15:35 ByteSelector.scala 16:12]
  assign io_out = io_offset == 2'h0 ? io_in[7:0] : _GEN_1; // @[ByteSelector.scala 13:33 ByteSelector.scala 14:12]
endmodule
