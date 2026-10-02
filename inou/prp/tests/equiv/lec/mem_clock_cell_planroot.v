/*
:lec_top: cm_pr
mem_clock_cell_twoport WITHOUT any clk_b flop: the memory's port 1 on the plain
clk_b is the ONLY thing clocked by clk_b. The formal phase plan used to read
only the memory's port-0 clock (Clock_cell on clk_a), so it saw one root and
took the memory over with port 0's guard: moving the clk_b write to clk_a (_1)
and gating it with `&& en` (_2) were both PROVEN. Every memory clock lane is a
root now, so the design is multi-root and each port commits on its own edge.
_3 respells the read and must PROVE.
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
  always @(posedge clk_b) if (we0) mem[wa0] <= wd0;
  assign o = mem[ra];
endmodule
