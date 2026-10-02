module top(input [7:0] a, output [7:0] z, output [7:0] y, output [7:0] x);
  assign z = a | 8'h80;
  assign y = a | 8'h80;
  assign x = a | 8'h80;
endmodule
