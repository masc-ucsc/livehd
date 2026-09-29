/*
:lec_expect: refuted
*/
// Deliberately wrong: the two low bits of the selected symbol are swapped, so
// the width fix must not turn a real difference into PROVEN.
module top(input logic [2:0] sel, input logic [7:0][3:0] in, output logic [3:0] out);
  logic [3:0] s;
  assign s = in[sel];
  assign out = {s[3], s[2], s[0], s[1]};
endmodule
