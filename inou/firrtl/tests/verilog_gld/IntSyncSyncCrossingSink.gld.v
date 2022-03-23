module IntSyncSyncCrossingSink(
  input   clock,
  input   reset,
  input   auto_in_sync_0,
  input   auto_in_sync_1,
  output  auto_out_0,
  output  auto_out_1
);
  assign auto_out_0 = auto_in_sync_0; // @[LazyModule.scala 181:31 Nodes.scala 389:84]
  assign auto_out_1 = auto_in_sync_1; // @[LazyModule.scala 181:31 Nodes.scala 389:84]
endmodule
