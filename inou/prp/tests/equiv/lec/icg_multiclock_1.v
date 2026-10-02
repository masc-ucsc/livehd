/*
:lec_expect: refuted
MUTATED: the gate hangs off clk_b instead of clk_a.
*/
module icg_mc(input clk_a, input clk_b, input en, input [3:0] d, output reg [3:0] p, output reg [3:0] q, output reg [3:0] z);
  wire gclk = clk_b & en;
  always @(posedge clk_a) p <= d;
  always @(posedge gclk) q <= d + 4'd1;
  always @(posedge clk_b) z <= d;
endmodule
