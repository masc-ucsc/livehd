// :test: error
// :error: clock input .clk. of instance .u1. is clocked by a register output \(a divided clock\)
// Ruling 81 (qa Q26): a derived clock handed to an instance's clock port (a
// port that clocks state in the child) is as unsupported as one on a local
// register. (A plain inversion there is accepted: a gate-level negedge flop.)
module dci_leaf(input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= d;
endmodule

module derived_clock_instance_reject(input clk, input [3:0] d, output [3:0] q);
  reg div;
  always @(posedge clk) div <= ~div;
  dci_leaf u1(.clk(div), .d(d), .q(q));
endmodule
