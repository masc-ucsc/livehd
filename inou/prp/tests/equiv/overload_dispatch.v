module \overload_dispatch.ovdispatch (
   input  [7:0] x
  ,input  [7:0] y
  ,input  [7:0] z
  ,output [8:0] s2
  ,output [9:0] s3
);

  // s2 = add2(x, y)    — the 2-arg overload (u8 + u8)
  // s3 = add3(x, y, z) — the 3-arg overload (u8 + u8 + u8)
  assign s2 = x + y;
  assign s3 = x + y + z;

endmodule
