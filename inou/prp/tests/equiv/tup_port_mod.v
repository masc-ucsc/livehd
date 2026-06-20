// Golden for tuple-typed mod ports with @[0] (combinational feedthrough).
module \tup_port_mod.f (
   input  [7:0] \ar.x 
  ,input  [7:0] \ar.y 
  ,output [8:0] \p.q 
  ,output [7:0] \p.r 
);
  assign \p.q  = {1'b0, \ar.x } + {1'b0, \ar.y };
  assign \p.r  = \ar.x ;
endmodule
