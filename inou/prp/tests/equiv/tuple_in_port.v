// Golden for tuple_in_port (2i-issues C). Intended lowering: the tuple input
// `ar:(x:u3, y:i4)` flattens to scalar leaf ports ar_x (u3) and ar_y (i4).
module top(input [2:0] ar_x, input signed [3:0] ar_y, input cond, output signed [4:0] res);
  assign res = cond ? ($signed({2'b0, ar_x}) + 5'sd1) : ($signed(ar_y) - 5'sd1);
endmodule
