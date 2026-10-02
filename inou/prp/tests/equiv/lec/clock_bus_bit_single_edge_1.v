/*
Same design, the clock bit taken through a wire. Must PROVE.
*/
module cbs(input [1:0] clks, input [3:0] d, output reg [3:0] q, output reg [3:0] n);
  wire c0 = clks[0];
  always @(posedge c0) q <= d;
  always @(negedge c0) n <= q;
endmodule
