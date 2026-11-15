// Golden for unbounded_out_width: each value at a width that holds it.
module top (
    input  signed [7:0] a,
    input  [7:0] b,
    input  signed [7:0] d,
    output signed [9:0] s,
    output signed [9:0] u,
    output signed [8:0] t
);
  wire signed [9:0] a10 = {{2{a[7]}}, a};
  wire signed [9:0] b10 = {2'b00, b};
  wire signed [8:0] b9  = {1'b0, b};
  wire signed [8:0] d9  = {d[7], d};
  assign s = a10 + b10;
  assign u = a10 - b10;
  assign t = b9 / (d9 | 9'sd1);
endmodule
