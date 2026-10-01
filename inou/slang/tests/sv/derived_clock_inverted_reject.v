// :test: error
// :error: register .q. is clocked by an inverted clock: derived, inverted and muxed clocks are not supported
// Ruling 81 (qa Q26): an inverted clock (`posedge ~clk`, a negedge written as
// an inversion) is a derived clock: write `negedge clk`. (It replaces the LEC
// pairs equiv/lec/clock_inverted_1 -- `posedge ~clk` PROVEN equal to
// `negedge clk` -- and clock_edge_2, which now stop at this error.)
module derived_clock_inverted_reject(input clk, input [3:0] d, output reg [3:0] q);
  wire nclk = ~clk;
  always @(posedge nclk) q <= d;
endmodule
