/*
:lec_expect: unknown
:lec_grep: scheduled on clock `clks` but the impl side on `clks\[1\]`
MUTATED: every clock moved from clks[0] to clks[1]. Each side is single-root,
but on a DIFFERENT bit, so one microstep sequence for both is two time bases
-- a sound UNKNOWN, never PROVEN.
*/
module cbp_cell(input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module cbp(input wire [1:0] clks, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  reg [2:0] early, late;
  cbp_cell u_gate(.clk(clks[1]), .en(early[0]), .gclk_n(gn));
  always @(posedge clks[1]) early <= d;
  always @(negedge gn) late <= d ^ 3'b111;
  assign q = late ^ early;
endmodule
