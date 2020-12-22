module Mux2(
  input   io_sel,
  input   io_in0,
  input   io_in1,
  output  io_out
);
  assign io_out = io_sel & io_in1 | ~io_sel & io_in0; // @[Mux4.scala 18:31]
endmodule
module Mux4(
  input        clock,
  input        reset,
  input        io_in0,
  input        io_in1,
  input        io_in2,
  input        io_in3,
  input  [1:0] io_sel,
  output       io_out
);
  wire  m0_io_sel; // @[Mux4.scala 36:18]
  wire  m0_io_in0; // @[Mux4.scala 36:18]
  wire  m0_io_in1; // @[Mux4.scala 36:18]
  wire  m0_io_out; // @[Mux4.scala 36:18]
  wire  m1_io_sel; // @[Mux4.scala 41:18]
  wire  m1_io_in0; // @[Mux4.scala 41:18]
  wire  m1_io_in1; // @[Mux4.scala 41:18]
  wire  m1_io_out; // @[Mux4.scala 41:18]
  wire  m2_io_sel; // @[Mux4.scala 46:18]
  wire  m2_io_in0; // @[Mux4.scala 46:18]
  wire  m2_io_in1; // @[Mux4.scala 46:18]
  wire  m2_io_out; // @[Mux4.scala 46:18]
  Mux2 m0 ( // @[Mux4.scala 36:18]
    .io_sel(m0_io_sel),
    .io_in0(m0_io_in0),
    .io_in1(m0_io_in1),
    .io_out(m0_io_out)
  );
  Mux2 m1 ( // @[Mux4.scala 41:18]
    .io_sel(m1_io_sel),
    .io_in0(m1_io_in0),
    .io_in1(m1_io_in1),
    .io_out(m1_io_out)
  );
  Mux2 m2 ( // @[Mux4.scala 46:18]
    .io_sel(m2_io_sel),
    .io_in0(m2_io_in0),
    .io_in1(m2_io_in1),
    .io_out(m2_io_out)
  );
  assign io_out = m2_io_out; // @[Mux4.scala 51:10]
  assign m0_io_sel = io_sel[0]; // @[Mux4.scala 37:22]
  assign m0_io_in0 = io_in0; // @[Mux4.scala 38:13]
  assign m0_io_in1 = io_in1; // @[Mux4.scala 39:13]
  assign m1_io_sel = io_sel[0]; // @[Mux4.scala 42:22]
  assign m1_io_in0 = io_in2; // @[Mux4.scala 43:13]
  assign m1_io_in1 = io_in3; // @[Mux4.scala 44:13]
  assign m2_io_sel = io_sel[1]; // @[Mux4.scala 47:22]
  assign m2_io_in0 = m0_io_out; // @[Mux4.scala 48:13]
  assign m2_io_in1 = m1_io_out; // @[Mux4.scala 49:13]
endmodule
