/*
:lec_top: cpr
Two SCALAR clock inputs, `clk` and `clkx`, on both sides, in a design that
needs the formal phase schedule (an active-low clock gate pass.single_edge
declines). A single-root plan owns its flops and commits them on the
schedule without reading the clock, so ref on `clk` vs impl on `clkx` (_1:
every clock moved to the other input) was PROVEN: two different clocks
scheduled as one. Both roots are inputs of both designs, so they are two
clocks (the force_multi_clock identity rule) and the run refuses by name.
_2 renames the only clock `clk` -> `clock` (no `clk` on that side): still one
clock, still PROVEN.
*/
module cpr_cell(input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module cpr(input wire clk, input wire clkx, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  reg [2:0] early, late;
  cpr_cell u_gate(.clk(clk), .en(early[0]), .gclk_n(gn));
  always @(posedge clk) early <= d;
  always @(negedge gn) late <= d ^ 3'b111;
  assign q = late ^ early;
endmodule
