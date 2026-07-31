// Structurally different hierarchy with the same behavior as the Pyrope pair.
module phase_hier_latch_neg_leaf (
  input  wire       clk,
  input  wire [2:0] d,
  output wire [2:0] q
);
  reg [2:0] staged;
  reg [2:0] captured;

  always_latch
    if (clk) staged <= d ^ 3'b101;

  always @(negedge clk)
    captured <= staged;

  assign q = captured;
endmodule

module phase_hier_latch_neg (
  input  wire       clk,
  input  wire [2:0] d,
  input  wire [2:0] mask,
  output wire [2:0] q
);
  wire [2:0] child_q;
  phase_hier_latch_neg_leaf u_phase(.clk(clk), .d(d), .q(child_q));
  assign q = child_q ^ mask;
endmodule
