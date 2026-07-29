// FIXME tracker -- a SELF-REFERENTIAL whole-vector assign is never solved
// (LEC-refuted). `assign p = { A[3]^p[2], A[2]^p[1], A[1]^p[0], A[0] };` is the
// classic prefix/ripple idiom and is perfectly legal: every bit depends only on
// STRICTLY LOWER bits, so the net settles in one pass. The reader never
// materializes it -- look at the Pyrope below, `p` is left at `0ub????` and the
// output expression reads that poison instead of the settled value.
// Distinct from selfref_slice_assign: there several separate `assign w[i] =`
// statements are SEQUENCED in source order; here a single whole-vector assign is
// simply never driven at all.
module selfref_concat_assign(A, O);
  input  [3:0] A;
  output [3:0] O;
  wire   [3:0] p;
  assign p = { (A[3] ^ p[2]), (A[2] ^ p[1]), (A[1] ^ p[0]), A[0] };
  assign O = p;
endmodule
