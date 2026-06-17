(* blackbox *)
module StoreQueue
#(
    parameter NUM_ENTRIES=`SQ_SIZE,
    parameter WIDTH_RN = `DEC_WIDTH,
    parameter NUM_OUT=2
)
(
    input wire clk,
    input wire rst,

    output reg OUT_empty,
    output wire OUT_done,

    input LD_UOp IN_uopLd[NUM_AGUS-1:0],
    output StFwdResult OUT_fwd[NUM_AGUS-1:0],

    input AGU_UOp IN_uopSt[NUM_AGUS-1:0],

    input R_UOp IN_rnUOp[WIDTH_RN-1:0],
    input StDataUOp IN_stDataUOp[NUM_AGUS-1:0],

    input SqN IN_curSqN,
    input SqN IN_comStSqN,

    input BranchProv IN_branch,

    output SQ_UOp OUT_uop[NUM_OUT-1:0],
    input wire IN_stall[NUM_OUT-1:0],

    output wire OUT_flush,
    output SqN OUT_maxStoreSqN
);
endmodule
