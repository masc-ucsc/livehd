module Snx0_4(
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  t0 = io_a + io_b; // @[Snx8Insts2.scala 10:20]
  wire  inv0 = ~t0; // @[Snx8Insts2.scala 11:15]
  wire  x0 = t0 ^ inv0; // @[Snx8Insts2.scala 12:18]
  wire  invx0 = ~x0; // @[Snx8Insts2.scala 13:15]
  assign io_z = invx0 ^ io_a; // @[Snx8Insts2.scala 14:17]
endmodule
module Snx1_4(
  input        io_a,
  input        io_b,
  output [1:0] io_z
);
  wire  t0 = io_a + io_b; // @[Snx8Insts2.scala 22:20]
  wire  inv0 = ~t0; // @[Snx8Insts2.scala 23:15]
  wire  x0 = t0 ^ inv0; // @[Snx8Insts2.scala 24:18]
  wire  invx0 = ~x0; // @[Snx8Insts2.scala 25:15]
  wire  _io_z_T = invx0 ^ io_a; // @[Snx8Insts2.scala 26:17]
  assign io_z = {{1'd0}, _io_z_T}; // @[Snx8Insts2.scala 26:17]
endmodule
module Snx8Insts2(
  input   clock,
  input   reset,
  input   io_a,
  input   io_b,
  output  io_z
);
  wire  m0_io_a; // @[Snx8Insts2.scala 35:18]
  wire  m0_io_b; // @[Snx8Insts2.scala 35:18]
  wire  m0_io_z; // @[Snx8Insts2.scala 35:18]
  wire  m1_io_a; // @[Snx8Insts2.scala 39:18]
  wire  m1_io_b; // @[Snx8Insts2.scala 39:18]
  wire [1:0] m1_io_z; // @[Snx8Insts2.scala 39:18]
  wire [1:0] _GEN_0 = {{1'd0}, m0_io_z}; // @[Snx8Insts2.scala 42:21]
  wire [1:0] sum = _GEN_0 + m1_io_z; // @[Snx8Insts2.scala 42:21]
  wire [1:0] _GEN_1 = {{1'd0}, io_a}; // @[Snx8Insts2.scala 43:15]
  wire [1:0] _io_z_T = sum ^ _GEN_1; // @[Snx8Insts2.scala 43:15]
  Snx0_4 m0 ( // @[Snx8Insts2.scala 35:18]
    .io_a(m0_io_a),
    .io_b(m0_io_b),
    .io_z(m0_io_z)
  );
  Snx1_4 m1 ( // @[Snx8Insts2.scala 39:18]
    .io_a(m1_io_a),
    .io_b(m1_io_b),
    .io_z(m1_io_z)
  );
  assign io_z = _io_z_T[0]; // @[Snx8Insts2.scala 43:8]
  assign m0_io_a = io_a; // @[Snx8Insts2.scala 36:11]
  assign m0_io_b = io_b; // @[Snx8Insts2.scala 37:11]
  assign m1_io_a = io_a; // @[Snx8Insts2.scala 40:11]
  assign m1_io_b = io_b; // @[Snx8Insts2.scala 41:11]
endmodule
