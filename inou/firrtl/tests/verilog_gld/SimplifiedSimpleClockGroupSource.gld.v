module SimplifiedSimpleClockGroupSource(
  input   clock,
  input   reset,
  output  auto_out_member_0_clock,
  output  auto_out_member_0_reset
);
  assign auto_out_member_0_clock = clock; // @[Nodes.scala 388:84 ClockGroup.scala 66:36]
  assign auto_out_member_0_reset = reset; // @[Nodes.scala 388:84 ClockGroup.scala 66:54]
endmodule
