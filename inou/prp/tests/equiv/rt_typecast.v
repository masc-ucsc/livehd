// Golden for runtime scalar typecasts. Unsigned ports (lgcheck compares bit
// vectors, so the signed/unsigned port decl is immaterial — see rt_wrap_u.v).
//   int(true) == -1, int(false) == 0   (1-bit signed all-ones)
//   u8(true)  ==  1, u8(false)  == 0   (unsigned magnitude bit)
//   s8(true)  == -1, s8(false)  == 0
//   int(c)/u8(c) == c                  (value-preserving, c provably fits)
module \rt_typecast.rt_typecast (
   input  [7:0] a
  ,input  [7:0] b
  ,input  [3:0] c
  ,output [3:0] zi
  ,output [3:0] zu
  ,output [3:0] zs
  ,output [7:0] zc
  ,output [7:0] zd
);
  wire lt = a < b;
  assign zi = lt ? 4'd0 : 4'd1;   // (-1)+1=0  /  0+1=1
  assign zu = lt ? 4'd2 : 4'd1;   //   1 +1=2  /  0+1=1
  assign zs = lt ? 4'd1 : 4'd2;   // (-1)+2=1  /  0+2=2
  assign zc = {4'd0, c} + 8'd1;   // c+1
  assign zd = {4'd0, c} - 8'd3;   // c-3 (8-bit two's complement)
endmodule
