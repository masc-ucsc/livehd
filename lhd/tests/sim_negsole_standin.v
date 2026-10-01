// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Verilog stand-in for inou/prp/tests/sim/flop_sim_negedge_sole_clock.prp while
// the Pyrope `Clock` type rejects that fixture's clock-as-data (clock lane,
// 2026-09-30; the Pyrope twin is the fixme target). Same shape: the ONLY clock
// is an explicit `rclk` net (no register on the reference clock), every flop
// commits on its FALL, and (c) is enabled through a latch transparent while
// `pclk` is low. Driven by sim_negsole_standin_tb.prp.
module negsole(input pclk, input rclk, input en, input [7:0] d,
               output [7:0] qa, output [7:0] qb, output [7:0] qc);
  reg en_q;
  always_latch if (!pclk) en_q = en;
  reg [7:0] ma = 8'd0, mb = 8'd0, mc = 8'd0;
  always @(negedge rclk) begin
    ma <= d;          // (a) unconditional
    if (en) mb <= d;  // (b) enable from an input
    if (en_q) mc <= d;  // (c) enable through the latch
  end
  assign qa = ma;
  assign qb = mb;
  assign qc = mc;
endmodule
