// A hi/lo latch pair with phase enables (the shape of a two-phase latch datapath): `hi` is open while
// the clock is high, `lo` while it is low, and the pair is a ring through `d + lo`.
module latch_pair(input clk, input en, input [7:0] d, output [7:0] q_hi, output [7:0] q_lo, output [7:0] acc);
  logic       hi_en, lo_en;
  logic [7:0] hi, lo, a;
  always_latch if (!clk) hi_en = en;
  always_latch if (clk) lo_en = en;
  always_latch if (clk & hi_en) hi = d + lo;
  always_latch if (!clk & lo_en) lo = hi;
  always_ff @(posedge clk) a <= a + lo;
  assign q_hi = hi;
  assign q_lo = lo;
  assign acc  = a;
endmodule
