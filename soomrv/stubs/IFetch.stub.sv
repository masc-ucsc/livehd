(* blackbox *)
module IFetch
#(
    parameter NUM_UOPS=3,
    parameter NUM_BLOCKS=8,
    parameter NUM_BP_UPD=2
)
(
    input wire clk,
    input wire rst,
    input wire IN_en,

    input wire IN_interruptPending,
    input wire IN_MEM_busy,

    IF_ICTable.HOST IF_ict,
    IF_ICache.HOST IF_icache,

    input FetchID_t IN_ROB_curFetchID,
    input BranchProv IN_branch,

    input DecodeBranch IN_decBranch,

    input wire IN_clearICache,
    input wire IN_flushTLB,
    input BTUpdate IN_btUpdates[NUM_BP_UPD-1:0],
    input BPUpdate IN_bpUpdate,

    input PCFileReadReq IN_pcRead[NUM_BRANCH_PORTS-1:0],
    output PCFileEntry OUT_pcReadData[NUM_BRANCH_PORTS-1:0],
    input PCFileReadReqTH IN_pcReadTH,
    output PCFileEntry OUT_pcReadDataTH,

    input wire IN_ready,
    output PD_Instr OUT_instrs[`DEC_WIDTH-1:0],

    input VirtMemState IN_vmem,
    output PageWalk_Req OUT_pw,
    input PageWalk_Res IN_pw,

    output MemController_Req OUT_memc,
    input MemController_Res IN_memc
);
endmodule
