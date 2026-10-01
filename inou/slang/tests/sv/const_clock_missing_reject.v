// :test: error
// :error: clock input .clk. of .vmc_sub. is unconnected.*register .q. of .vmc_sub.
// Rulings 41/80: an omitted (never connected) clock port that reaches a
// register is an error, like `.clk()`.
module vmc_sub(input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= d;
endmodule

module const_clock_missing_reject(input clk, input [3:0] d, output [3:0] q1);
  vmc_sub u1(.d(d), .q(q1));
endmodule
