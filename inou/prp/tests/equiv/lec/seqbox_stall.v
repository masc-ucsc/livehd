/*
:lec_set: formal.lec.hier=false formal.engine=ind
:lec_collapse: sleaf
*/
module sleaf(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
module top(input clk, input en, input [7:0] x, output [7:0] o);
  wire [7:0] q; sleaf u(.clk(clk), .d(en ? x : q), .q(q)); assign o = q;
endmodule
