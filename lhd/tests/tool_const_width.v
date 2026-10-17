// Fixture for the `const_bits` column of `lhd tool cat`.
//
// `consts` prints a VALUE, and a value alone does not say what the design
// computes with it: Op_EQ compares `bv_uint` at each operand's own width, so a
// `-1` landing at width 2 IS 3.  `const_bits` carries that width.  This
// fixture holds the shapes whose width is easy to get wrong:
//   * a >64-bit literal, which no int64 path can measure;
//   * a negative literal on a declared-width pin;
//   * the unsized `-1` that LiveHD's zext idiom `get_mask(a,-1)` uses.
module tool_const_width (
    input  [99:0] wide,
    input  [7:0]  b,
    output        q,
    output [7:0]  r
);
  assign q = (wide == 100'h1234567890ABCDEF12345);
  assign r = b ^ 8'sha5;
endmodule
