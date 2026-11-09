module reduce_shift(input [7:0] a, input signed [7:0] b, input [2:0] n, output r, output q, output [15:0] l, output signed [7:0] sr, output [7:0] cmp, output p);
  assign r = |a;
  assign q = |{a, b[3:0]};
  assign l = a << n;
  assign sr = b >>> n;
  assign cmp = {a == 8'd5, a > 8'd7, b < 8'sd0, a != 8'd3, a <= 8'd9, b >= -8'sd2, ~&a, ^b};
  assign p = ^a;
endmodule
