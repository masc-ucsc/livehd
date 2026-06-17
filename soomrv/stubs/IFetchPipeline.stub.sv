(* blackbox *)
module IFetchPipeline#(parameter ASSOC=`CASSOC, parameter NUM_ICACHE_LINES=(1<<(`CACHE_SIZE_E-`CLSIZE_E)), parameter RQ_ID=0, parameter FIFO_SIZE=4)
(
    input logic clk,
    input logic rst,

    input wire IN_MEM_busy,

    input logic IN_mispr,
    input FetchID_t IN_misprFetchID,

    input FetchID_t IN_ROB_curFetchID,
    input FetchLimit IN_BP_fetchLimit,

    
    input IFetchOp IN_ifetchOp,
    output logic OUT_stall,

    
    input PredBranch IN_predBranch,
    input RetStackIdx_t IN_rIdx,
    input FetchOff_t IN_lastValid,

    
    
    output logic OUT_bpFileWE,
    output FetchID_t OUT_bpFileAddr,

    
    output logic OUT_pcFileWE,
    output FetchID_t OUT_pcFileAddr,
    output PCFileEntry OUT_pcFileEntry,

    
    output FetchBranchProv OUT_fetchBranch,
    output BTUpdate OUT_btUpdate,
    output ReturnDecUpdate OUT_retUpdate,

    input wire[30:0] IN_lateRetAddr,

    IF_ICache.HOST IF_icache,
    IF_ICTable.HOST IF_ict,

    input logic IN_ready,
    output PD_Instr OUT_instrs[`DEC_WIDTH-1:0],

    input wire IN_clearICache,
    input wire IN_flushTLB,
    input VirtMemState IN_vmem,
    output PageWalk_Req OUT_pw,
    input PageWalk_Res IN_pw,

    output MemController_Req OUT_memc,
    input MemController_Res IN_memc
);
endmodule
