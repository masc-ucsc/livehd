// Two division sites by a runtime-zero divisor: each must warn exactly once.
module warn_div(input clock, input reset, input [7:0] a, input [7:0] b, output [7:0] q, output [7:0] r);
  assign q = a / b;
  assign r = a % b;
endmodule
