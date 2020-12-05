module Trivial(
  input        clock,
  input        reset,
  output [3:0] io_outp
);
  assign io_outp = 4'h5; // @[Trivial.scala 22:11]
endmodule
