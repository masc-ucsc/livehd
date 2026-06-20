// Golden for tuple_out_port (2i-issues D). Intended lowering: the tuple output
// `p:(first:u8, second:u8)` flattens to scalar leaf ports p_first / p_second.
module top(input c, output [7:0] \p.first , output [7:0] \p.second );
  assign \p.first  = 8'd1;
  assign \p.second = 8'd2;
endmodule
