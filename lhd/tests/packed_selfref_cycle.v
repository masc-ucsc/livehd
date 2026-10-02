// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// A packed net assembled by OR-ing disjoint ranges and read back through one of
// its own slices: the word-level-false combinational cycle that
// graph/split_selfref.cpp exists to dissolve.
//
// It is here to prove the DIAGNOSTIC works, not the dissolution: with
// LIVEHD_SIM_SPLIT_DEBUG=1 the pass must say what it saw, and it must say it
// somewhere a caller can read.
module packed_selfref_cycle(input wire clk, input wire [7:0] a, input wire [7:0] b, output wire [15:0] q);
  wire [15:0] w;
  assign w = {w[15:8], 8'h00} | {8'h00, a} | {b, 8'h00};
  assign q = w;
endmodule
