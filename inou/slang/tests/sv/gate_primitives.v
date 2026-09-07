module gate_primitives(input [3:0] a, b, input c, output [10:0] y);
  wire [3:0] n;
  wire first;
  // Deliberately consume a gate output before its producer in source order.
  xor g0(y[0], first, c);
  nand g1(first, a[0], b[0], a[1]);
  and g2(y[1], a[0], b[0], c);
  or g3(y[2], a[1], b[1], c);
  nor g4(y[3], a[2], b[2], c);
  xnor g5(y[4], a[3], b[3], c);
  not inv[3:0](n, a);
  buf multi(y[5], y[6], n[0]);
  not multi_inv(y[7], y[8], n[1]);
  assign y[10:9] = n[3:2];
endmodule
