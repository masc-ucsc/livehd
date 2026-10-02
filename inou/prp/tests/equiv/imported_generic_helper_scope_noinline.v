module top(input [7:0] a, output [7:0] z, output [7:0] y);
  assign z = a & 8'h0F;
  assign y = a ^ 8'hFF;
endmodule
