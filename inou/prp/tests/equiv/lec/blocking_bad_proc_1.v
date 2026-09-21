/*
*/
module bad_proc(input clk, input [7:0] d, output reg [7:0] q);
  reg [7:0] stage;
  always @(posedge clk) stage = d;
  always @(posedge clk) q <= stage;
endmodule
