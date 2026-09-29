/*
`busy` stored complemented under the same register name, sync reset folded
into the next-state logic (what a technology mapper may emit).
*/
module top(input clk, input rst, input start, input [3:0] din, output [3:0] q, output qb);
reg busy;
reg [3:0] data;
always @(posedge clk) begin
  busy <= rst | ~start;
  if (!busy) data <= din;
end
assign q = data;
assign qb = ~busy;
endmodule
