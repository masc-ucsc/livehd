module BundleConnectSubMod(
  input  [3:0] io_in1,
  input  [3:0] io_in2,
  output [3:0] io_out
);
  assign io_out = io_in1 + io_in2; // @[BundleConnect.scala 27:10]
endmodule
module BundleConnect(
  input        clock,
  input        reset,
  input  [3:0] io_in1,
  input  [3:0] io_in2,
  output [3:0] io_out
);
  wire [3:0] submodule_inst_io_in1; // @[BundleConnect.scala 16:30]
  wire [3:0] submodule_inst_io_in2; // @[BundleConnect.scala 16:30]
  wire [3:0] submodule_inst_io_out; // @[BundleConnect.scala 16:30]
  BundleConnectSubMod submodule_inst ( // @[BundleConnect.scala 16:30]
    .io_in1(submodule_inst_io_in1),
    .io_in2(submodule_inst_io_in2),
    .io_out(submodule_inst_io_out)
  );
  assign io_out = submodule_inst_io_out; // @[BundleConnect.scala 17:6]
  assign submodule_inst_io_in1 = io_in1; // @[BundleConnect.scala 17:6]
  assign submodule_inst_io_in2 = io_in2; // @[BundleConnect.scala 17:6]
endmodule
