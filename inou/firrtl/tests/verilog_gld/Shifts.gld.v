module Shifts(
  input        clock,
  input        reset,
  input  [3:0] io_in1,
  input  [3:0] io_in2,
  input  [3:0] io_in3,
  input        io_in4,
  output [3:0] io_out1,
  output [4:0] io_out2,
  output [2:0] io_out3,
  output [4:0] io_out4,
  output [3:0] io_out5
);
  wire [4:0] _GEN_0 = {{1'd0}, io_in3};
  assign io_out1 = {io_in1[3:2],io_in2[3:2]};
  assign io_out2 = {io_in3, 1'h0};
  assign io_out3 = io_in3[3:1];
  assign io_out4 = _GEN_0 << io_in4;
  assign io_out5 = io_in3 >> io_in4;
endmodule
