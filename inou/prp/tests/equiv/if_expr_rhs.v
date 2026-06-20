module \if_expr_rhs.ifsel (
  input c,
  input d,
  input [7:0] a,
  input [7:0] b,
  output [7:0] o,
  output [7:0] p,
  output [7:0] q
);
  assign o = c ? a : b;
  assign p = c ? b : a;
  assign q = c ? a : (d ? b : 8'd0);
endmodule
