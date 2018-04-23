module add(input [7:0] a, input [7:0] b,
  output [7:0] c,
  output [7:0] d,
  output [7:0] e,
  output [7:0] ei,
  output signed [7:0] f,
  output signed [7:0] g,
  output signed [7:0] h,
  output signed [7:0] hi
);

  assign c = a + b;
  assign d = a - b;
  assign e = a + b - a;
  //assign ei = - a - b;

  signed wire [7:0] as = a;
  signed wire [7:0] bs = b;

  assign f = as + bs;
  assign g = as - bs;
  assign h = as + bs - as;
  //assign hi = -as - bs;

endmodule

