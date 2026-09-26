// Independently clocked bits of one vector register: two clocks writing the
// SAME bit must fail cleanly, never merge the clocks.
// :test: error
// :error: the same register bit is written from different clocks
module bit_clock_overlap_reject(input [1:0] clocks, input d, index, output reg [1:0] q);
  always @(posedge clocks[0]) q[0] <= d;
  always @(posedge clocks[1]) q[0] <= d;
endmodule
