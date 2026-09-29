/*
:lec_expect: refuted
*/
// Deliberately wrong: narrowing the selected operand before shifting loses
// observable high bits. This variant guards the width-policy regression.
module top(input signed [7:0] a, input [7:0] b, input [7:0] s,
           output [2:0] shifted, output [2:0] added,
           output less, output [3:0] clipped);
  wire signed [8:0] selected = s ? {1'b0, b} : {a[7], a};
  wire signed [2:0] narrowed = selected;
  assign shifted = narrowed >>> 4;
  assign added = selected + 9'sd3;
  assign less = selected < 9'sd7;
  assign clipped = selected[3:0];
endmodule
