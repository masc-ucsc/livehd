/*
:lec_top: dut
*/
module dut(input clk, input [7:0] d, output reg [7:0] q);
  always @(negedge clk) q <= d;
endmodule
