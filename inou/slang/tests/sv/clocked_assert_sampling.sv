// :test: roundtrip_sim
// :verilog_re: initial .*assert_sample.* = .*0
// :verilog_re: always @\(negedge clk
module clocked_assert_sampling(input clk, enable, ok, output reg captured);
  always @(negedge clk) begin
    if (enable) assert(ok);
    captured <= ok;
  end
endmodule
