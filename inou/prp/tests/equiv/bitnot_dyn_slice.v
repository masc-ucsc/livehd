module bitnot_dyn_slice(input [15:0] a, input [1:0] b, output [3:0] r, output [3:0] q);
  assign r = ~a[b*4 +: 4];
  assign q = ~a[b*4 +: 4];
endmodule
