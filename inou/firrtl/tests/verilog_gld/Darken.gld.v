module Darken(
  input        clock,
  input        reset,
  input  [7:0] io_in,
  output [7:0] io_out
);
  wire [8:0] _T = {io_in, 1'h0}; // @[Darken.scala 12:19]
  assign io_out = _T[7:0]; // @[Darken.scala 12:10]
endmodule
