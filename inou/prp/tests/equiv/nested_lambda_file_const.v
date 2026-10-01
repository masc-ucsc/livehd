// Golden for nested_lambda_file_const.prp: x = 2, w = x * 2 = 4.
module nested_lambda_file_const (
  input  [7:0] a,
  output [7:0] r,
  output [3:0] l
);
  assign r = a + 8'd2;
  assign l = a[3:0];
endmodule
