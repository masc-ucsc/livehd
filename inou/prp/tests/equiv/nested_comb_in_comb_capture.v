// Golden for nested_comb_in_comb_capture.prp: N = 3, W = 4.
module nested_comb_in_comb_capture (
  input  [7:0] a,
  output [7:0] o,
  output [3:0] p
);
  assign o = a + 8'd3;
  assign p = a[3:0];
endmodule
