module top(input [7:0] a, output [7:0] z, output [7:0] w);
  assign z = a & 8'h0F;
  assign w = a ^ 8'hFF;
endmodule
