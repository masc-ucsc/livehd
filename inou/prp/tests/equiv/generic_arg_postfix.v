// Golden for generic_arg_postfix.prp: a.[bits] = 6, a.[max] = 63,
// b.[bits] = 8, cfg.w = 5.
module generic_arg_postfix (
  input  [5:0] a,
  input  [7:0] b,
  output [5:0] r,
  output [7:0] s,
  output [7:0] m,
  output [7:0] t,
  output [4:0] u
);
  assign r = b[5:0];
  assign s = b + 8'd6;
  assign m = b + 8'd63;
  assign t = b + 8'd8;
  assign u = b[4:0];
endmodule
