// A bit-selected clock with a DYNAMIC destination must fail cleanly, never
// silently turn the selected bit into one flop.
// :test: error
// :error: require constant nonblocking destinations
module bit_clock_dynamic_reject(input [1:0] clocks, input d, index, output reg [1:0] q);
  always @(posedge clocks[0]) q[index] <= d;
endmodule
