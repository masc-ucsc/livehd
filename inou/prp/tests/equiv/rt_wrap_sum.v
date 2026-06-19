// Golden for runtime `wrap` of a computed sum: low 4 bits of (a + b).
module \rt_wrap_sum.rt_wrap_sum (
  input  [3:0] a,
  input  [3:0] b,
  output [3:0] z
);
  assign z = a + b;
endmodule
