// Golden for 2i-issues D: tuple-typed output port driven by a whole-tuple write.
// `comb top(c)->(p:(first:u8,second:u8))` lowers to escaped dotted leaf ports
// `\p.first `,`\p.second `; the whole-tuple write `p=(first=1,second=2)` drives
// the two leaves with the constants 1 and 2 (input `c` is unused).
module \tup_out_port.top (
   input        c
  ,output [7:0] \p.first
  ,output [7:0] \p.second
);
  assign \p.first  = 8'd1;
  assign \p.second  = 8'd2;
endmodule
