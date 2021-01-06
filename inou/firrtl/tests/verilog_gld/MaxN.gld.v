module MaxN(
  input        clock,
  input        reset,
  input  [7:0] io_ins_0,
  input  [7:0] io_ins_1,
  input  [7:0] io_ins_2,
  output [7:0] io_out
);
  wire [7:0] _io_out_T_1 = io_ins_0 > io_ins_1 ? io_ins_0 : io_ins_1; // @[MaxN.scala 13:43]
  assign io_out = _io_out_T_1 > io_ins_2 ? _io_out_T_1 : io_ins_2; // @[MaxN.scala 13:43]
endmodule
