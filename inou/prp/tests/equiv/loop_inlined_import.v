module top(input [7:0] x, output [7:0] lanes, output [3:0] direct);
  assign lanes = x ^ 8'h33;
  assign direct = ~x[3:0];
endmodule
