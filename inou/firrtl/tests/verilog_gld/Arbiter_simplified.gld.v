module Arbiter_simplified(
  input        clock,
  input        reset,
  input  [3:0] io_in_0_valid,
  input  [3:0] io_in_1_valid,
  output [3:0] io_out_0_valid,
  output [3:0] io_out_1_valid
);
  assign io_out_0_valid = io_in_0_valid;
  assign io_out_1_valid = io_in_1_valid;
endmodule
