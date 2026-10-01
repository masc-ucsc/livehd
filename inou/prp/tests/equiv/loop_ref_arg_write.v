module loop_ref_arg_write(input [3:0] a, output [7:0] o, output [7:0] p);
  assign o = a[0] + a[1] + a[2] + a[3];
  assign p = 8'd3;
endmodule
