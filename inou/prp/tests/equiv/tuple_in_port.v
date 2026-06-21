// Golden for tuple_in_port (2i-issues C, now FIXED). The tuple input
// `ar:(x:u3, y:i4)` flattens to escaped dotted leaf ports \ar.x (u3) / \ar.y (i4).
module top(
   input         [2:0] \ar.x
  ,input  signed [3:0] \ar.y
  ,input               cond
  ,output signed [4:0] res
);
  wire signed [4:0] a = {2'b00, \ar.x };           // u3 zero-extended
  wire signed [4:0] b = {{1{\ar.y [3]}}, \ar.y };  // i4 sign-extended
  assign res = cond ? (a + 5'sd1) : (b - 5'sd1);
endmodule
