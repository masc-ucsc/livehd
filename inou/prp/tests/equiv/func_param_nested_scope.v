module top(input [7:0] a, output [7:0] z, output [7:0] y, output [7:0] w, output [7:0] v);
  assign z = a & 8'h0F;
  assign y = a ^ 8'hFF;
  assign w = a ^ 8'hFF;
  assign v = a ^ 8'hFF;
endmodule
