/*
:lec_expect: refuted
MUTATED: the plain clk_b write port is also qualified by the gate enable.
*/
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cm_pr(input clk_a, input clk_b, input en, input we0, input we1, input [1:0] wa0, input [1:0] wa1,
             input [3:0] wd0, input [3:0] wd1, input [1:0] ra, output [3:0] o);
  logic gclk;
  clkgate u_cg(.clk_i(clk_a), .en_i(en), .clk_o(gclk));
  logic [3:0] mem [4];
  always @(posedge gclk) if (we1) mem[wa1] <= wd1;
  always @(posedge clk_b) if (we0 && en) mem[wa0] <= wd0;
  assign o = mem[ra];
endmodule
