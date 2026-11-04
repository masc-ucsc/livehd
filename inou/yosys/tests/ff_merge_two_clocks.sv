// Two clocked processes on DIFFERENT clocks write one variable: no single flop keeps both, so the yosys
// readers must refuse it (lhd_ff_merge) instead of keeping one writer.
module ff_merge_two_clocks(input logic clk1, input logic clk2, input logic a, input logic b, output logic [3:0] q);
  logic [3:0] x;
  always_ff @(posedge clk1) if (a) x <= 4'd5;
  always_ff @(posedge clk2) if (b) x <= 4'd9;
  assign q = x;
endmodule
