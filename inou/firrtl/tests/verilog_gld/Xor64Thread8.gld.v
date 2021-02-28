module Xor8(
  input  [7:0] io_ii,
  input  [7:0] io_iivec_0,
  input  [7:0] io_iivec_1,
  input  [7:0] io_iivec_2,
  input  [7:0] io_iivec_3,
  input  [7:0] io_iivec_4,
  input  [7:0] io_iivec_5,
  input  [7:0] io_iivec_6,
  output [7:0] io_oo
);
  wire [7:0] tmp_vec_1 = io_ii ^ io_iivec_0; // @[Xor64Thread8.scala 13:32]
  wire [7:0] tmp_vec_2 = tmp_vec_1 ^ io_iivec_1; // @[Xor64Thread8.scala 13:32]
  wire [7:0] tmp_vec_3 = tmp_vec_2 ^ io_iivec_2; // @[Xor64Thread8.scala 13:32]
  wire [7:0] tmp_vec_4 = tmp_vec_3 ^ io_iivec_3; // @[Xor64Thread8.scala 13:32]
  wire [7:0] tmp_vec_5 = tmp_vec_4 ^ io_iivec_4; // @[Xor64Thread8.scala 13:32]
  wire [7:0] tmp_vec_6 = tmp_vec_5 ^ io_iivec_5; // @[Xor64Thread8.scala 13:32]
  assign io_oo = tmp_vec_6 ^ io_iivec_6; // @[Xor64Thread8.scala 13:32]
