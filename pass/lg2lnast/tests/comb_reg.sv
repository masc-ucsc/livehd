module comb_reg(input clk, input [7:0] a, input [7:0] b, input s, output [8:0] y, output [7:0] q, output z);
  reg [7:0] r;
  wire [8:0] sum = a + b;
  assign y = s ? sum : {1'b0, a ^ b};
  always @(posedge clk) r <= r + a;
  assign q = r;
  assign z = a < b;
endmodule
