module leaf(input clk, input [7:0] a, output [7:0] y);
  reg [7:0] value;
  always @(posedge clk) value <= a + 8'd1;
  assign y = value;
endmodule
