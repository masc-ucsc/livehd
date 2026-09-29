module \signed_divrem_mixed.foo (
  input signed [4:0] a,
  input [2:0] b,
  input [4:0] c,
  input signed [2:0] d,
  output signed [5:0] q,
  output signed [4:0] r,
  output signed [6:0] qr,
  output signed [5:0] rr
);
assign q = a / $signed({1'b0, b});
assign r = a % $signed({1'b0, b});
assign qr = $signed({1'b0, c}) / d;
assign rr = $signed({1'b0, c}) % d;
endmodule
