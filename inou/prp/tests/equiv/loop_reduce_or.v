// GOLDEN for loop_reduce_or.prp -- written by hand from the same
// specification, never generated.
//
// Deliberately spelled with NEITHER of the two constructs the Pyrope side is
// about: no `for`/`generate`, and no `|` reduction operator. Every OR is an
// explicit binary chain over named bits, so the golden is an independent
// statement of the value rather than a transliteration of the reduce.
module \loop_reduce_or.top (
  input  [15:0] a,
  output [3:0]  r,
  output        any
);
  assign r[0] = a[0]  | a[1]  | a[2]  | a[3];
  assign r[1] = a[4]  | a[5]  | a[6]  | a[7];
  assign r[2] = a[8]  | a[9]  | a[10] | a[11];
  assign r[3] = a[12] | a[13] | a[14] | a[15];
  assign any  = r[0] | r[1] | r[2] | r[3];
endmodule
