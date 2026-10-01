// qa.md Appendix §8 / docs 06b-instantiation.md: `__mux(s=c, p1=b, p2=a)` is
// `a` when `c` is true, `b` otherwise.
module spec_calls_gate_runtime(input c, input [7:0] a, input [7:0] b, output [7:0] o);
  assign o = c ? a : b;
endmodule
