/*
:lec_expect: refuted
MUTATED: the ungated clk_a write port is qualified by the NEGATED gate enable.
*/
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cm_or(input clk_a, input en, input we0, input we1, input [1:0] wa0, input [1:0] wa1,
             input [3:0] wd0, input [3:0] wd1, input [1:0] ra, output [3:0] o, output z);
  logic gclk;
  clkgate u_cg(.clk_i(clk_a), .en_i(en), .clk_o(gclk));
  logic [3:0] mem [4];
  always @(posedge gclk) if (we1) mem[wa1] <= wd1;
  always @(posedge clk_a) if (we0 && !en) mem[wa0] <= wd0;
  assign o = mem[ra];
  reg zz; always @(posedge clk_a) zz <= we1;
  assign z = zz;
endmodule
