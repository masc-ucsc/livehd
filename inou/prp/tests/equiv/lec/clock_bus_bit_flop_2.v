/*
Equivalent rewrite: the clock bit is selected through a renamed bus.
*/
module cbf(input [1:0] clks, input [3:0] d, output reg [3:0] q);
  wire [1:0] cc = clks;
  wire c = cc[0];
  always @(posedge c) q <= d;
endmodule
