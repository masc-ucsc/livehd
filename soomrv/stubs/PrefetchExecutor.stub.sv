(* blackbox *)
module PrefetchExecutor
(
    input logic clk,
    input logic rst,

    input Prefetch IN_prefetch,
    output logic OUT_prefetchReady,

    output CacheTableRead OUT_ctRead,
    input logic IN_ctReadReady,
    input CacheTableResult IN_ctResult,

    output CacheMiss OUT_miss,
    input logic IN_missReady,

    output Prefetch_ACK OUT_prefetchAck
);
endmodule
