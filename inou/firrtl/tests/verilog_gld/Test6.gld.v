module Test6(
  input         clock,
  input         reset,
  input  [15:0] io_in_0,
  input  [15:0] io_in_1,
  input  [15:0] io_in_2,
  input  [4:0]  io_addr,
  output [15:0] io_out
);
  wire [15:0] _GEN_1 = 2'h1 == io_addr[1:0] ? io_in_1 : io_in_0; // @[Test6.scala 17:10 Test6.scala 17:10]
  assign io_out = 2'h2 == io_addr[1:0] ? io_in_2 : _GEN_1; // @[Test6.scala 17:10 Test6.scala 17:10]
endmodule
