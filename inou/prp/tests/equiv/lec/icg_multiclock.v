/*
:lec_top: icg_mc
An And-ICG (`clk_a & en`) flop in a MULTI-clock design whose gate reference
clk_a also clocks a plain flop. The fold "commit iff the enables hold" is only
right when one step is one reference period; with two clocks the gated flop
must also see a rising clk_a, or moving the gate to clk_b (variant _1) is
PROVEN. Variant _2 respells the gate and must PROVE.
*/
module icg_mc(input clk_a, input clk_b, input en, input [3:0] d, output reg [3:0] p, output reg [3:0] q, output reg [3:0] z);
  wire gclk = clk_a & en;
  always @(posedge clk_a) p <= d;
  always @(posedge gclk) q <= d + 4'd1;
  always @(posedge clk_b) z <= d;
endmodule
