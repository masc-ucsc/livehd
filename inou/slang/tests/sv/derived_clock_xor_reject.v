// :test: error
// :error: register .q. is clocked by a .xor. of a clock: derived, inverted and muxed clocks are not supported
// Ruling 81 (qa Q26): the programmable-polarity idiom `clk ^ inv` (Xilinx
// RAM128X1D) is a derived clock: not supported.
module derived_clock_xor_reject(input clk, input inv, input [3:0] d, output reg [3:0] q);
  wire gclk = clk ^ inv;
  always @(posedge gclk) q <= d;
endmodule
