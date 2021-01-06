module Mux2(
  input   clock,
  input   reset,
  input   io_sel,
  input   io_in0,
  input   io_in1,
  output  io_out
);
  assign io_out = io_sel & io_in1 | ~io_sel & io_in0; // @[Mux2.scala 18:31]
endmodule
