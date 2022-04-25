module HellaCacheArbiterSimplified(
  input   clock,
  input   reset,
  output  io_requestor_req_ready,
  input   io_requestor_req_foo,
  input   io_mem_req_ready,
  output  io_mem_req_foo
);
  assign io_requestor_req_ready = io_mem_req_ready; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_foo = io_requestor_req_foo; // @[HellaCacheArbiter.scala 17:12]
endmodule
