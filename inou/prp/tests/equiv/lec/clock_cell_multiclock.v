/*
:lec_top: cc_mc
An instantiated latch clock gate (recognized as a Clock_cell) on clk_a in a
design that also has flops on clk_a and clk_b. A Clock_cell flop used to
commit on its enable alone in multi-clock mode, and a Clock_cell clock was not
counted by the clock census, so moving the gate to clk_b (variant _1) was
PROVEN. Variant _2 changes only the data path spelling and must PROVE.
*/
module clkgate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cc_mc(input clk_a, input clk_b, input en, input [3:0] d, output [3:0] o, output reg [3:0] p, output reg [3:0] z);
  logic gclk;
  clkgate u_cg(.clk_i(clk_a), .en_i(en), .clk_o(gclk));
  logic [3:0] f;
  always @(posedge gclk) f <= d;
  always @(posedge clk_a) p <= d ^ 4'h5;
  always @(posedge clk_b) z <= d;
  assign o = f;
endmodule
