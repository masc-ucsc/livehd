
module sky130_trivial(y, a, b, c);
  wire res0;
  input a;
  input b;
  input c;
  output [1:0] y;

  sky130_fd_sc_hs__nand3b_1 comb1 (
    .A_N(c),
    .B(a),
    .C(b),
    .Y(res0)
  );

  sky130_fd_sc_hs__o21a_1 comb2 (
    .A1(b),
    .A2(a),
    .B1(res0),
    .X(y[0])
  );

  sky130_fd_sc_hs__dfxtp_1 register (
    .CLK(res0),
    .D(y[0]),
    .Q(y[1])
  );


endmodule
