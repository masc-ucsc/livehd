// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Verilog stand-in for inou/prp/tests/sim/flop_feeds_transparent_high_latch.prp
// while the Pyrope `Clock` type rejects that fixture's latch gated by the clock
// level (clock lane, 2026-09-30; the Pyrope twin is the fixme target): a
// posedge flop feeding a latch transparent while `clk` is high. Driven by
// sim_flop_high_latch_standin_tb.prp.
module flop_high_latch(input clk, input [7:0] d, output [7:0] qf, output [7:0] qh);
  reg [7:0] f = 8'd0;
  reg [7:0] h = 8'd0;
  always @(posedge clk) f <= d;
  always_latch if (clk) h = f;
  assign qf = f;
  assign qh = h;
endmodule
