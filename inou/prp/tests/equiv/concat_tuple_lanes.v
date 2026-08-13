// Golden for the tuple/dotted concat lane forms — written from the
// specification in docs/pyrope/10-internals.md, not from the emitter's output.
// (The Pyrope side does not compile yet; see concat_tuple_lanes.prp.)
//
// A tuple lane expands to its fields in DECLARATION order, field 0 most
// significant, so `concat(p, c)` and `concat(p.hi, p.lo, c)` are the same nine
// bits: p.hi in [8:5], p.lo in [4:2], c in [1:0]. `\p.lo ` is signed and still
// occupies exactly its own three bits — a Verilog concatenation operand is
// self-determined, which is the same rule a concat lane states.
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
  assign zt = {\p.hi , \p.lo , c};  // the tuple lane, expanded
  assign zd = {\p.hi , \p.lo , c};  // the same layout, spliced by hand
endmodule
