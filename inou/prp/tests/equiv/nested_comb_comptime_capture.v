// Golden for nested_comb_comptime_capture.prp: N = 3, W = 4, N * 5 = 15.
module nested_comb_comptime_capture (
  input  [7:0] a,
  output [7:0] o,
  output [3:0] p,
  output [7:0] q
);
  assign o = a + 8'd3;
  assign p = a[3:0];
  assign q = a ^ 8'd15;
endmodule
