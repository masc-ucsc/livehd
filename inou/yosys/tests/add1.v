module add1(input signed [3:0] a, input [3:0] b, output [4:0] c1, output signed [6:1] c2, output [2:0] c3);
  assign c1 = a + b;
  assign c2 = a + b;
  assign c3 = a + b;
endmodule
