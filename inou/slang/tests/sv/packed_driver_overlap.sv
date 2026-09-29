// Overlapping concurrent packed drivers must NOT take the one-wire-per-bit
// representation (it is single-assignment by construction, so assembling the
// vector from it would silently invent last-write-wins semantics) and must NOT
// be refused either: an arrayed instantiation broadcasting one output onto one
// net bit is the same shape, and grid_hier_test/fixme_hier_test compile and LEC
// at the `lec` tier today. The overlap warns and keeps the legacy
// source-ordered lowering, which is what this fixture pins.
// :test: lec
module packed_driver_overlap(input [2:0] d, output wire [2:0] q);
  assign q[1:0] = d[1:0];
  for (genvar i = 1; i < 3; i = i + 1) begin : duplicate
    assign q[i] = d[i];
  end
endmodule
