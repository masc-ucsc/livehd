module Xor3(
  input  [7:0] io_ii,
  input  [7:0] io_iivec_0,
  input  [7:0] io_iivec_1,
  output [7:0] io_oo
);
  wire [7:0] tmp_vec_1 = io_ii ^ io_iivec_0; // @[Xor6Thread2.scala 13:32]
  assign io_oo = tmp_vec_1 ^ io_iivec_1; // @[Xor6Thread2.scala 13:32]
endmodule
module Xor6Thread2(
  input        clock,
  input        reset,
  input  [7:0] io_ii,
  input  [7:0] io_iivec_0,
  input  [7:0] io_iivec_1,
  output [7:0] io_oo
);
  wire [7:0] m0_io_ii; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m0_io_iivec_0; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m0_io_iivec_1; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m0_io_oo; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m1_io_ii; // @[Xor6Thread2.scala 31:18]
  wire [7:0] m1_io_iivec_0; // @[Xor6Thread2.scala 31:18]
  wire [7:0] m1_io_iivec_1; // @[Xor6Thread2.scala 31:18]
  wire [7:0] m1_io_oo; // @[Xor6Thread2.scala 31:18]
  Xor3 m0 ( // @[Xor6Thread2.scala 25:18]
    .io_ii(m0_io_ii),
    .io_iivec_0(m0_io_iivec_0),
    .io_iivec_1(m0_io_iivec_1),
    .io_oo(m0_io_oo)
  );
  Xor3 m1 ( // @[Xor6Thread2.scala 31:18]
    .io_ii(m1_io_ii),
    .io_iivec_0(m1_io_iivec_0),
    .io_iivec_1(m1_io_iivec_1),
    .io_oo(m1_io_oo)
  );
  assign io_oo = m1_io_oo; // @[Xor6Thread2.scala 36:22 Xor6Thread2.scala 37:12]
  assign m0_io_ii = io_ii; // @[Xor6Thread2.scala 26:12]
  assign m0_io_iivec_0 = io_iivec_0; // @[Xor6Thread2.scala 28:20]
  assign m0_io_iivec_1 = io_iivec_1; // @[Xor6Thread2.scala 28:20]
  assign m1_io_ii = m0_io_oo; // @[Xor6Thread2.scala 32:12]
  assign m1_io_iivec_0 = io_iivec_0; // @[Xor6Thread2.scala 34:20]
  assign m1_io_iivec_1 = io_iivec_1; // @[Xor6Thread2.scala 34:20]
endmodule
