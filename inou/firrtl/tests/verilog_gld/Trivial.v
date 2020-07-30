module Trivial(
  input   clock,
  input   reset,
  input   io_a,
  input   io_b,
  output  io_x
);
  assign io_x = io_a & io_b; // @[Trivial.scala 14:8]
endmodule
