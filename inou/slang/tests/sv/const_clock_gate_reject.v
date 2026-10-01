// :test: error
// :error: register .q. of .const_clock_gate_reject. is clocked by a constant
// User ruling 2026-09-28 (27): a register whose clock resolves to a constant is
// a compile error, also when the constant holds a clock GATE: `clk & EN` with
// `EN = 0` never takes an edge. Before the fix only a clock that was already a
// constant when lowered was caught; this gate folded later and was emitted as
// `always @(posedge ('sb0))`.
module const_clock_gate_reject(input clk, input [3:0] d, output reg [3:0] q);
  localparam EN = 1'b0;
  wire g = clk & EN;
  always @(posedge g) q <= d;
endmodule
