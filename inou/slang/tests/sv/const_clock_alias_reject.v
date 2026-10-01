// :test: error
// :error: clock input .clk. of .ccar_mid. is bound to a constant.*register .q. of .ccar_leaf.
// User ruling 2026-09-28 (27): the error follows the register's ACTUAL clock,
// not the port a constant lands on. ccar_leaf buffers its clock through a
// plain copy (`assign cb = clk`, the clock-buffer idiom), and ccar_mid forwards
// its own `clk` to it through another copy (`wire c2 = clk`), so the top's
// `.clk(1'b0)` still clocks `q` by a constant. Before the fix both copies hid
// the register and it compiled silently to `.clk(1'h0)`.
module ccar_leaf(input clk, input [3:0] d, output reg [3:0] q);
  wire cb;
  assign cb = clk;
  always @(posedge cb) q <= d;
endmodule

module ccar_mid(input clk, input [3:0] d, output [3:0] q);
  wire c2 = clk;
  ccar_leaf l(.clk(c2), .d(d), .q(q));
endmodule

module const_clock_alias_reject(input clk, input [3:0] d, output [3:0] q);
  ccar_mid m(.clk(1'b0), .d(d), .q(q));
endmodule
