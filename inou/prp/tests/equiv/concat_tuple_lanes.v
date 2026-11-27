// Golden for explicit named-bundle field ordering. Named bundles themselves
// have no implicit bit order; selecting each field as a concat lane states it.
// `\p.lo ` is signed and still occupies exactly its own three bits — a Verilog
// concatenation operand is self-determined, which is the same rule a Pyrope
// concat lane states.
//
// The bundle input `p:(hi:u4, lo:s3)` flattens to the dotted leaf ports
// `\p.hi ` / `\p.lo `.
module \concat_tuple_lanes.top (
   input         [3:0] \p.hi
  ,input  signed [2:0] \p.lo
  ,input         [1:0] c
  ,output        [8:0] zt
  ,output        [8:0] zd
  ,output       [11:0] pairs
  ,output        [7:0] direct
  ,output        [7:0] copied
);
  assign direct = {2'b00, \p.hi , c};
  assign copied = direct;
  assign pairs = {2'b00, c, \p.hi , \p.hi [1:0], c};
  assign zt = {\p.lo , \p.hi , c};
  assign zd = {\p.hi , \p.lo , c};
endmodule
