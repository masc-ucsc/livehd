// Golden for unbounded int/unsigned outputs sized to their value range.
// Unsigned ports (lgcheck compares bit vectors — see rt_wrap_u.v).
module \rt_int_output.rt_int_output (
   input  [3:0] a
  ,input  [3:0] c
  ,output [5:0] zi
  ,output [5:0] zu
  ,output [4:0] zc
  ,output [9:0] zk
  ,output [8:0] zn
);
  assign zi = a + 1;            // 1..16
  assign zu = a + 1;            // 1..16
  assign zc = c;                // int(c) == c, 0..15
  assign zk = 10'd300;          // wide comptime constant
  assign zn = {5'd0, c} - 9'd100;  // int(c)-100 = -100..-85 (two's complement)
endmodule
