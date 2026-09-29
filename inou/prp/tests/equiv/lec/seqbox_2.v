/*
:lec_expect: refuted
*/
module sleaf(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
module top(input clk, input [7:0] x, output [7:0] o);
  wire [7:0] m; sleaf u(.clk(clk), .d(x), .q(m));
  reg [7:0] r; always @(posedge clk) r <= m; assign o = r;
endmodule
