module reduce_array(input [7:0] a, output z);
  assign z = ^a[2:0];
endmodule
