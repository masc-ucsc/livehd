// Acyclic golden for sim_loop_mux1h_selfslice: the Pyrope's word-level false
// loop unrolled at bit level. w[3:0] is the Mux1H of a/b; the high nibble is
// the Mux1H of (w[3:0]|a) / (w[3:0]|b).
module sim_loop_mux1h_selfslice(input [1:0] sel, input [3:0] a, input [3:0] b, output [7:0] z);
  wire [3:0] lo = ({4{sel[0]}} & a) | ({4{sel[1]}} & b);
  wire [3:0] e0 = lo | a;
  wire [3:0] e1 = lo | b;
  wire [3:0] hi = ({4{sel[0]}} & e0) | ({4{sel[1]}} & e1);
  assign z = {hi, lo};
endmodule
