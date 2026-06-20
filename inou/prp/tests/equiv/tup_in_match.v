// Golden for 2i-issues C (corner): tuple input port read inside match arms.
//   res = match s { 0->x ; 1->y ; 2->x+y ; else 0 }  as a 5-bit value.
module \tup_in_match.top (
   input  [3:0] \ar.x
  ,input  [3:0] \ar.y
  ,input  [1:0] s
  ,output reg [4:0] res
);
  always @* begin
    case (s)
      2'd0   : res = {1'b0, \ar.x };
      2'd1   : res = {1'b0, \ar.y };
      2'd2   : res = \ar.x  + \ar.y ;
      default: res = 5'd0;
    endcase
  end
endmodule
