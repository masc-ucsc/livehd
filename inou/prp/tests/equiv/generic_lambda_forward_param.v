module top(input [7:0] a, output [7:0] z, output [7:0] y, output [7:0] x, output [7:0] w);
  assign z = a ^ 8'hFF;
  assign y = a ^ 8'hFF;
  assign x = a ^ 8'hFF;
  assign w = a & 8'h0F;
endmodule
