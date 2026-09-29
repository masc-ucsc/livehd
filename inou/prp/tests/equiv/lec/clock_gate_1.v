/*
:lec_expect: refuted
*/
module dut(input clk, input en, input [7:0] d, output reg [7:0] q);
  wire gclk = clk & en;
  always @(posedge gclk) q <= d;
endmodule
