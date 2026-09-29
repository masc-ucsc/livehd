/*
:type: lec
:lec_top: top
*/
// Mixed signed/unsigned values must stay wide before the operation; only the
// output assignment truncates. Selectors test nonzero, including even values.
module top(input signed [7:0] a, input [7:0] b, input [7:0] s,
           output [2:0] shifted, output [2:0] added,
           output less, output [3:0] clipped);
  wire signed [8:0] sa = {a[7], a};
  wire signed [8:0] sb = {1'b0, b};
  assign shifted = s ? (sb >>> 4) : (sa >>> 4);
  assign added = s ? (sb + 9'sd3) : (sa + 9'sd3);
  assign less = s ? (sb < 9'sd7) : (sa < 9'sd7);
  assign clipped = s ? sb[3:0] : sa[3:0];
endmodule
