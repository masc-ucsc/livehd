// Golden for nested_comb_plain_const_capture.prp: n = 3, w = 4, w * 5 = 20;
// bump<K=3> adds n + m = 3 + 4, bump<K=6> adds 3 + 7.
module nested_comb_plain_const_capture (
  input  [7:0] a,
  output [7:0] o3,
  output [7:0] p3,
  output [7:0] l3,
  output [7:0] o6
);
  assign o3 = a + 8'd7;
  assign p3 = a ^ 8'd20;
  assign l3 = {4'd0, a[3:0]};
  assign o6 = a + 8'd10;
endmodule
