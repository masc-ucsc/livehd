module coverage(
  input        clock,
  input        reset,
  input  [3:0] io_in1,
  input  [3:0] io_in2,
  input  [1:0] io_in3,
  output       io_out1,
  output [3:0] io_out2
);
  wire  _T = io_in1 != 4'h0;
  wire  _T_1 = &io_in2;
  wire  _T_2 = |io_in2;
  assign io_out1 = _T ? _T_1 : _T_2;
  assign io_out2 = io_in2;
endmodule
