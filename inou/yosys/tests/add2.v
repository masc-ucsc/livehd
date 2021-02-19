module add2(input [6:0] a, input [7:0] b,
  output signed [5:0] hs,
  output [9:0] hu,
  output signed [9:0] js,
  output [8:0] ju
);

  wire signed [6:0] as = a;
  wire signed [7:0] bs = b;

  assign ju = as + bs;
  assign js = as + bs;

  assign hs = as + bs - as;
  assign hu = as + bs - as;

endmodule

