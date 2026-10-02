/*
:lec_top: cm_tp
A MEMORY with two write ports: port 0 behind a recognized Clock_cell on clk_a,
port 1 on a PLAIN clk_b. The gate enable belongs to port 0's clock sink only;
port 1 used to inherit it, so adding `&& en` to the clk_b write (variant _1)
was PROVEN. _2 negates it (control), _3 respells the read and must PROVE,
_4 puts the clk_b port behind its OWN second clock gate (must REFUTE).
*/
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cm_tp(input clk_a, input clk_b, input en, input we0, input we1, input [1:0] wa0, input [1:0] wa1,
             input [3:0] wd0, input [3:0] wd1, input [1:0] ra, output [3:0] o, output z);
  logic gclk;
  clkgate u_cg(.clk_i(clk_a), .en_i(en), .clk_o(gclk));
  logic [3:0] mem [4];
  always @(posedge gclk) if (we1) mem[wa1] <= wd1;
  always @(posedge clk_b) if (we0) mem[wa0] <= wd0;
  assign o = mem[ra];
  reg zz; always @(posedge clk_b) zz <= we1;
  assign z = zz;
endmodule
