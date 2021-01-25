module Xor3(
  input  [7:0] io_inp,
  input  [7:0] io_inp_vec_0,
  input  [7:0] io_inp_vec_1,
  output [7:0] io_out
);
  wire [7:0] tmp_vec_1 = io_inp ^ io_inp_vec_0; // @[Xor6Thread2.scala 13:32]
  assign io_out = tmp_vec_1 ^ io_inp_vec_1; // @[Xor6Thread2.scala 13:32]
endmodule
module Xor6Thread2(
  input        clock,
  input        reset,
  input  [7:0] io_inp,
  input  [7:0] io_inp_vec_0,
  input  [7:0] io_inp_vec_1,
  output [7:0] io_out
);
  wire [7:0] m0_io_inp; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m0_io_inp_vec_0; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m0_io_inp_vec_1; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m0_io_out; // @[Xor6Thread2.scala 25:18]
  wire [7:0] m1_io_inp; // @[Xor6Thread2.scala 31:18]
  wire [7:0] m1_io_inp_vec_0; // @[Xor6Thread2.scala 31:18]
  wire [7:0] m1_io_inp_vec_1; // @[Xor6Thread2.scala 31:18]
  wire [7:0] m1_io_out; // @[Xor6Thread2.scala 31:18]
  Xor3 m0 ( // @[Xor6Thread2.scala 25:18]
    .io_inp(m0_io_inp),
    .io_inp_vec_0(m0_io_inp_vec_0),
    .io_inp_vec_1(m0_io_inp_vec_1),
    .io_out(m0_io_out)
  );
  Xor3 m1 ( // @[Xor6Thread2.scala 31:18]
    .io_inp(m1_io_inp),
    .io_inp_vec_0(m1_io_inp_vec_0),
    .io_inp_vec_1(m1_io_inp_vec_1),
    .io_out(m1_io_out)
  );
  assign io_out = m1_io_out; // @[Xor6Thread2.scala 36:10]
  assign m0_io_inp = io_inp; // @[Xor6Thread2.scala 26:13]
  assign m0_io_inp_vec_0 = io_inp_vec_0; // @[Xor6Thread2.scala 28:22]
  assign m0_io_inp_vec_1 = io_inp_vec_1; // @[Xor6Thread2.scala 28:22]
  assign m1_io_inp = m0_io_out; // @[Xor6Thread2.scala 32:13]
  assign m1_io_inp_vec_0 = io_inp_vec_0; // @[Xor6Thread2.scala 34:22]
  assign m1_io_inp_vec_1 = io_inp_vec_1; // @[Xor6Thread2.scala 34:22]
endmodule
