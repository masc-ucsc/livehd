/*
:lec_expect: unknown
:lec_grep: scheduled on clock `clk` but the impl side on `clkx`
MUTATED: every clock moved from `clk` to `clkx`. Must never be PROVEN.
*/
module cpr_cell(input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module cpr(input wire clk, input wire clkx, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  reg [2:0] early, late;
  cpr_cell u_gate(.clk(clkx), .en(early[0]), .gclk_n(gn));
  always @(posedge clkx) early <= d;
  always @(negedge gn) late <= d ^ 3'b111;
  assign q = late ^ early;
endmodule
