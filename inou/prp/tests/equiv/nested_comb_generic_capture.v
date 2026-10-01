// Golden for nested_comb_generic_capture.prp: bump<W=5> adds K = 6 / W = 5
// and keeps a[4:0]; bump<W=2> adds K = 3 / W = 2 and keeps a[1:0].
module nested_comb_generic_capture (
  input  [7:0] a,
  output [7:0] o5,
  output [7:0] q5,
  output [4:0] l5,
  output [7:0] o2,
  output [7:0] q2,
  output [1:0] l2
);
  assign o5 = a + 8'd6;
  assign q5 = a + 8'd5;
  assign l5 = a[4:0];
  assign o2 = a + 8'd3;
  assign q2 = a + 8'd2;
  assign l2 = a[1:0];
endmodule
