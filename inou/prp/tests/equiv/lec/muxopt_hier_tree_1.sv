/*
:lec_expect: proven
*/
// The same selection as one flat indexed part-select.
module top(input logic [2:0] sel, input logic [7:0][3:0] in, output logic [3:0] out);
  assign out = in[sel];
endmodule
