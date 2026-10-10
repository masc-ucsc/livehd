// Latch-only design (no flop clock): a clock-low latch read after the fall
// shows its transparent value whatever the enable spelling. The always_latch
// if/else-if form (a pure clock-window latch) used to publish its rise value.
module latch_only_readthrough(input clock, input reset, input [4:0] i0, output [2:0] o2, output [4:0] o3, output [4:0] o4);
  reg [2:0] l0;
  always @(*) if (clock && (reset || (|i0))) l0 = reset ? 0 : i0[1:0];
  reg [4:0] l1;
  always @(*) if (!clock && (reset || 1'b1)) l1 = reset ? 0 : l0;
  reg [4:0] l2;
  always_latch begin
    if (!clock && reset) l2 = 0;
    else if (!clock && (5)) l2 = l0 + 1;
  end
  assign o2 = l0;
  assign o3 = l1;
  assign o4 = l2;
endmodule
