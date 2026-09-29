/*
:lec_top: top
*/
// Binary 8:1 x 4-bit mux tree built from one-bit gate instances (the shape of
// br_mux_bin_structured_gates). LEC inlines the 28 instances absent from the
// flat variants and re-runs cprop: vectorization plus mux_op_share_pass then
// re-types an UNSTAMPED fresh operand mux into a Get_mask. Without the
// bitwidth pass that follows cprop in compile, the encoder read that pin as
// one bit and a correct design was REFUTED (ref=0 impl=2 @ in=2, sel=0).
module gmux2(input logic in0, input logic in1, input logic sel, output logic out);
  assign out = sel ? in1 : in0;
endmodule

module top(input logic [2:0] sel, input logic [7:0][3:0] in, output logic [3:0] out);
  logic [3:0][7:0][3:0] st;
  always_comb begin
    st[0] = '0;
    st[0][7:0] = in;
  end
  for (genvar i = 0; i < 3; i++) begin : g_l
    localparam int M = 8 / (2 ** (i + 1));
    for (genvar j = 0; j < M; j++) begin : g_m
      for (genvar k = 0; k < 4; k++) begin : g_b
        gmux2 m(.sel(sel[i]), .in0(st[i][2*j][k]), .in1(st[i][2*j+1][k]), .out(st[i+1][j][k]));
      end
    end
    assign st[i+1][7:M] = '0;
  end
  assign out = st[3][0];
endmodule
