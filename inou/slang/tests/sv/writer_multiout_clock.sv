// :test: roundtrip
// :top: clock_consumer
// A child forwards its clock alongside data. Only the clock field should
// acquire Clock type when its parent uses that field to clock a register.
module clock_source(input clk, d, output forwarded, output reg q);
  assign forwarded = clk;
  always @(posedge clk) q <= d;
endmodule

module clock_consumer(input clk, d, output reg result);
  wire forwarded, q;
  clock_source child(.clk(clk), .d(d), .forwarded(forwarded), .q(q));
  always @(posedge forwarded) result <= q;
endmodule
