/*
:lec_expect: refuted
MUTATED: the memory write moved from clks[0] to clks[1].
*/
module cbm_gate(input clk_i, input en_i, output clk_o);
  logic en_latch;
  always_latch if (!clk_i) en_latch <= en_i;
  assign clk_o = clk_i & en_latch;
endmodule
module cbm(input [1:0] clks, input en, input we, input [1:0] wa, input [1:0] ra, input [3:0] d, output reg [3:0] q, output [3:0] o);
  logic gclk;
  cbm_gate u(.clk_i(clks[0]), .en_i(en), .clk_o(gclk));
  always @(posedge gclk) q <= d;
  logic [3:0] mem [4];
  always @(posedge clks[1]) if (we) mem[wa] <= d;
  assign o = mem[ra];
endmodule
