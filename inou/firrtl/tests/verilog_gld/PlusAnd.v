module PlusAnd(
  input         clock,
  input         reset,
  input  [22:0] io_vecI_0,
  input  [22:0] io_vecI_1,
  input  [22:0] io_vecI_2,
  input  [22:0] io_vecI_3,
  input  [22:0] io_vecI_4,
  output [22:0] io_bund_vecO_0,
  output [22:0] io_bund_vecO_1,
  output [22:0] io_bund_vecO_2,
  output [22:0] io_bund_vecO_3,
  output [22:0] io_bund_vecO_4
);
  assign io_bund_vecO_0 = io_vecI_0; // @[PlusAnd.scala 15:16]
  assign io_bund_vecO_1 = io_vecI_1; // @[PlusAnd.scala 15:16]
  assign io_bund_vecO_2 = io_vecI_2; // @[PlusAnd.scala 15:16]
  assign io_bund_vecO_3 = io_vecI_3; // @[PlusAnd.scala 15:16]
  assign io_bund_vecO_4 = io_vecI_4; // @[PlusAnd.scala 15:16]
endmodule
