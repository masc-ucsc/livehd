// Hand-written golden for loop_triangle_share.prp: bit i*4 + j of lo_tri is
// a[i] ^ a[j] below the diagonal (j < i), bit i*4 + j of up_tri is a[i] & a[j]
// above it (j > i); every other bit is 0.
module \loop_triangle_share.top (
  input  [3:0]  a,
  output [15:0] lo_tri,
  output [15:0] up_tri
);
  assign lo_tri = {1'b0, a[3] ^ a[2], a[3] ^ a[1], a[3] ^ a[0],
                   2'b0, a[2] ^ a[1], a[2] ^ a[0],
                   3'b0, a[1] ^ a[0],
                   4'b0};
  assign up_tri = {4'b0,
                   a[2] & a[3], 3'b0,
                   a[1] & a[3], a[1] & a[2], 2'b0,
                   a[0] & a[3], a[0] & a[2], a[0] & a[1], 1'b0};
endmodule
