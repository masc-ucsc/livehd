/*
:lec_expect: refuted
*/
module dut(input clk, input [7:0] d, output reg [7:0] q);
  wire nclk = ~clk;
  always @(posedge nclk) q <= d;
endmodule
