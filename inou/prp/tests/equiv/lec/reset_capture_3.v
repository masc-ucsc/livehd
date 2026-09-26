/*
Negative control: `_1` with the folded reset dropped from `busy`. The
reference's pre-reset unknown plane on `busy` must clear once reset lands, so
the wrong post-reset `qb` still refutes.
:lec_expect: refuted
*/
module top(input clk, input rst, input start, input [3:0] din, output [3:0] q, output qb);
reg busy;
reg [3:0] data;
always @(posedge clk) begin
  busy <= ~start;
  if (!busy) data <= din;
end
assign q = data;
assign qb = ~busy;
endmodule
