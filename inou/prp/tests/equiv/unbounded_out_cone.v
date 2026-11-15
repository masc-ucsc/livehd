// Golden for unbounded_out_cone: each value at a width that holds it.
module top (
    input  signed [7:0] a,
    input  [7:0] b,
    input  [7:0] c,
    input  signed [7:0] d,
    output signed [10:0] o,
    output signed [10:0] p,
    output signed [10:0] q,
    output signed [9:0] r,
    output signed [10:0] s,
    output signed [9:0] t
);
  wire signed [10:0] a11 = {{3{a[7]}}, a};
  wire signed [10:0] b11 = {3'b000, b};
  wire signed [10:0] c11 = {3'b000, c};
  wire signed [9:0]  b10 = {2'b00, b};
  wire signed [9:0]  d10 = {{2{d[7]}}, d};
  assign o = a11 + b11;
  assign p = c11 + a11;
  assign q = a11 + c11;
  assign s = a11 + b11;
  assign r = b10 / (d10 | 10'sd1);
  assign t = b10 / (d10 | 10'sd1);
endmodule
