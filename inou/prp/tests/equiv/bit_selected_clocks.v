// Simple scalar-state reference, using named scalar (ICG-gated) clocks.
module bit_selected_clocks (
  input clk, gate0, gate1, gate2,
  input gate, clr_,
  input [2:0] data, enable,
  output [2:0] q
);
  // Each bit clock is a real ICG (enable latch transparent while clk is low, then
  // an AND), the form the Pyrope side spells `Clock(clock_pin=clk, enable=...)`.
  // A raw `clk & gate0 & gate` is NOT an ICG: its enable can glitch the clock.
  reg en0, en1, en2;
  always_latch if (!clk) en0 = gate0 & gate;
  always_latch if (!clk) en1 = gate1 & gate;
  always_latch if (!clk) en2 = gate2 & gate;
  wire c0 = clk & en0;
  wire c1 = clk & en1;
  wire c2 = clk & en2;
  reg q__bit0, q__bit1, q__bit2;
  always @(posedge c0 or negedge clr_)
    if (!clr_) q__bit0 <= 0;
    else if (enable[0]) q__bit0 <= data[0];
  always @(negedge c1 or negedge clr_)
    if (!clr_) q__bit1 <= 1;
    else if (enable[1]) q__bit1 <= data[1];
  always @(posedge c2 or negedge clr_)
    if (!clr_) q__bit2 <= 0;
    else if (enable[2]) q__bit2 <= data[2];
  assign q = {q__bit2, q__bit1, q__bit0};
endmodule
