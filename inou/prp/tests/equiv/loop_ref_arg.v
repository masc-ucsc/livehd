// Golden for loop_ref_arg: `s` counts the set bits of `a`, `t` is 3 + b.
module loop_ref_arg (
  input  [3:0] a,
  input  [3:0] b,
  output [7:0] s,
  output [7:0] t
);
  assign s = a[0] + a[1] + a[2] + a[3];
  assign t = 8'd3 + b;
endmodule
