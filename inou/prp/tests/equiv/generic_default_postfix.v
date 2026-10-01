// Golden for generic_default_postfix.prp: Z.[bits] = 6, cfg.w = 5.
module generic_default_postfix (
  input  [7:0] b,
  output [7:0] s,
  output [5:0] r,
  output [5:0] p,
  output [4:0] u,
  output [7:0] k
);
  assign s = b + 8'd6;
  assign r = b[5:0];
  assign p = b[5:0];
  assign u = b[4:0];
  assign k = b + 8'd2;
endmodule
