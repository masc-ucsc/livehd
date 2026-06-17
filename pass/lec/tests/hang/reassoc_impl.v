// HANG repro #1 (multiply re-association). Pair with reassoc_ref.v.
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a * (b * c);
endmodule
