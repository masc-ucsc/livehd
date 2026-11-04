module srasll(u0, u1, s0, s1, y0, y1, y2, y3, y4, y5, y6, y7, x0, x1, x2, x3, x4, x5, x6, x7);

  input [4:0] u0;
  input [2:0] u1;
  input signed [3:0] s0;
  input signed [5:0] s1;

  output [15:0] y0;
  output [15:0] y1;
  output [15:0] y2;
  output [15:0] y3;
  output [15:0] y4;
  output [15:0] y5;
  output [15:0] y6;
  output [15:0] y7;

  output signed [13:0] x0;
  output signed [13:0] x1;
  output signed [13:0] x2;
  output signed [13:0] x3;
  output signed [13:0] x4;
  output signed [13:0] x5;
  output signed [13:0] x6;
  output signed [13:0] x7;

  assign y0 = s0>> s1;
  assign y1 = s0>>>s1;
  assign y2 = u0>> s1;
  assign y3 = u0>>>s1;
  assign y4 = s0>> u1;
  assign y5 = s0>>>u1;
  assign y6 = u0>> u1;
  assign y7 = u0>>>u1;

  assign x0 = s0>> s1;
  assign x1 = s0>>>s1;
  assign x2 = u0>> s1;
  assign x3 = u0>>>s1;
  assign x4 = s0>> u1;
  assign x5 = s0>>>u1;
  assign x6 = u0>> u1;
  assign x7 = u0>>>u1;

endmodule

