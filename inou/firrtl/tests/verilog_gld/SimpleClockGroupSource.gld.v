module SimpleClockGroupSource(
  input   clock,
  input   reset,
  output  auto_out_member_5_clock,
  output  auto_out_member_5_reset,
  output  auto_out_member_4_clock,
  output  auto_out_member_4_reset,
  output  auto_out_member_3_clock,
  output  auto_out_member_3_reset,
  output  auto_out_member_2_clock,
  output  auto_out_member_2_reset,
  output  auto_out_member_1_clock,
  output  auto_out_member_1_reset,
  output  auto_out_member_0_clock,
  output  auto_out_member_0_reset
);
  assign auto_out_member_5_clock = clock; // @[Nodes.scala 388:84 ClockGroup.scala 66:36]
  assign auto_out_member_5_reset = reset; // @[Nodes.scala 388:84 ClockGroup.scala 66:54]
  assign auto_out_member_4_clock = clock; // @[Nodes.scala 388:84 ClockGroup.scala 66:36]
  assign auto_out_member_4_reset = reset; // @[Nodes.scala 388:84 ClockGroup.scala 66:54]
  assign auto_out_member_3_clock = clock; // @[Nodes.scala 388:84 ClockGroup.scala 66:36]
  assign auto_out_member_3_reset = reset; // @[Nodes.scala 388:84 ClockGroup.scala 66:54]
  assign auto_out_member_2_clock = clock; // @[Nodes.scala 388:84 ClockGroup.scala 66:36]
  assign auto_out_member_2_reset = reset; // @[Nodes.scala 388:84 ClockGroup.scala 66:54]
  assign auto_out_member_1_clock = clock; // @[Nodes.scala 388:84 ClockGroup.scala 66:36]
  assign auto_out_member_1_reset = reset; // @[Nodes.scala 388:84 ClockGroup.scala 66:54]
  assign auto_out_member_0_clock = clock; // @[Nodes.scala 388:84 ClockGroup.scala 66:36]
  assign auto_out_member_0_reset = reset; // @[Nodes.scala 388:84 ClockGroup.scala 66:54]
endmodule
