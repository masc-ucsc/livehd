/*
:lec_expect: refuted
MUTATED: the plain flop `s` moved from clks[0] to clks[1].
*/
module cbg_gate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cbg(input [1:0] clks, input en, input [3:0] d, output reg [3:0] q, output reg [3:0] s);
  logic gclk;
  cbg_gate u(.clk_i(clks[0]), .en_i(en), .clk_o(gclk));
  always @(posedge gclk) q <= d;
  always @(posedge clks[1]) s <= d;
endmodule
