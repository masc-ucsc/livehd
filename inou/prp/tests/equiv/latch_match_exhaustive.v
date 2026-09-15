module latch_match_exhaustive(input sel, input [3:0] a, b, output [3:0] q);
  assign q = sel ? b : a;
endmodule
