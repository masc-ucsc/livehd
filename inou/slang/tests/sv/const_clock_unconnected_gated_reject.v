// :test: error
// :error: clock input .clk. of .cug_sub. is unconnected.*register .q. of .cug_sub.
// Rulings 41/80: an unconnected clock never ticks, gated or not: `.clk()`
// reaching a register through a latch ICG is the same error as a tied-off one.
module cug_sub(input clk, input en, input [3:0] d, output reg [3:0] q);
  reg en_l;
  always_latch if (!clk) en_l = en;
  wire g = clk & en_l;
  always @(posedge g) q <= d;
endmodule

module const_clock_unconnected_gated_reject(input en, input [3:0] d, output [3:0] q);
  cug_sub u1(.clk(), .en(en), .d(d), .q(q));
endmodule
