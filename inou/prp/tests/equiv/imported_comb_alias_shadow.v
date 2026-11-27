module top(input [7:0] a, input [7:0] other, output [7:0] z, output [7:0] w, output [7:0] copied, output [7:0] boxed);
  assign boxed = other;
  assign copied = other;
  assign z = a & 8'h0F;
  assign w = a ^ 8'hFF;
endmodule
