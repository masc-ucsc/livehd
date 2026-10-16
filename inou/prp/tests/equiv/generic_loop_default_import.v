// Golden for generic_loop_default_import.prp: every output flips lane w
// (bits 2w+1..2w) of `a` when b[w] is set.
module top(input [7:0] a, input [3:0] b, output [7:0] o_loop, output [7:0] o_flat, output [7:0] o_rt,
           output [7:0] o_two, output [7:0] o_spec);
  wire [7:0] m = {{2{b[3]}}, {2{b[2]}}, {2{b[1]}}, {2{b[0]}}};
  assign o_loop = a ^ m;
  assign o_flat = a ^ m;
  assign o_rt   = a ^ m;
  assign o_two  = a ^ m;
  assign o_spec = a ^ m;
endmodule
