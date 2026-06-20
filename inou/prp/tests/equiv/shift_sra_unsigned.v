// Golden for 2i-issues E (corner): the idiom on an UNSIGNED u8 — the right
// shift is logical (zero-fill), not arithmetic.
module \shift_sra_unsigned.top (
   input  [7:0] a
  ,input  [1:0] b
  ,output [7:0] r
);
  assign r = (a << b) ^ (a >> b);
endmodule
