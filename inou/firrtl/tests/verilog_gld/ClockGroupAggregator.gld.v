module ClockGroupAggregator(
  input   auto_in_member_5_clock,
  input   auto_in_member_5_reset,
  input   auto_in_member_4_clock,
  input   auto_in_member_4_reset,
  input   auto_in_member_3_clock,
  input   auto_in_member_3_reset,
  input   auto_in_member_2_clock,
  input   auto_in_member_2_reset,
  input   auto_in_member_1_clock,
  input   auto_in_member_1_reset,
  input   auto_in_member_0_clock,
  input   auto_in_member_0_reset,
  output  auto_out_3_member_1_clock,
  output  auto_out_3_member_1_reset,
  output  auto_out_3_member_0_clock,
  output  auto_out_3_member_0_reset,
  output  auto_out_2_member_0_clock,
  output  auto_out_2_member_0_reset,
  output  auto_out_1_member_1_clock,
  output  auto_out_1_member_1_reset,
  output  auto_out_1_member_0_clock,
  output  auto_out_1_member_0_reset,
  output  auto_out_0_member_0_clock,
  output  auto_out_0_member_0_reset
);
  assign auto_out_3_member_1_clock = auto_in_member_5_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_3_member_1_reset = auto_in_member_5_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_3_member_0_clock = auto_in_member_4_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_3_member_0_reset = auto_in_member_4_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_2_member_0_clock = auto_in_member_3_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_2_member_0_reset = auto_in_member_3_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_1_member_1_clock = auto_in_member_2_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_1_member_1_reset = auto_in_member_2_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_1_member_0_clock = auto_in_member_1_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_1_member_0_reset = auto_in_member_1_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_0_member_0_clock = auto_in_member_0_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_0_member_0_reset = auto_in_member_0_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
endmodule
