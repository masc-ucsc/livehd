// :test: error
// :error: register .q. is clocked by an .and. of data \(no clock operand\)
// Ruling 81 (qa Q26): an AND is an ICG only when one operand is a clock (an
// input that clocks state, by use or by the clk/clock convention; a gate cell
// or Clock_cell output). `a & b` over two data inputs is a derived clock.
module derived_clock_and_data_reject(input a, input b, input [3:0] d, output reg [3:0] q);
  wire g = a & b;
  always @(posedge g) q <= d;
endmodule
