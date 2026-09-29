module \signed_inline_arithmetic.foo (
  input signed [7:0] a,
  input [7:0] b, c,
  output signed [17:0] product, total
);
  wire signed [17:0] sa = a;
  wire signed [17:0] ub = {1'b0, b};
  wire signed [17:0] uc = {1'b0, c};
  assign product = sa + ub * uc;
  assign total = sa + (ub + uc) * 18'sd3;
endmodule
