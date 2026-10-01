// :test: error
// :error: register .q. is clocked by an .or. of a clock: derived, inverted and muxed clocks are not supported
// Ruling 81 (qa Q26): data OR-ed into a clock is a derived clock. The only OR
// on a clock LiveHD maps is the active-low ICG `clk | ~en_latch`.
module derived_clock_or_reject(input clk, input sel, input [3:0] d, output reg [3:0] q);
  wire g = sel | clk;
  always @(posedge g) q <= d;
endmodule
