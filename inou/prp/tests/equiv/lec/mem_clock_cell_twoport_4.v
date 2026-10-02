/*
:lec_expect: refuted
MUTATED: the plain clk_b write port goes through its OWN second clock gate
(clkgate on clk_b). Two distinct non-identity sink gates on two clocks: with one
enable per memory this side's clk_b port took port 0's gate and was PROVEN.
*/
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cm_tp(input clk_a, input clk_b, input en, input we0, input we1, input [1:0] wa0, input [1:0] wa1,
             input [3:0] wd0, input [3:0] wd1, input [1:0] ra, output [3:0] o, output z);
  logic gclk, gclk2;
  clkgate u_cg(.clk_i(clk_a), .en_i(en), .clk_o(gclk));
  clkgate u_cg2(.clk_i(clk_b), .en_i(en), .clk_o(gclk2));
  logic [3:0] mem [4];
  always @(posedge gclk) if (we1) mem[wa1] <= wd1;
  always @(posedge gclk2) if (we0) mem[wa0] <= wd0;
  assign o = mem[ra];
  reg zz; always @(posedge clk_b) zz <= we1;
  assign z = zz;
endmodule
