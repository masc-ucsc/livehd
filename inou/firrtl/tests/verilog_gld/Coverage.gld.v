module Coverage(
  input        clock,
  input        reset,
  input  [3:0] io_in1,
  input  [3:0] io_in2,
  input  [1:0] io_in3,
  output       io_out1,
  output [3:0] io_out2
);
  assign io_out1 = io_in1 != 4'h0 ? &io_in2 : |io_in2;
  assign io_out2 = io_in2;
endmodule
