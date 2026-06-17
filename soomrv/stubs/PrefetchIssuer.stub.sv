(* blackbox *)
module PrefetchIssuer#(parameter NUM_ACCESS=2, parameter NUM_STREAMS=4)
(
    input logic clk,
    input logic rst,

    input PrefetchAccess IN_access[NUM_ACCESS-1:0],
    input PrefetchPattern IN_pattern,

    output Prefetch OUT_prefetch,
    input logic IN_prefetchReady,
    input Prefetch_ACK IN_prefetchAck
);
endmodule
