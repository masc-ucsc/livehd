// Golden for runtime signed `wrap`: low 4 bits read as a two's-complement s4.
module \rt_wrap_s.rt_wrap_s (
  input  signed [7:0] a,
  output signed [3:0] z
);
  assign z = a[3:0];
endmodule
