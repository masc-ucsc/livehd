module SimplifiedClockGroupAggregator(
  input   auto_in_member_3_clock,
  input   auto_in_member_3_reset,
  output  auto_out_2_member_0_clock,
  output  auto_out_2_member_0_reset
);
  assign auto_out_2_member_0_clock = auto_in_member_3_clock; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
  assign auto_out_2_member_0_reset = auto_in_member_3_reset; // @[Nodes.scala 389:84 LazyModule.scala 181:31]
endmodule
