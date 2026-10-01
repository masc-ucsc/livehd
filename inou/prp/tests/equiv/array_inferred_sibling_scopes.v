module array_inferred_sibling_scopes(input [1:0] c, input [2:0] ia, input [7:0] va, input [2:0] ja,
                                     input ib, input vb, input jb, output [7:0] o);
  assign o = (c == 2'd1) ? ((ib == jb) ? {7'd0, vb} : 8'd0) : ((ia == ja) ? va : 8'd0);
endmodule
