/*
:lec_expect: proven
*/
module dut(input clk, input [7:0] d, output reg [7:0] q);
  reg [7:0] a;
  always @(posedge clk) a <= (d + 8'd5) - 8'd2;
  always @(negedge clk) q <= a;
endmodule
