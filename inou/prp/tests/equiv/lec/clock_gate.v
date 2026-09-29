/*
:lec_top: dut
*/
module dut(input clk, input en, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule
