// Hierarchical design with a repeated submodule, for the pass.color smoke test.
// `leaf` is instantiated twice; flat (per-def) coloring gives both instances the
// same ids, which is exactly the "same module => same color" invariant.
module leaf(input [7:0] x, input [7:0] w, output [7:0] r);
  assign r = (x + w) ^ x;
endmodule

module part_hier(input [7:0] a, input [7:0] b, output [7:0] o);
  wire [7:0] m1, m2;
  leaf u1(.x(a),  .w(b), .r(m1));
  leaf u2(.x(m1), .w(b), .r(m2));
  assign o = m1 & m2;
endmodule
