module sim_packed_selfref_1bit(input [7:0] a, input [7:0] k, output z);
  assign z = (k == a);
endmodule
