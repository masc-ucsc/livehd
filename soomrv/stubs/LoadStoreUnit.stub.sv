(* blackbox *)
module LoadStoreUnit
#(
    parameter SIZE=(1<<(`CACHE_SIZE_E - `CLSIZE_E))
)
(
    input wire clk,
    input wire rst,

    input wire IN_enable,

    input BranchProv IN_branch,
    output reg OUT_ldAGUStall[NUM_AGUS-1:0],
    output reg OUT_ldStall[NUM_AGUS-1:0],
    output wire OUT_stStall,

    
    
    
    input ELD_UOp IN_uopELd[NUM_AGUS-1:0],
    input LD_UOp IN_aguLd[NUM_AGUS-1:0],

    input LD_UOp IN_uopLd[NUM_AGUS-1:0], 
    output LD_UOp OUT_uopLdSq[NUM_AGUS-1:0],
    output LD_Ack OUT_ldAck[NUM_AGUS-1:0],

    input ST_UOp IN_uopSt,

    IF_Cache.HOST IF_cache,
    IF_MMIO.HOST IF_mmio,

    input logic IN_ctReadReady[NUM_CT_READS-1:0],
    output CacheTableRead OUT_ctRead[NUM_CT_READS-1:0],
    input CacheTableResult IN_ctResult[NUM_CT_READS-1:0],

    output CacheLineSetDirty OUT_setDirty,
    output CacheMiss OUT_miss,
    input logic IN_missReady,

    input StFwdResult IN_sqStFwd[NUM_AGUS-1:0],
    input StFwdResult IN_sqbStFwd[NUM_AGUS-1:0],
    output ST_Ack OUT_stAck,

    output MemController_Req OUT_BLSU_memc,
    input MemController_Req LSU_memc,
    input MemController_Res IN_memc,

    input wire[NUM_AGUS-1:0] IN_ready,
    output ResultUOp OUT_resultUOp[NUM_AGUS-1:0],
    output FlagsUOp OUT_flagsUOp[NUM_AGUS-1:0]
);
endmodule
