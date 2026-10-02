/*
Equivalent rewrite of the data path only.
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
  assign o = ~(~f);
endmodule
