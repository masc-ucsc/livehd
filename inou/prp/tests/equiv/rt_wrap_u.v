// Golden for runtime unsigned `wrap`: truncate to the low 4 bits.
module \rt_wrap_u.rt_wrap_u (
  input  [7:0] a,
  output [3:0] z
);
  assign z = a[3:0];
endmodule
