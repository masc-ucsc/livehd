// Golden for a no-op `wrap` (value already fits): plain zero-extend alias.
module \rt_wrap_noop.rt_wrap_noop (
  input  [3:0] a,
  output [7:0] z
);
  assign z = a;
endmodule
