// :test: error
// :error: register .q. is clocked by a clock mux: derived, inverted and muxed clocks are not supported
// Ruling 81 (qa Q26): a clock mux is a derived clock: not supported.
module derived_clock_mux_reject(input clk_a, input clk_b, input sel, input [3:0] d, output reg [3:0] q);
  wire mclk = sel ? clk_b : clk_a;
  always @(posedge mclk) q <= d;
endmodule
