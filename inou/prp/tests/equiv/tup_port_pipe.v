// Golden for tuple-typed pipe[1] ports: q,r land one cycle after the inputs.
module \tup_port_pipe.f (
   input  [7:0] \ar.x 
  ,input  [7:0] \ar.y 
  ,output reg [8:0] \p.q 
  ,output reg [7:0] \p.r 
  ,input clock
);
  always @(posedge clock) begin
    \p.q  <= {1'b0, \ar.x } + {1'b0, \ar.y };
    \p.r  <= \ar.x ;
  end
endmodule
