// Same two-stage ICG chain as phase_clock_gate_chain.prp. Each latch samples
// its enable only while its local reference clock is low.
module phase_clock_gate_leaf (
  input  wire clk,
  input  wire en,
  output wire gclk
);
  reg held_en;
  always_latch
    if (!clk) held_en <= en;
  assign gclk = clk & held_en;
endmodule

module phase_clock_gate_chain (
  input  wire       clk,
  input  wire       en0,
  input  wire       en1,
  input  wire [2:0] d,
  output wire [2:0] q
);
  wire g0;
  wire g1;
  reg [2:0] state;
  phase_clock_gate_leaf u_gate0(.clk(clk), .en(en0), .gclk(g0));
  phase_clock_gate_leaf u_gate1(.clk(g0),  .en(en1), .gclk(g1));
  always @(posedge g1)
    state <= d;
  assign q = state;
endmodule
