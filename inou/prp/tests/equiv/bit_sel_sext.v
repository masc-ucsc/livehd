// Golden for 2f-bit_sel_sext RTL coverage (verified with lgcheck).
// zout: pure-unsigned arithmetic over the 1-bit zext (a[3]) and 2-bit zext
// (a[1:0]) — a set bit reads as 1, so zout is 0..1030 (never wraps negative).
// sout: the 2-bit field a[7:6] sign-extended on its own signed port (-2..1).
module \bit_sel_sext.bsel (
  input  [7:0]  a,
  output [31:0] zout,
  output signed [7:0] sout
);
  wire       e1 = a[3];                 // 1-bit zext
  wire [1:0] e2 = a[1:0];               // 2-bit zext
  assign zout = (e1 * 32'd1000) + (e2 * 32'd10);
  assign sout = $signed(a[7:6]);        // sign-extend the 2-bit field -> -2..1
endmodule
