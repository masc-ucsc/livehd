/*
:lec_top: sw
*/
module sw(input [31:0] d, output [31:0] q32, output [15:0] q16, output [31:0] q16b, output [31:0] same);
  assign q32  = {d[7:0], d[15:8], d[23:16], d[31:24]};
  assign q16  = {d[7:0], d[15:8]};
  assign q16b = {d[15:0], d[31:16]};
  assign same = d;
endmodule
