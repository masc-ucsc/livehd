module TrivialArith(
  input         clock,
  input         reset,
  input  [7:0]  io_value1,
  input  [7:0]  io_value2,
  input  [7:0]  io_value3,
  output [9:0]  io_outputAdd,
  output [15:0] io_outputMul
);
  wire [8:0] _io_outputAdd_T = io_value1 + io_value2; // @[TrivialArith.scala 16:29]
  wire [8:0] _GEN_0 = {{1'd0}, io_value3}; // @[TrivialArith.scala 16:42]
  assign io_outputAdd = _io_outputAdd_T + _GEN_0; // @[TrivialArith.scala 16:42]
  assign io_outputMul = io_value1 * io_value2; // @[TrivialArith.scala 17:29]
endmodule
