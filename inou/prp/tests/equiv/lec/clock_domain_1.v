/*
:lec_expect: refuted
*/
module dut(input clk, input clk2, input [7:0] d, output reg [7:0] q);
  reg [7:0] r;
  always @(posedge clk)  r <= d;
  always @(posedge clk2) q <= r;
endmodule
