/*
:type: lec
:lec_top: top
:set: pass.satopt=false
*/
// Correlated comparisons survive the structural facts; arithmetic lives in
// separate output cones whose complete activations are disjoint.
module top(input [7:0] x, a, b, c, d,
           output [7:0] selected,
           output [15:0] left, right);
  assign selected = x < 8 ? (x < 16 ? a : b) : c;
  assign left = x < 8 ? a * b : 16'd11;
  assign right = x > 7 ? c * d : 16'd13;
endmodule