endmodule
module Xor64Thread8(
  input        clock,
  input        reset,
  input  [7:0] io_ii,
  input  [7:0] io_iivec_0,
  input  [7:0] io_iivec_1,
  input  [7:0] io_iivec_2,
  input  [7:0] io_iivec_3,
  input  [7:0] io_iivec_4,
  input  [7:0] io_iivec_5,
  input  [7:0] io_iivec_6,
  output [7:0] io_oo
);
  wire [7:0] m0_io_ii; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_iivec_0; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_iivec_1; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_iivec_2; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_iivec_3; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_iivec_4; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_iivec_5; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_iivec_6; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m0_io_oo; // @[Xor64Thread8.scala 25:18]
  wire [7:0] m1_io_ii; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_iivec_0; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_iivec_1; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_iivec_2; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_iivec_3; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_iivec_4; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_iivec_5; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_iivec_6; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m1_io_oo; // @[Xor64Thread8.scala 31:18]
  wire [7:0] m2_io_ii; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_iivec_0; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_iivec_1; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_iivec_2; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_iivec_3; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_iivec_4; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_iivec_5; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_iivec_6; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m2_io_oo; // @[Xor64Thread8.scala 37:18]
  wire [7:0] m3_io_ii; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_iivec_0; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_iivec_1; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_iivec_2; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_iivec_3; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_iivec_4; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_iivec_5; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_iivec_6; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m3_io_oo; // @[Xor64Thread8.scala 43:18]
  wire [7:0] m4_io_ii; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_iivec_0; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_iivec_1; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_iivec_2; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_iivec_3; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_iivec_4; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_iivec_5; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_iivec_6; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m4_io_oo; // @[Xor64Thread8.scala 49:18]
  wire [7:0] m5_io_ii; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_iivec_0; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_iivec_1; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_iivec_2; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_iivec_3; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_iivec_4; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_iivec_5; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_iivec_6; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m5_io_oo; // @[Xor64Thread8.scala 55:18]
  wire [7:0] m6_io_ii; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_iivec_0; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_iivec_1; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_iivec_2; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_iivec_3; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_iivec_4; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_iivec_5; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_iivec_6; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m6_io_oo; // @[Xor64Thread8.scala 61:18]
  wire [7:0] m7_io_ii; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_iivec_0; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_iivec_1; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_iivec_2; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_iivec_3; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_iivec_4; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_iivec_5; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_iivec_6; // @[Xor64Thread8.scala 67:18]
  wire [7:0] m7_io_oo; // @[Xor64Thread8.scala 67:18]
  Xor8 m0 ( // @[Xor64Thread8.scala 25:18]
    .io_ii(m0_io_ii),
    .io_iivec_0(m0_io_iivec_0),
    .io_iivec_1(m0_io_iivec_1),
    .io_iivec_2(m0_io_iivec_2),
    .io_iivec_3(m0_io_iivec_3),
    .io_iivec_4(m0_io_iivec_4),
    .io_iivec_5(m0_io_iivec_5),
    .io_iivec_6(m0_io_iivec_6),
    .io_oo(m0_io_oo)
  );
  Xor8 m1 ( // @[Xor64Thread8.scala 31:18]
    .io_ii(m1_io_ii),
    .io_iivec_0(m1_io_iivec_0),
    .io_iivec_1(m1_io_iivec_1),
    .io_iivec_2(m1_io_iivec_2),
    .io_iivec_3(m1_io_iivec_3),
    .io_iivec_4(m1_io_iivec_4),
    .io_iivec_5(m1_io_iivec_5),
    .io_iivec_6(m1_io_iivec_6),
    .io_oo(m1_io_oo)
  );
  Xor8 m2 ( // @[Xor64Thread8.scala 37:18]
    .io_ii(m2_io_ii),
    .io_iivec_0(m2_io_iivec_0),
    .io_iivec_1(m2_io_iivec_1),
    .io_iivec_2(m2_io_iivec_2),
    .io_iivec_3(m2_io_iivec_3),
    .io_iivec_4(m2_io_iivec_4),
    .io_iivec_5(m2_io_iivec_5),
    .io_iivec_6(m2_io_iivec_6),
    .io_oo(m2_io_oo)
  );
  Xor8 m3 ( // @[Xor64Thread8.scala 43:18]
    .io_ii(m3_io_ii),
    .io_iivec_0(m3_io_iivec_0),
    .io_iivec_1(m3_io_iivec_1),
    .io_iivec_2(m3_io_iivec_2),
    .io_iivec_3(m3_io_iivec_3),
    .io_iivec_4(m3_io_iivec_4),
    .io_iivec_5(m3_io_iivec_5),
    .io_iivec_6(m3_io_iivec_6),
    .io_oo(m3_io_oo)
  );
  Xor8 m4 ( // @[Xor64Thread8.scala 49:18]
    .io_ii(m4_io_ii),
    .io_iivec_0(m4_io_iivec_0),
    .io_iivec_1(m4_io_iivec_1),
    .io_iivec_2(m4_io_iivec_2),
    .io_iivec_3(m4_io_iivec_3),
    .io_iivec_4(m4_io_iivec_4),
    .io_iivec_5(m4_io_iivec_5),
    .io_iivec_6(m4_io_iivec_6),
    .io_oo(m4_io_oo)
  );
  Xor8 m5 ( // @[Xor64Thread8.scala 55:18]
    .io_ii(m5_io_ii),
    .io_iivec_0(m5_io_iivec_0),
    .io_iivec_1(m5_io_iivec_1),
    .io_iivec_2(m5_io_iivec_2),
    .io_iivec_3(m5_io_iivec_3),
    .io_iivec_4(m5_io_iivec_4),
    .io_iivec_5(m5_io_iivec_5),
    .io_iivec_6(m5_io_iivec_6),
    .io_oo(m5_io_oo)
  );
  Xor8 m6 ( // @[Xor64Thread8.scala 61:18]
    .io_ii(m6_io_ii),
    .io_iivec_0(m6_io_iivec_0),
    .io_iivec_1(m6_io_iivec_1),
    .io_iivec_2(m6_io_iivec_2),
    .io_iivec_3(m6_io_iivec_3),
    .io_iivec_4(m6_io_iivec_4),
    .io_iivec_5(m6_io_iivec_5),
    .io_iivec_6(m6_io_iivec_6),
    .io_oo(m6_io_oo)
  );
  Xor8 m7 ( // @[Xor64Thread8.scala 67:18]
    .io_ii(m7_io_ii),
    .io_iivec_0(m7_io_iivec_0),
    .io_iivec_1(m7_io_iivec_1),
    .io_iivec_2(m7_io_iivec_2),
    .io_iivec_3(m7_io_iivec_3),
    .io_iivec_4(m7_io_iivec_4),
    .io_iivec_5(m7_io_iivec_5),
    .io_iivec_6(m7_io_iivec_6),
    .io_oo(m7_io_oo)
  );
  assign io_oo = m7_io_oo; // @[Xor64Thread8.scala 72:22 Xor64Thread8.scala 73:12]
  assign m0_io_ii = io_ii; // @[Xor64Thread8.scala 26:12]
  assign m0_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 28:20]
  assign m0_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 28:20]
  assign m0_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 28:20]
  assign m0_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 28:20]
  assign m0_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 28:20]
  assign m0_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 28:20]
  assign m0_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 28:20]
  assign m1_io_ii = m0_io_oo; // @[Xor64Thread8.scala 32:12]
  assign m1_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 34:20]
  assign m1_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 34:20]
  assign m1_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 34:20]
  assign m1_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 34:20]
  assign m1_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 34:20]
  assign m1_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 34:20]
  assign m1_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 34:20]
  assign m2_io_ii = m1_io_oo; // @[Xor64Thread8.scala 38:12]
  assign m2_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 40:20]
  assign m2_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 40:20]
  assign m2_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 40:20]
  assign m2_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 40:20]
  assign m2_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 40:20]
  assign m2_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 40:20]
  assign m2_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 40:20]
  assign m3_io_ii = m2_io_oo; // @[Xor64Thread8.scala 44:12]
  assign m3_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 46:20]
  assign m3_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 46:20]
  assign m3_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 46:20]
  assign m3_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 46:20]
  assign m3_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 46:20]
  assign m3_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 46:20]
  assign m3_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 46:20]
  assign m4_io_ii = m3_io_oo; // @[Xor64Thread8.scala 50:12]
  assign m4_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 52:20]
  assign m4_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 52:20]
  assign m4_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 52:20]
  assign m4_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 52:20]
  assign m4_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 52:20]
  assign m4_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 52:20]
  assign m4_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 52:20]
  assign m5_io_ii = m4_io_oo; // @[Xor64Thread8.scala 56:12]
  assign m5_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 58:20]
  assign m5_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 58:20]
  assign m5_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 58:20]
  assign m5_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 58:20]
  assign m5_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 58:20]
  assign m5_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 58:20]
  assign m5_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 58:20]
  assign m6_io_ii = m5_io_oo; // @[Xor64Thread8.scala 62:12]
  assign m6_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 64:20]
  assign m6_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 64:20]
  assign m6_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 64:20]
  assign m6_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 64:20]
  assign m6_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 64:20]
  assign m6_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 64:20]
  assign m6_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 64:20]
  assign m7_io_ii = m6_io_oo; // @[Xor64Thread8.scala 68:12]
  assign m7_io_iivec_0 = io_iivec_0; // @[Xor64Thread8.scala 70:20]
  assign m7_io_iivec_1 = io_iivec_1; // @[Xor64Thread8.scala 70:20]
  assign m7_io_iivec_2 = io_iivec_2; // @[Xor64Thread8.scala 70:20]
  assign m7_io_iivec_3 = io_iivec_3; // @[Xor64Thread8.scala 70:20]
  assign m7_io_iivec_4 = io_iivec_4; // @[Xor64Thread8.scala 70:20]
  assign m7_io_iivec_5 = io_iivec_5; // @[Xor64Thread8.scala 70:20]
  assign m7_io_iivec_6 = io_iivec_6; // @[Xor64Thread8.scala 70:20]
endmodule
