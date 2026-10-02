/*
Equivalent: the clock input renamed `clk` -> `clock`. `clk` does not exist on
this side, so it is one clock spelled two ways (lec clock identity rule).
*/
module cpr_cell(input wire clk, input wire en, output wire gclk_n);
  reg held_en;
  always_latch if (clk) held_en <= en;
  assign gclk_n = clk | ~held_en;
endmodule
module cpr(input wire clock, input wire clkx, input wire [2:0] d, output wire [2:0] q);
  wire gn;
  reg [2:0] early, late;
  cpr_cell u_gate(.clk(clock), .en(early[0]), .gclk_n(gn));
  always @(posedge clock) early <= d;
  always @(negedge gn) late <= d ^ 3'b111;
  assign q = late ^ early;
endmodule
