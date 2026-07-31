// Same behavior as phase_latch_flop_edges.prp with the two latch windows made
// explicit. The closing latch event precedes the coincident flop sampling.
module phase_latch_flop_edges (
  input  wire       clk,
  input  wire [2:0] d_lo,
  input  wire [2:0] d_hi,
  output wire [2:0] rise_o,
  output wire [2:0] fall_o
);
  reg [2:0] low_latch;
  reg [2:0] high_latch;
  reg [2:0] rise;
  reg [2:0] fall;

  always_latch
    if (!clk) low_latch <= d_lo;

  always_latch
    if (clk) high_latch <= d_hi;

  always @(posedge clk)
    rise <= low_latch;

  always @(negedge clk)
    fall <= high_latch;

  assign rise_o = rise;
  assign fall_o = fall;
endmodule
