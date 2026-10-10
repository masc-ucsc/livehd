// A latch plus an unrelated flop: the latch's commit in the clock-high phase
// must wake its other phase colors, or the output (published after the fall)
// keeps the first value it ever loaded under the default activation cache.
module latch_phase_dirty(input clock, input reset, input e, input [7:0] d, output [7:0] o0);
  reg [8:0] t0;
  always @(posedge clock) if (reset) t0 <= 38; else t0 <= 4;
  reg [7:0] l1;
  always @(*) if (clock && e) l1 = d;
  assign o0 = l1;
endmodule
