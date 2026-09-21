/*
:lec_top: pstruct
:lec_set: formal.engine=bmc
*/
module pstruct(
  input  [2:0] a,
  input  [7:0] x,
  input  [7:0] y,
  output [7:0] r
);
  wire       z = a[2];
  wire [7:0] w = x + y;
  assign r = w + z;
endmodule
