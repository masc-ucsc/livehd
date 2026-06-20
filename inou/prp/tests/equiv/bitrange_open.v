// Golden for open-ended bit slices `a#[k..]` = bits k..MSB, zero-extended:
//   a#[0..]  = the whole value (same as the bare `#[..]` full slice)
//   a#[5..]  = a[15:5]  (== a >> 5; the bits above the MSB are zero)
//   a#[15..] = a[15]    (the single top bit)
module \bitrange_open.osel (
  input  [15:0] a,
  output [15:0] zfull,
  output [10:0] zoff,
  output        zmsb
);
  assign zfull = a;
  assign zoff  = a[15:5];
  assign zmsb  = a[15];
endmodule
