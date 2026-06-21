// Golden for tup_out_cond_whole (2i-issues D residual). The tuple output flattens
// to escaped leaf ports \p.first / \p.second; the conditional whole-tuple write
// must select per arm on c.
module top(input c, output [7:0] \p.first , output [7:0] \p.second );
  assign \p.first  = c ? 8'd1 : 8'd3;
  assign \p.second = c ? 8'd2 : 8'd4;
endmodule
