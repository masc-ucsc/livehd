module IntXbar(
  input   clock,
  input   reset,
  input   auto_int_in_0,
  output  auto_int_out_0
);
  assign auto_int_out_0 = auto_int_in_0; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
endmodule
