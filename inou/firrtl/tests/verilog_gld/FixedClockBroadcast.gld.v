module FixedClockBroadcast(
  input   auto_in_clock,
  input   auto_in_reset,
  output  auto_out_clock,
  output  auto_out_reset
);
  assign auto_out_clock = auto_in_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_reset = auto_in_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
endmodule
