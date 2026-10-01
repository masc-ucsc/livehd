// :test: lec
// Ruling 81 (qa Q26): the Verilog clock idioms LiveHD maps compile -- the
// active-low ICG (`clk | ~en_latch`, the enable latched while clk is high,
// gating the falling edge; minion's prim_clk_gate_n) and a gate-level negedge
// flop, an inverter into a posedge cell's clock port.
module gcn_dff(input CLK, input D, output reg Q);
  always @(posedge CLK) Q <= D;
endmodule

module gated_clock_icg_n_accept(input clk, input en, input [3:0] d, input b, output reg [3:0] q, output n);
  reg en_l;
  always_latch if (clk) en_l = en;
  wire g = clk | ~en_l;
  always @(negedge g) q <= d;
  gcn_dff u_n(.CLK(~clk), .D(b), .Q(n));
endmodule
