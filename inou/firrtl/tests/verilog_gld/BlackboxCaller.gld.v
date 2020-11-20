module BlackboxCaller(
  input         clock,
  input         reset,
  input  [32:0] io_val0,
  input  [31:0] io_val1,
  input  [31:0] io_val2,
  output [32:0] io_valOut
);
  wire [31:0] submodule_inst_in1; // @[Blackbox.scala 18:30]
  wire [31:0] submodule_inst_in2; // @[Blackbox.scala 18:30]
  wire [32:0] submodule_inst_out; // @[Blackbox.scala 18:30]
  BlackboxInline submodule_inst ( // @[Blackbox.scala 18:30]
    .in1(submodule_inst_in1),
    .in2(submodule_inst_in2),
    .out(submodule_inst_out)
  );
  assign io_valOut = submodule_inst_out ^ io_val0; // @[Blackbox.scala 21:13]
  assign submodule_inst_in1 = io_val1; // @[Blackbox.scala 19:25]
  assign submodule_inst_in2 = io_val2; // @[Blackbox.scala 20:25]
endmodule
