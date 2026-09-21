/*
:lec_top: psigned
:lec_set: formal.engine=bmc
*/
module psigned(
  input  signed [7:0] a,
  input  signed [7:0] b,
  output signed [15:0] r
);
  wire signed [15:0] p = a * b;
  assign r = p + a;
endmodule
