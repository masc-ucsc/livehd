module \reduction_integer_arithmetic.foo (
  input signed [31:0] a,
  input [2:0] b,
  output signed [33:0] r_or,
  output signed [33:0] r_and,
  output signed [33:0] r_xor
);
  assign r_or = {{2{a[31]}}, a} + {33'b0, |b[2:1]};
  assign r_and = {{2{a[31]}}, a} + {33'b0, &b[2:1]};
  assign r_xor = {{2{a[31]}}, a} + {33'b0, ^b[2:1]};
endmodule
