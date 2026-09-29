module outer(input [3:0] x, output [4:0] y);
  assign y = {1'b0,x} + 5'd1;
endmodule
