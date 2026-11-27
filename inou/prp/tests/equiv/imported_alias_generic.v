// Golden for imported_alias_generic.prp: W = 8, Row<W> = 8 bits, Row<(W*2)> = 16,
// the default Row = 8, Nib<4> = 4, Row<W=3> = 3.
module top (
  input  [7:0]  a,
  input  [15:0] b,
  input  [7:0]  c,
  input  [3:0]  n,
  input  [2:0]  m,
  input  [7:0]  k,
  output [15:0] y,
  output [3:0]  z,
  output [7:0]  q,
  output [2:0]  r,
  output [7:0]  s
);
  assign y = b ^ {8'd0, a};
  assign z = n;
  assign q = c;
  assign r = m;
  assign s = k;
endmodule
