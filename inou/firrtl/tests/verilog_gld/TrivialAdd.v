module TrivialAdd(
  input        clock,
  input        reset,
  input  [7:0] io_value1,
  input  [7:0] io_value2,
  input  [7:0] io_value3,
  output [9:0] io_outputAdd,
  output [8:0] io_outputSub
);
  wire [8:0] _T = io_value1 + io_value2;
  wire [8:0] _GEN_0 = {{1'd0}, io_value3};
  assign io_outputAdd = _T + _GEN_0;
  assign io_outputSub = io_value1 - io_value2;
endmodule
