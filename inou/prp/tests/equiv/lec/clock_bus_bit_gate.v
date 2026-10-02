/*
:lec_top: cbg
A recognized clock GATE on clks[0] beside a plain flop on clks[0]. The gate
alone makes the design need the phase plan (one root, one sampled guard) while
`multi` stays false, so the plan -- which keyed a clock by its INPUT net and
saw clks[0] and clks[1] as one root `clks` -- owned every flop and committed
them all every step. Moving the plain flop to clks[1] (variant _1) was PROVEN.
_2 respells the select through a wire and must PROVE.
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
  always @(posedge clks[0]) s <= d;
endmodule
