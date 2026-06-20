// Golden for reduce_popcount: each output is the explicit sum of its selected
// bits (popcount). The continuous-assignment context width is the (widest) LHS,
// so every 1-bit term zero-extends to that width and the sum never truncates.
module \reduce_popcount.foo (
  input  [15:0]        a,
  input  signed [15:0] b,
  output [1:0]         p2,
  output [2:0]         p4,
  output [1:0]         p3,
  output [2:0]         p5,
  output [2:0]         p7,
  output [4:0]         pf,
  output [3:0]         ps,
  output               p1
);
  assign p2 = a[0] + a[1];
  assign p4 = a[0] + a[1] + a[2] + a[3];
  assign p3 = a[0] + a[1] + a[2];
  assign p5 = a[0] + a[1] + a[2] + a[3] + a[4];
  assign p7 = a[2] + a[3] + a[4] + a[5] + a[6] + a[7] + a[8];
  assign pf = a[0]  + a[1]  + a[2]  + a[3]  + a[4]  + a[5]  + a[6]  + a[7]
            + a[8]  + a[9]  + a[10] + a[11] + a[12] + a[13] + a[14] + a[15];
  assign ps = b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + b[7] + b[8] + b[9] + b[10] + b[11];
  assign p1 = a[9];
endmodule
