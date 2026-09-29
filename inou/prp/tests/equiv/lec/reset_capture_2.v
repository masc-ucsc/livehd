/*
Negative control: `_1` with one captured data bit inverted. Once `busy` has
loaded a known `din` after reset, `q` must diverge.
:lec_expect: refuted
*/
module top(input clk, input rst, input start, input [3:0] din, output [3:0] q, output qb);
reg busy;
reg [3:0] data;
always @(posedge clk) begin
  busy <= rst | ~start;
  if (!busy) data <= din ^ 4'b0001;
end
assign q = data;
assign qb = ~busy;
endmodule
