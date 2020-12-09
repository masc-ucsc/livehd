module MaxN(
  input         clock,
  input         reset,
  input  [15:0] io_ins_0,
  input  [15:0] io_ins_1,
  input  [15:0] io_ins_2,
  input  [15:0] io_ins_3,
  input  [15:0] io_ins_4,
  input  [15:0] io_ins_5,
  input  [15:0] io_ins_6,
  input  [15:0] io_ins_7,
  output [15:0] io_out
);
  wire [15:0] _T_1 = io_ins_0 > io_ins_1 ? io_ins_0 : io_ins_1; // @[MaxN.scala 13:43]
  wire [15:0] _T_3 = _T_1 > io_ins_2 ? _T_1 : io_ins_2; // @[MaxN.scala 13:43]
  wire [15:0] _T_5 = _T_3 > io_ins_3 ? _T_3 : io_ins_3; // @[MaxN.scala 13:43]
  wire [15:0] _T_7 = _T_5 > io_ins_4 ? _T_5 : io_ins_4; // @[MaxN.scala 13:43]
  wire [15:0] _T_9 = _T_7 > io_ins_5 ? _T_7 : io_ins_5; // @[MaxN.scala 13:43]
  wire [15:0] _T_11 = _T_9 > io_ins_6 ? _T_9 : io_ins_6; // @[MaxN.scala 13:43]
  assign io_out = _T_11 > io_ins_7 ? _T_11 : io_ins_7; // @[MaxN.scala 13:43]
endmodule
