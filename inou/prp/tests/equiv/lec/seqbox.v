/*
:lec_set: formal.lec.hier=false formal.engine=bmc formal.bound=8
:lec_collapse: sleaf
*/
module sleaf(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
module top(input clk, input [7:0] x, output [7:0] o);
  sleaf u(.clk(clk), .d(x), .q(o));
endmodule
