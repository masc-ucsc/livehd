// Golden for gen_type_cast: widen<u8>(a) = u32(u8(a)) + 1 = a + 1 (32-bit).
// The `u8(a)` (T(a)) cast is an identity for a u8 actual, so the datapath is a
// plain zero-extend-and-increment. This is what the .prp SHOULD lower to once
// the generic `T(...)` constructor-call lowering is implemented.
module \gen_type_cast.top (
   input  [7:0]  x
  ,output [31:0] r
);
  assign r = {24'd0, x} + 32'd1;
endmodule
