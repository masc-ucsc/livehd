// Independently clocked bits of one vector register need nonblocking
// assignments; blocking writes must fail cleanly.
// :test: error
// :error: independently clocked vector bits require nonblocking assignments
module bit_clock_blocking_reject(input [1:0] clocks, input d, index, output reg [1:0] q);
  always @(posedge clocks[0]) q[0] = d;
  always @(posedge clocks[1]) q[1] = d;
endmodule
