module p(input [3:0] x, output [3:0] a, b);
  assign a = x + 4'd1;
  assign b = x * 4'd2;
endmodule
