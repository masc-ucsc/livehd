// Golden for 2-level nested tuple ports.
module \tup_port_nested.f (
   input  [7:0] \ar.x 
  ,input  [7:0] \ar.inner.a 
  ,input  [7:0] \ar.inner.b 
  ,output [8:0] \p.q 
  ,output [7:0] \p.s.m 
  ,output [8:0] \p.s.n 
);
  assign \p.q   = {1'b0, \ar.x } + {1'b0, \ar.inner.a };
  assign \p.s.m  = \ar.inner.b ;
  assign \p.s.n  = {1'b0, \ar.x } + {1'b0, \ar.inner.b };
endmodule
