// HANG repro #1 (multiply re-association). Pair with reassoc_impl.v.
//   lhd lec --ref reassoc_ref.v --impl reassoc_impl.v --top foo
// Equivalent to reassoc_impl.v, but cvc5 (bit-blast) never finishes: the miter
// is a 16-bit (a*b)*c == a*(b*c) associativity check, a known-hard nonlinear
// instance. 8-bit proves in ~2s; 16-bit does not return.
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = (a * b) * c;
endmodule
