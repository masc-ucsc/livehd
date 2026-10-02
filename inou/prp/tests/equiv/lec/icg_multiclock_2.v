/*
Equivalent rewrite (positive control): operands of the gate swapped and the
gated flop's data path double-inverted. The operand swap alone canonicalizes
back to the same graph, so semdiff matched it structurally and the encoder's
multi-clock And-ICG gating never ran; the `~(~...)` forces the solver.
:lec_grep_not: structurally identical
*/
module icg_mc(input clk_a, input clk_b, input en, input [3:0] d, output reg [3:0] p, output reg [3:0] q, output reg [3:0] z);
  wire gclk = en & clk_a;
  always @(posedge clk_a) p <= d;
  always @(posedge gclk) q <= ~(~(d + 4'd1));
  always @(posedge clk_b) z <= d;
endmodule
