module Test2(
  input        clock,
  input        reset,
  input  [4:0] io_in_val,
  output [3:0] io_out_head,
  output [2:0] io_out_extractS,
  output [2:0] io_out_extractI,
  output [2:0] io_out_extractE
);
  assign io_out_head = io_in_val[4:1]; // @[Test2.scala 38:15]
  assign io_out_extractS = io_in_val[3:1]; // @[Test2.scala 41:19]
  assign io_out_extractI = io_in_val[3:1]; // @[Test2.scala 45:19]
  assign io_out_extractE = io_in_val[3:1]; // @[Test2.scala 49:19]
endmodule
