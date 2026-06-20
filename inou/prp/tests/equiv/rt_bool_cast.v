// Golden for runtime bool() cast. Unsigned ports (lgcheck compares bit vectors).
module \rt_bool_cast.rt_bool_cast (
   input  [7:0] a
  ,input  [7:0] b
  ,output       zb
  ,output [3:0] zc
);
  assign zb = (a != 0);                 // bool(a)
  assign zc = (b != 0) ? 4'd1 : 4'd2;   // int(bool(b)) + 2
endmodule
