module top(input [7:0] x, input [7:0] y, input [7:0] z, output [8:0] s2, output [9:0] s3);
  assign s2 = x + y;
  assign s3 = x + y + z;
endmodule
