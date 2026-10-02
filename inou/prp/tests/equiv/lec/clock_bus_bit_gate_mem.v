/*
:lec_top: cbm
A recognized clock gate on clks[0] plus a memory written on clks[0]. The gate
makes the design need the phase plan (one root, `multi` false), and the plan
-- which saw clks[0] and clks[1] as one root `clks` -- owned the memory and
committed its write port every step. Moving the write to clks[1] (variant _1)
was PROVEN.
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
  always @(posedge clks[0]) if (we) mem[wa] <= d;
  assign o = mem[ra];
endmodule
