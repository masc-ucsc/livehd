module DebugCustomXbar(
  input   clock,
  input   reset,
  input   auto_out_addr,
  output  auto_out_ready,
  input   auto_out_valid
);
  assign auto_out_ready = 1'h0; // @[Nodes.scala 388:84 Custom.scala 83:16]
endmodule
