// Golden for tup_out_nested_whole (2i-issues D residual). The nested tuple output
// flattens to escaped dotted leaves \p.lo.hi / \p.lo.mid / \p.hilvl.
module top(input [7:0] a, output [7:0] \p.lo.hi , output [7:0] \p.lo.mid , output [7:0] \p.hilvl );
  assign \p.lo.hi  = a;
  assign \p.lo.mid = a + 8'd1;
  assign \p.hilvl  = a << 1;
endmodule
