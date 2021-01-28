module XorSelf(
  input  [7:0] io_ii,
  input  [7:0] io_iivec_0,
  output [7:0] io_oo
);
  assign io_oo = io_ii ^ io_iivec_0; // @[XorSelfThread1.scala 13:32]
endmodule
module XorSelfThread1(
  input        clock,
  input        reset,
  input  [7:0] io_ii,
  input  [7:0] io_iivec_0,
  input  [7:0] io_iivec_1,
  output [7:0] io_oo
);
  wire [7:0] m0_io_ii; // @[XorSelfThread1.scala 25:18]
  wire [7:0] m0_io_iivec_0; // @[XorSelfThread1.scala 25:18]
  wire [7:0] m0_io_oo; // @[XorSelfThread1.scala 25:18]
  XorSelf m0 ( // @[XorSelfThread1.scala 25:18]
    .io_ii(m0_io_ii),
    .io_iivec_0(m0_io_iivec_0),
    .io_oo(m0_io_oo)
  );
  assign io_oo = m0_io_oo; // @[XorSelfThread1.scala 28:22 XorSelfThread1.scala 30:12]
  assign m0_io_ii = io_ii; // @[XorSelfThread1.scala 26:12]
  assign m0_io_iivec_0 = io_iivec_0; // @[XorSelfThread1.scala 27:15]
endmodule
