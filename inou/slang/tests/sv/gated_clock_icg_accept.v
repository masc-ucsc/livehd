// Ruling 81 (qa Q26): Verilog clock idioms map what fits. A latch-based ICG
// (`clk & en_latch`, the enable latched while clk is low) and a plain
// `negedge clk` are clocks, not derived clocks: the compile accepts them.
module gated_clock_icg_accept(input clk, input en, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  reg en_l;
  always_latch if (!clk) en_l = en;
  wire gclk = clk & en_l;
  always @(posedge gclk) q <= d;
  always @(negedge clk) n <= d;
endmodule
