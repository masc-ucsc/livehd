// HANG repro #2 (multiply distributes over add). Pair with distrib_impl.v.
//   lhd lec --ref distrib_ref.v --impl distrib_impl.v --top foo
// Equivalent (a*(b+c) == a*b + a*c) but cvc5 never returns at 16 bits.
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a * (b + c);
endmodule
