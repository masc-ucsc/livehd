module \const_nil_cond_bind.cnb (input [7:0] a, input c, output [7:0] o);
  // The conditional bind is the const's ONE assignment, so it holds on every
  // path: `c` does not reach `o` at all.
  assign o = a + 1;
endmodule
