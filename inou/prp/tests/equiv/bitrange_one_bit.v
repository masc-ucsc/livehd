// Golden for bitrange_one_bit.prp: `x#[lo..+1]` / `x#[lo..<lo+1]` /
// `x#[lo..=lo]` all select (or write) the single bit `lo`.
module top (
  input  [3:0] a,
  input        v,
  output       r0, r1, r2, r3, r4, r5, r6, r7,
  output       c0,
  output [1:0] c1,
  output [1:0] c2,
  output [3:0] w0, w1, w2, w3
);
  assign r0 = a[0];
  assign r1 = a[0];
  assign r2 = a[0];
  assign r3 = a[1];
  assign r4 = a[3];
  assign r5 = a[2];
  assign r6 = a[2];
  assign r7 = a[2];
  assign c0 = a[1];
  assign c1 = a[3:2];
  assign c2 = a[3:2];
  assign w0 = {a[3:1], v};
  assign w1 = {a[3:2], v, a[0]};
  assign w2 = {a[3], v, a[1:0]};
  assign w3 = {a[3], v, a[1:0]};
endmodule
