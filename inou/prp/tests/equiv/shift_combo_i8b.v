// Golden for 2i-issues E: the signed-shift idiom (a<<b)^(a>>b) on i8.
// `a << b` truncates to 8 bits (the `wrap`); `a >>> b` is the arithmetic
// (sign-filling) right shift; their xor is the 8-bit result.
module \shift_combo_i8.top (
   input  signed [7:0] a
  ,input         [1:0] b
  ,output signed [7:0] r
);
  wire signed [7:0] shl = a << b;   // self-determined 8-bit truncation
  wire signed [7:0] sra = a >>> b;  // arithmetic right shift
  assign r = shl ^ sra;
endmodule
