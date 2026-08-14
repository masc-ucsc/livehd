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
);
  assign zt = {\p.lo , \p.hi , c};
  assign zd = {\p.hi , \p.lo , c};
endmodule
