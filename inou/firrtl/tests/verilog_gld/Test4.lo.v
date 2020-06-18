module Test4(
  input         clock,
  input         reset,
  input  [15:0] io_inp,
  output [15:0] io_out
);
  assign io_out = io_inp; // @[Test4.scala 18:10]
endmodule
