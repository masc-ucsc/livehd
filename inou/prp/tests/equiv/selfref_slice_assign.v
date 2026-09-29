// Concurrent partial drivers read sibling slices. They settle as one acyclic
// bit-level equation system regardless of source order.
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
