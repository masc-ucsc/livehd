module top(input [3:0] a, input [3:0] b, output [3:0] z, output [3:0] y);
  assign z = a ^ b;
  assign y = b;
endmodule
