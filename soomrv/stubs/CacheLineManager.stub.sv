(* blackbox *)
module CacheLineManager
(
    input logic clk,
    input logic rst,

    IF_CTable.HOST IF_ct,

    input wire IN_flush,
    input wire IN_storeBusy,
    output wire OUT_busy,

    input CacheLineSetDirty IN_setDirty,

    input CacheTableRead IN_ctRead[NUM_CT_READS-1:0],
    output logic OUT_ctReadReady[NUM_CT_READS-1:0],
    output CacheTableResult OUT_ctResult[NUM_CT_READS-1:0],

    input CacheMiss IN_miss,
    output logic OUT_missReady,

    input Prefetch IN_prefetch,
    output logic OUT_prefetchReady,
    output Prefetch_ACK OUT_prefetchAck,

    output MemController_Req OUT_memc,
    input MemController_Res IN_memc
);
endmodule
