// :test: roundtrip
// :top: local_clock_alias
module local_clock_alias (
  input  logic clk_i,
  input  logic d,
  output logic q
);
  logic clock;
  assign clock = clk_i;
  always_ff @(posedge clock) q <= d;
endmodule
