// A packed word-level false loop whose feedback threads through a pure-comb
// instance. `w` is three disjoint lanes; the top lane is a function of a slice
// of `w` itself, so the design is acyclic per bit and only looks cyclic when
// read a whole word at a time.
module selfref_thru_comb_sub (
  input  [3:0]  a,
  input  [3:0]  b,
  output [11:0] z
);
  wire [11:0] w;
  wire [3:0]  hi;
  assign hi = (w[5:2] ^ b) ^ 4'd9;
  assign w  = {hi, b, a};
  assign z  = w;
endmodule
