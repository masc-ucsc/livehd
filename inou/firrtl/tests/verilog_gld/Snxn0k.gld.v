module SnxnLv3Inst0(
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  t0 = io_a + io_b; // @[Snxn0k.scala 468:20]
  wire  inv0 = ~t0; // @[Snxn0k.scala 469:15]
  wire  x0 = t0 ^ inv0; // @[Snxn0k.scala 470:18]
  wire  invx0 = ~x0; // @[Snxn0k.scala 471:15]
  wire  t1 = x0 + invx0; // @[Snxn0k.scala 472:18]
  wire  inv1 = ~t1; // @[Snxn0k.scala 473:15]
  wire  x1 = t1 ^ inv1; // @[Snxn0k.scala 474:18]
  assign io_z = ~x1; // @[Snxn0k.scala 475:15]
endmodule
module SnxnLv2Inst0(
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  inst_SnxnLv3Inst0_io_a; // @[Snxn0k.scala 256:33]
  wire  inst_SnxnLv3Inst0_io_b; // @[Snxn0k.scala 256:33]
  wire  inst_SnxnLv3Inst0_io_z; // @[Snxn0k.scala 256:33]
  SnxnLv3Inst0 inst_SnxnLv3Inst0 ( // @[Snxn0k.scala 256:33]
    .io_a(inst_SnxnLv3Inst0_io_a),
    .io_b(inst_SnxnLv3Inst0_io_b),
    .io_z(inst_SnxnLv3Inst0_io_z)
  );
  assign io_z = inst_SnxnLv3Inst0_io_z ^ io_a; // @[Snxn0k.scala 261:15]
  assign inst_SnxnLv3Inst0_io_a = io_a; // @[Snxn0k.scala 257:26]
  assign inst_SnxnLv3Inst0_io_b = io_b; // @[Snxn0k.scala 258:26]
endmodule
module SnxnLv1Inst0(
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  inst_SnxnLv2Inst0_io_a; // @[Snxn0k.scala 83:33]
  wire  inst_SnxnLv2Inst0_io_b; // @[Snxn0k.scala 83:33]
  wire  inst_SnxnLv2Inst0_io_z; // @[Snxn0k.scala 83:33]
  wire  inst_SnxnLv2Inst1_io_a; // @[Snxn0k.scala 87:33]
  wire  inst_SnxnLv2Inst1_io_b; // @[Snxn0k.scala 87:33]
  wire  inst_SnxnLv2Inst1_io_z; // @[Snxn0k.scala 87:33]
  wire  inst_SnxnLv2Inst2_io_a; // @[Snxn0k.scala 91:33]
  wire  inst_SnxnLv2Inst2_io_b; // @[Snxn0k.scala 91:33]
  wire  inst_SnxnLv2Inst2_io_z; // @[Snxn0k.scala 91:33]
  wire  inst_SnxnLv2Inst3_io_a; // @[Snxn0k.scala 95:33]
  wire  inst_SnxnLv2Inst3_io_b; // @[Snxn0k.scala 95:33]
  wire  inst_SnxnLv2Inst3_io_z; // @[Snxn0k.scala 95:33]
  wire  _sum_T_1 = inst_SnxnLv2Inst0_io_z + inst_SnxnLv2Inst1_io_z; // @[Snxn0k.scala 99:36]
  wire  _sum_T_3 = _sum_T_1 + inst_SnxnLv2Inst2_io_z; // @[Snxn0k.scala 99:61]
  wire  sum = _sum_T_3 + inst_SnxnLv2Inst3_io_z; // @[Snxn0k.scala 99:86]
  SnxnLv2Inst0 inst_SnxnLv2Inst0 ( // @[Snxn0k.scala 83:33]
    .io_a(inst_SnxnLv2Inst0_io_a),
    .io_b(inst_SnxnLv2Inst0_io_b),
    .io_z(inst_SnxnLv2Inst0_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst1 ( // @[Snxn0k.scala 87:33]
    .io_a(inst_SnxnLv2Inst1_io_a),
    .io_b(inst_SnxnLv2Inst1_io_b),
    .io_z(inst_SnxnLv2Inst1_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst2 ( // @[Snxn0k.scala 91:33]
    .io_a(inst_SnxnLv2Inst2_io_a),
    .io_b(inst_SnxnLv2Inst2_io_b),
    .io_z(inst_SnxnLv2Inst2_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst3 ( // @[Snxn0k.scala 95:33]
    .io_a(inst_SnxnLv2Inst3_io_a),
    .io_b(inst_SnxnLv2Inst3_io_b),
    .io_z(inst_SnxnLv2Inst3_io_z)
  );
  assign io_z = sum ^ io_a; // @[Snxn0k.scala 100:15]
  assign inst_SnxnLv2Inst0_io_a = io_a; // @[Snxn0k.scala 84:26]
  assign inst_SnxnLv2Inst0_io_b = io_b; // @[Snxn0k.scala 85:26]
  assign inst_SnxnLv2Inst1_io_a = io_a; // @[Snxn0k.scala 88:26]
  assign inst_SnxnLv2Inst1_io_b = io_b; // @[Snxn0k.scala 89:26]
  assign inst_SnxnLv2Inst2_io_a = io_a; // @[Snxn0k.scala 92:26]
  assign inst_SnxnLv2Inst2_io_b = io_b; // @[Snxn0k.scala 93:26]
  assign inst_SnxnLv2Inst3_io_a = io_a; // @[Snxn0k.scala 96:26]
  assign inst_SnxnLv2Inst3_io_b = io_b; // @[Snxn0k.scala 97:26]
endmodule
module SnxnLv1Inst1(
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  inst_SnxnLv2Inst4_io_a; // @[Snxn0k.scala 149:33]
  wire  inst_SnxnLv2Inst4_io_b; // @[Snxn0k.scala 149:33]
  wire  inst_SnxnLv2Inst4_io_z; // @[Snxn0k.scala 149:33]
  wire  inst_SnxnLv2Inst5_io_a; // @[Snxn0k.scala 153:33]
  wire  inst_SnxnLv2Inst5_io_b; // @[Snxn0k.scala 153:33]
  wire  inst_SnxnLv2Inst5_io_z; // @[Snxn0k.scala 153:33]
  wire  inst_SnxnLv2Inst6_io_a; // @[Snxn0k.scala 157:33]
  wire  inst_SnxnLv2Inst6_io_b; // @[Snxn0k.scala 157:33]
  wire  inst_SnxnLv2Inst6_io_z; // @[Snxn0k.scala 157:33]
  wire  inst_SnxnLv2Inst7_io_a; // @[Snxn0k.scala 161:33]
  wire  inst_SnxnLv2Inst7_io_b; // @[Snxn0k.scala 161:33]
  wire  inst_SnxnLv2Inst7_io_z; // @[Snxn0k.scala 161:33]
  wire  _sum_T_1 = inst_SnxnLv2Inst4_io_z + inst_SnxnLv2Inst5_io_z; // @[Snxn0k.scala 165:36]
  wire  _sum_T_3 = _sum_T_1 + inst_SnxnLv2Inst6_io_z; // @[Snxn0k.scala 165:61]
  wire  sum = _sum_T_3 + inst_SnxnLv2Inst7_io_z; // @[Snxn0k.scala 165:86]
  SnxnLv3Inst0 inst_SnxnLv2Inst4 ( // @[Snxn0k.scala 149:33]
    .io_a(inst_SnxnLv2Inst4_io_a),
    .io_b(inst_SnxnLv2Inst4_io_b),
    .io_z(inst_SnxnLv2Inst4_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst5 ( // @[Snxn0k.scala 153:33]
    .io_a(inst_SnxnLv2Inst5_io_a),
    .io_b(inst_SnxnLv2Inst5_io_b),
    .io_z(inst_SnxnLv2Inst5_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst6 ( // @[Snxn0k.scala 157:33]
    .io_a(inst_SnxnLv2Inst6_io_a),
    .io_b(inst_SnxnLv2Inst6_io_b),
    .io_z(inst_SnxnLv2Inst6_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst7 ( // @[Snxn0k.scala 161:33]
    .io_a(inst_SnxnLv2Inst7_io_a),
    .io_b(inst_SnxnLv2Inst7_io_b),
    .io_z(inst_SnxnLv2Inst7_io_z)
  );
  assign io_z = sum ^ io_a; // @[Snxn0k.scala 166:15]
  assign inst_SnxnLv2Inst4_io_a = io_a; // @[Snxn0k.scala 150:26]
  assign inst_SnxnLv2Inst4_io_b = io_b; // @[Snxn0k.scala 151:26]
  assign inst_SnxnLv2Inst5_io_a = io_a; // @[Snxn0k.scala 154:26]
  assign inst_SnxnLv2Inst5_io_b = io_b; // @[Snxn0k.scala 155:26]
  assign inst_SnxnLv2Inst6_io_a = io_a; // @[Snxn0k.scala 158:26]
  assign inst_SnxnLv2Inst6_io_b = io_b; // @[Snxn0k.scala 159:26]
  assign inst_SnxnLv2Inst7_io_a = io_a; // @[Snxn0k.scala 162:26]
  assign inst_SnxnLv2Inst7_io_b = io_b; // @[Snxn0k.scala 163:26]
endmodule
module SnxnLv1Inst2(
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  inst_SnxnLv2Inst8_io_a; // @[Snxn0k.scala 114:33]
  wire  inst_SnxnLv2Inst8_io_b; // @[Snxn0k.scala 114:33]
  wire  inst_SnxnLv2Inst8_io_z; // @[Snxn0k.scala 114:33]
  wire  inst_SnxnLv2Inst9_io_a; // @[Snxn0k.scala 118:33]
  wire  inst_SnxnLv2Inst9_io_b; // @[Snxn0k.scala 118:33]
  wire  inst_SnxnLv2Inst9_io_z; // @[Snxn0k.scala 118:33]
  wire  inst_SnxnLv2Inst10_io_a; // @[Snxn0k.scala 122:34]
  wire  inst_SnxnLv2Inst10_io_b; // @[Snxn0k.scala 122:34]
  wire  inst_SnxnLv2Inst10_io_z; // @[Snxn0k.scala 122:34]
  wire  inst_SnxnLv2Inst11_io_a; // @[Snxn0k.scala 126:34]
  wire  inst_SnxnLv2Inst11_io_b; // @[Snxn0k.scala 126:34]
  wire  inst_SnxnLv2Inst11_io_z; // @[Snxn0k.scala 126:34]
  wire  _sum_T_1 = inst_SnxnLv2Inst8_io_z + inst_SnxnLv2Inst9_io_z; // @[Snxn0k.scala 130:36]
  wire  _sum_T_3 = _sum_T_1 + inst_SnxnLv2Inst10_io_z; // @[Snxn0k.scala 130:61]
  wire  sum = _sum_T_3 + inst_SnxnLv2Inst11_io_z; // @[Snxn0k.scala 130:87]
  SnxnLv3Inst0 inst_SnxnLv2Inst8 ( // @[Snxn0k.scala 114:33]
    .io_a(inst_SnxnLv2Inst8_io_a),
    .io_b(inst_SnxnLv2Inst8_io_b),
    .io_z(inst_SnxnLv2Inst8_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst9 ( // @[Snxn0k.scala 118:33]
    .io_a(inst_SnxnLv2Inst9_io_a),
    .io_b(inst_SnxnLv2Inst9_io_b),
    .io_z(inst_SnxnLv2Inst9_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst10 ( // @[Snxn0k.scala 122:34]
    .io_a(inst_SnxnLv2Inst10_io_a),
    .io_b(inst_SnxnLv2Inst10_io_b),
    .io_z(inst_SnxnLv2Inst10_io_z)
  );
  SnxnLv3Inst0 inst_SnxnLv2Inst11 ( // @[Snxn0k.scala 126:34]
    .io_a(inst_SnxnLv2Inst11_io_a),
    .io_b(inst_SnxnLv2Inst11_io_b),
    .io_z(inst_SnxnLv2Inst11_io_z)
  );
  assign io_z = sum ^ io_a; // @[Snxn0k.scala 131:15]
  assign inst_SnxnLv2Inst8_io_a = io_a; // @[Snxn0k.scala 115:26]
  assign inst_SnxnLv2Inst8_io_b = io_b; // @[Snxn0k.scala 116:26]
  assign inst_SnxnLv2Inst9_io_a = io_a; // @[Snxn0k.scala 119:26]
  assign inst_SnxnLv2Inst9_io_b = io_b; // @[Snxn0k.scala 120:26]
  assign inst_SnxnLv2Inst10_io_a = io_a; // @[Snxn0k.scala 123:27]
  assign inst_SnxnLv2Inst10_io_b = io_b; // @[Snxn0k.scala 124:27]
  assign inst_SnxnLv2Inst11_io_a = io_a; // @[Snxn0k.scala 127:27]
  assign inst_SnxnLv2Inst11_io_b = io_b; // @[Snxn0k.scala 128:27]
endmodule
module Snxn0k(
  input   clock,
  input   reset,
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  inst_SnxnLv1Inst0_io_a; // @[Snxn0k.scala 21:33]
  wire  inst_SnxnLv1Inst0_io_b; // @[Snxn0k.scala 21:33]
  wire  inst_SnxnLv1Inst0_io_z; // @[Snxn0k.scala 21:33]
  wire  inst_SnxnLv1Inst1_io_a; // @[Snxn0k.scala 25:33]
  wire  inst_SnxnLv1Inst1_io_b; // @[Snxn0k.scala 25:33]
  wire  inst_SnxnLv1Inst1_io_z; // @[Snxn0k.scala 25:33]
  wire  inst_SnxnLv1Inst2_io_a; // @[Snxn0k.scala 29:33]
  wire  inst_SnxnLv1Inst2_io_b; // @[Snxn0k.scala 29:33]
  wire  inst_SnxnLv1Inst2_io_z; // @[Snxn0k.scala 29:33]
  wire  inst_SnxnLv1Inst3_io_a; // @[Snxn0k.scala 33:33]
  wire  inst_SnxnLv1Inst3_io_b; // @[Snxn0k.scala 33:33]
  wire  inst_SnxnLv1Inst3_io_z; // @[Snxn0k.scala 33:33]
  wire  _sum_T_1 = inst_SnxnLv1Inst0_io_z + inst_SnxnLv1Inst1_io_z; // @[Snxn0k.scala 37:36]
  wire  _sum_T_3 = _sum_T_1 + inst_SnxnLv1Inst2_io_z; // @[Snxn0k.scala 37:61]
  wire  sum = _sum_T_3 + inst_SnxnLv1Inst3_io_z; // @[Snxn0k.scala 37:86]
  SnxnLv1Inst0 inst_SnxnLv1Inst0 ( // @[Snxn0k.scala 21:33]
    .io_a(inst_SnxnLv1Inst0_io_a),
    .io_b(inst_SnxnLv1Inst0_io_b),
    .io_z(inst_SnxnLv1Inst0_io_z)
  );
  SnxnLv1Inst1 inst_SnxnLv1Inst1 ( // @[Snxn0k.scala 25:33]
    .io_a(inst_SnxnLv1Inst1_io_a),
    .io_b(inst_SnxnLv1Inst1_io_b),
    .io_z(inst_SnxnLv1Inst1_io_z)
  );
  SnxnLv1Inst2 inst_SnxnLv1Inst2 ( // @[Snxn0k.scala 29:33]
    .io_a(inst_SnxnLv1Inst2_io_a),
    .io_b(inst_SnxnLv1Inst2_io_b),
    .io_z(inst_SnxnLv1Inst2_io_z)
  );
  SnxnLv1Inst2 inst_SnxnLv1Inst3 ( // @[Snxn0k.scala 33:33]
    .io_a(inst_SnxnLv1Inst3_io_a),
    .io_b(inst_SnxnLv1Inst3_io_b),
    .io_z(inst_SnxnLv1Inst3_io_z)
  );
  assign io_z = sum ^ io_a; // @[Snxn0k.scala 38:15]
  assign inst_SnxnLv1Inst0_io_a = io_a; // @[Snxn0k.scala 22:26]
  assign inst_SnxnLv1Inst0_io_b = io_b; // @[Snxn0k.scala 23:26]
  assign inst_SnxnLv1Inst1_io_a = io_a; // @[Snxn0k.scala 26:26]
  assign inst_SnxnLv1Inst1_io_b = io_b; // @[Snxn0k.scala 27:26]
  assign inst_SnxnLv1Inst2_io_a = io_a; // @[Snxn0k.scala 30:26]
  assign inst_SnxnLv1Inst2_io_b = io_b; // @[Snxn0k.scala 31:26]
  assign inst_SnxnLv1Inst3_io_a = io_a; // @[Snxn0k.scala 34:26]
  assign inst_SnxnLv1Inst3_io_b = io_b; // @[Snxn0k.scala 35:26]
endmodule
