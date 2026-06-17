(* blackbox *)
module ROB
#(
    
    
    parameter ID_LEN = `ROB_SIZE_EXP,
    parameter WIDTH_RN = `DEC_WIDTH,
    parameter WIDTH = `DEC_WIDTH,
    parameter NUM_FLAG_UOPS = NUM_PORTS_TOTAL
)
(
    input wire clk,
    input wire rst,

    input R_UOp IN_uop[WIDTH_RN-1:0],
    input FlagsUOp IN_flagUOps[NUM_FLAG_UOPS-1:0],

    input wire IN_interruptPending ,

    
    output ROB_PERFC_Info OUT_perfcInfo,

    input BranchProv IN_branch,
    input ComLimit IN_stComLimit[NUM_AGUS-1:0],
    input ComLimit IN_ldComLimit,

    output SqN OUT_maxSqN,
    output SqN OUT_curSqN,

    output SqN OUT_lastLoadSqN,
    output SqN OUT_lastStoreSqN,

    output CommitUOp OUT_comUOp[WIDTH-1:0],
    output reg[4:0] OUT_fpNewFlags,
    output FetchID_t OUT_curFetchID,

    output Trap_UOp OUT_trapUOp,
    output BPUpdate OUT_bpUpdate,

    output reg OUT_mispredFlush
);
endmodule
