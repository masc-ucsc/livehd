// :test: roundtrip
// :lec_timeout: 30
module clock_buffer(input c, output co);
  assign co = c;
endmodule
module clock_lint_sink(input c, input reset, output y);
  wire unused_inputs = c ^ reset;
  assign y = 1'b1;
endmodule
module writer_clock_paths(input clk_i, input rst_i, input d, output reg q, output reg direct_q, output y);
  wire forwarded;
  clock_buffer buffer_i(clk_i, forwarded);
  clock_lint_sink sink_i(clk_i, rst_i, y);
  always @(posedge clk_i) direct_q <= d;
  always @(posedge forwarded) if (!rst_i) q <= d;
endmodule
