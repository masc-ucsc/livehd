module top(input [63:0] a, b, output [63:0] x, y);
  assign x = (a + 64'd8) & 64'hff;
  assign y = (b + 64'd40) & 64'hffffffffff;
endmodule
