// :test: error
// :error: clock input .clk. of .ccir_sub. is bound to a constant.*register .q. of .ccir_sub.
// User ruling 2026-09-28 (27): a register whose clock resolves to a constant is
// a compile error in every front end. `u1` ties the clock of ccir_sub's
// `always @(posedge clk)` flop to 1'b0, so `q1` could never update (the
// emitted Verilog and LEC keep the tie-off, while `lhd sim` steps every clock
// port). Before the fix the Verilog reader was exempt and it compiled silently.
module ccir_sub(input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= d;
endmodule

module const_clock_instance_reject(input clk, input [3:0] d, output [3:0] q1, output [3:0] q2);
  ccir_sub u1(.clk(1'b0), .d(d), .q(q1));
  ccir_sub u2(.clk(clk), .d(d), .q(q2));
endmodule
