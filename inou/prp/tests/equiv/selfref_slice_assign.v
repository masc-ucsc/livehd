// FIXME tracker -- concurrent slice assignments to one wire are ORDERED (LEC-
// refuted). The three `assign` statements below drive disjoint bit-fields of `w`
// and read each other; being CONCURRENT, they settle to w[3:2]=hi, w[1]=w[2]=hi[0],
// w[0]=~w[1]. The reader instead threads them through SSA in source order, so
// `w__w1#[0] = ~((w >> 1) & 1)` reads the ORIGINAL (poison) `w`, not the settled
// w[1]. Reordering the same three assigns in the source changes the result, which
// is the signature of the bug: continuous assigns must be solved, not sequenced.
module selfref_slice_assign (
  input  [1:0] hi,
  output [3:0] q
);
  wire [3:0] w;
  assign w[0]   = ~w[1];
  assign w[1]   =  w[2];
  assign w[3:2] =  hi;
  assign q = w;
endmodule
