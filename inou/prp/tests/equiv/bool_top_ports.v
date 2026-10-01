// Golden: bool ports are plain 1-bit ports (true == 1). `run` is a
// set/clear flag with a synchronous reset; `run_bit` is the same flag as a u1.
module bool_top_ports(input clk, input rst, input go, input stop, input [3:0] n,
                      output busy, output hit, output [3:0] cnt, output run_bit);
  reg run;
  always @(posedge clk) begin
    if (rst)       run <= 1'b0;
    else if (go)   run <= 1'b1;
    else if (stop) run <= 1'b0;
  end
  assign busy = run;
  assign hit  = go & ~stop;
  assign cnt  = run ? n : 4'd0;
  assign run_bit = run;
endmodule
