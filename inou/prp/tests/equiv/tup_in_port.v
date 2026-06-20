// Golden for 2i-issues C: a tuple-typed input port read inside an if-expr.
// `comb top(ar:(x:u3,y:i4),cond)->(res:i5)` lowers to escaped dotted leaf ports
// `\ar.x `,`\ar.y `; this independent reference computes
//   res = cond ? (u3(x) + 1) : (i4(y) - 1)   as a 5-bit signed value.
module \tup_in_port.top (
   input         [2:0] \ar.x
  ,input  signed [3:0] \ar.y
  ,input               cond
  ,output signed [4:0] res
);
  wire signed [4:0] a = {2'b00, \ar.x };           // u3 zero-extended
  wire signed [4:0] b = {{1{\ar.y [3]}}, \ar.y };  // i4 sign-extended
  assign res = cond ? (a + 5'sd1) : (b - 5'sd1);
endmodule
