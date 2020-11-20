module Test3(
  input         clock,
  input         reset,
  input  [11:0] io_inp,
  output [15:0] io_out_pad
);
  assign io_out_pad = {{4'd0}, io_inp}; // @[Test3.scala 18:14]
endmodule
