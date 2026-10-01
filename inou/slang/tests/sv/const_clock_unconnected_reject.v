// :test: error
// :error: clock input .clk. of .vuc_sub. is unconnected.*register .q. of .vuc_sub.
// Rulings 41/80: an UNCONNECTED clock (`.clk()`, read as `0ub?`) that reaches
// a register is an error in every front end, like a constant clock (ruling
// 27), naming the register. The name has no meaning: the port is a clock
// because it clocks `q` (an unconnected data port named clk is plain X).
module vuc_sub(input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= d;
endmodule

module const_clock_unconnected_reject(input clk, input [3:0] d, output [3:0] q1, output [3:0] q2);
  vuc_sub u1(.clk(), .d(d), .q(q1));
  vuc_sub u2(.clk(clk), .d(d), .q(q2));
endmodule
