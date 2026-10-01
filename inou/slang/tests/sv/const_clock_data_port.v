// User ruling 2026-09-28 (27): an input named `clk` is just data unless a
// register clocks on it. ccdp_sub reads `clk` as a data bit only, so tying it
// to a constant is legal (the lec tier: compile + LEC against this source).
module ccdp_sub(input clk, input [3:0] d, output [3:0] y);
  assign y = d ^ {4{clk}};
endmodule

module const_clock_data_port(input clk, input [3:0] d, output [3:0] y0, output [3:0] y1, output reg [3:0] q);
  ccdp_sub u0(.clk(1'b0), .d(d), .y(y0));
  ccdp_sub u1(.clk(1'b1), .d(d), .y(y1));
  always @(posedge clk) q <= d;
endmodule
