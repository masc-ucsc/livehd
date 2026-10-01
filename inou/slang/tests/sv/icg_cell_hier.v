// :test: sim
// Coverage twin of inou/prp/tests/sim/hier_gate_port.prp, gate_through_wrapper
// and verify/latch_icg_hier, whose Pyrope forms now gate with a
// `Clock(clock_pin=, enable=)` Clock_cell: a hand-built ICG CELL (the enable
// latch transparent while the clock is low, ANDed with the clock -- minion's
// prim_clk_gate) whose gated clock is read UP (the cell's output clocks the
// parent's flop) and DOWN through a stateless pass-through wrapper (minion's
// txfma chain: the leaf's state commits on a port the wrapper only forwards).
// Both must fold into a commit guard, never commit every tick with the gate as
// dead code. With en = c0, c1, c4 and d = 10 + cycle: up = down = 10, 11, 11,
// 11, 14, 14 and ctl = d every cycle.
module icg_cell(input clk_i, input en_i, output clk_o);
  reg en_latch;
  always_latch if (!clk_i) en_latch = en_i;
  assign clk_o = clk_i & en_latch;
endmodule

module gated_flop(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule

module flop_wrap(input wclk, input [7:0] d, output [7:0] q);
  gated_flop u_leaf(.clk(wclk), .d(d), .q(q));
endmodule

module icg_cell_hier(input clk, input en, input [7:0] d, output reg [7:0] up, output [7:0] down,
                     output reg [7:0] ctl);
  wire gclk;
  icg_cell u_cell(.clk_i(clk), .en_i(en), .clk_o(gclk));
  always @(posedge gclk) up <= d;
  flop_wrap u_down(.wclk(gclk), .d(d), .q(down));
  always @(posedge clk) ctl <= d;  // CONTROL: the ungated reference clock
endmodule
