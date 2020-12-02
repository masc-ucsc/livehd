module SubModuleSubMod(
  input  [3:0] io_a,
  output [3:0] io_b
);
  assign io_b = io_a + 4'h1; // @[SubModule.scala 29:8]
endmodule
module SubModule(
  input        clock,
  input        reset,
  input  [3:0] io_inp,
  output [3:0] io_out
);
  wire [3:0] submodule_inst_io_a; // @[SubModule.scala 18:30]
  wire [3:0] submodule_inst_io_b; // @[SubModule.scala 18:30]
  SubModuleSubMod submodule_inst ( // @[SubModule.scala 18:30]
    .io_a(submodule_inst_io_a),
    .io_b(submodule_inst_io_b)
  );
  assign io_out = submodule_inst_io_b; // @[SubModule.scala 20:10]
  assign submodule_inst_io_a = io_inp; // @[SubModule.scala 19:23]
endmodule
