(* blackbox *)
module DataPrefetch
(
    input logic clk,
    input logic rst,

    input AGU_UOp IN_aguOps[NUM_AGUS-1:0],

    input CacheMiss IN_miss,
    output Prefetch OUT_prefetch,
    input logic IN_prefetchReady,
    input Prefetch_ACK IN_prefetchAck
);
endmodule
