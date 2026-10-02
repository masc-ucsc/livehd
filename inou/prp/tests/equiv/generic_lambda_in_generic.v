module top(input [7:0] a, output [7:0] v, output [7:0] u, output [7:0] t, output [7:0] w);
  assign v = (a & 8'h0F) ^ 8'd1;
  assign u = a ^ 8'hFF;
  assign t = a & 8'h0F;
  assign w = a ^ 8'hFF;
endmodule
