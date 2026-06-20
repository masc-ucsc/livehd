// Golden for tuple-typed comb ports. The pyrope `comb f(ar:(x,y))->(p:(q,r))`
// lowers to escaped dotted ports `\ar.x `, `\p.q `; this independent reference
// implements q = x + y (u9) and r = x (u8).
module \tup_port_comb.f (
   input  [7:0] \ar.x 
  ,input  [7:0] \ar.y 
  ,output [8:0] \p.q 
  ,output [7:0] \p.r 
);
  assign \p.q  = {1'b0, \ar.x } + {1'b0, \ar.y };
  assign \p.r  = \ar.x ;
endmodule
