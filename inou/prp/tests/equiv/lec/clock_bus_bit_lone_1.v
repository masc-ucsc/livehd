/*
Same design, the clock bit taken through a wire. Must PROVE.
*/
module cbo(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  wire c1 = clks[1];
  always @(posedge c1) q <= d;
  always @(negedge c1) n <= q;
endmodule
