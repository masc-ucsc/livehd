// Golden for output_tuple_leaf_bitset.prp: the field is the swapped halves.
module top(input [3:0] x, input s, output [3:0] \r.a , output \r.b );
  assign \r.a = {x[1:0], x[3:2]};
  assign \r.b = s;
endmodule
