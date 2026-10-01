// Golden for comb_default_call.prp: `f`'s omitted `b` defaults to `a + 1`
// (8-bit wrap), `k`'s to 4, `n`'s to `a + 2`, `m`'s to `(a + 1) & 0x0f`, `p`'s
// to 5 and `q`'s to `c + 1`.
module comb_default_call (
  input  [7:0] x,
  input  [7:0] z,
  output [7:0] y,
  output [7:0] w,
  output [7:0] v,
  output [7:0] y1,
  output [7:0] y2,
  output [7:0] y5,
  output [7:0] y6
);
  wire [7:0] x1 = x + 8'd1;
  wire [7:0] x2 = x + 8'd2;
  wire [7:0] z1 = z + 8'd1;
  assign y  = x ^ x1;
  assign w  = x ^ z;
  assign v  = x & 8'd4;
  assign y1 = x ^ x2;
  assign y2 = x ^ (x1 & 8'h0f);
  assign y5 = x ^ 8'd5;
  assign y6 = x ^ z1;
endmodule
