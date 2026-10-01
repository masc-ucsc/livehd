// Golden for spec_memory_untyped_elem_array: all-zero array, one store, one read.
module spec_memory_untyped_elem_array(input [2:0] a, input [7:0] d, input [2:0] b, output [7:0] q);
  assign q = (a == b) ? d : 8'd0;
endmodule
