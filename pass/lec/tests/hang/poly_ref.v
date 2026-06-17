// HANG repro #3 (3-way multiply commutativity). Pair with poly_impl.v.
//   lhd lec --ref poly_ref.v --impl poly_impl.v --top foo
// Equivalent (a*b*c == c*a*b) but cvc5 never returns at 16 bits. Note a SINGLE
// 64-bit multiply commute (a*b == b*a) proves instantly -- it is the chain of
// two multiplies that explodes the bit-blast.
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a * b * c;
endmodule
