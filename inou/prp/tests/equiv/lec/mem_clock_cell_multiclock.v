/*
:lec_top: cm_mc
A MEMORY written through a recognized Clock_cell on clk_a, plus a flop on
clk_b. The memory must commit on a rising clk_a with the enable held, not on
the enable alone: moving the gate to clk_b (variant _1) was PROVEN. Variant _2
respells the read and must PROVE.
*/
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cm_mc(input clk_a, input clk_b, input en, input we, input [1:0] wa, input [3:0] wd, input [1:0] ra,
             output [3:0] o, output reg [3:0] z);
  logic gclk;
  clkgate u_cg(.clk_i(clk_a), .en_i(en), .clk_o(gclk));
  logic [3:0] mem [4];
  always @(posedge gclk) if (we) mem[wa] <= wd;
  always @(posedge clk_b) z <= wd;
  assign o = mem[ra];
endmodule
