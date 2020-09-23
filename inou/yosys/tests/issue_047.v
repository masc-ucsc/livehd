module issue_047(a, b, y);
  input signed [0:0] a;
  input signed [1:0] b;
  output [1:0] y;

  // For a='b1 and b='b11 this should output y='b00 (because a='b1 is
  // sign-extended to a='b11). But Yosys 0.7+205 (git sha1 7d2fb6e)
  // after optimizations outputs y='b10 instead.

  assign y = b ^ (a|a);
endmodule
