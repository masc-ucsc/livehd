(* blackbox *)
module IssueQueue
#(
    parameter SIZE = 8,
    parameter NUM_ENQUEUE=4,
    parameter PORT_IDX=0,
    parameter NUM_OPERANDS = 2,
    parameter NUM_UOPS = 4,
    parameter RESULT_BUS_COUNT = 4,
    parameter IMM_BITS=32,
    parameter FUS=0

)
(
    input wire clk,
    input wire rst,

    input wire[NUM_UOPS-1:0] IN_defer,
    output reg[NUM_UOPS-1:0] OUT_stall,

    input wire IN_stall,
    input wire IN_doNotIssueDiv,
    input wire IN_doNotIssueFDiv,

    input R_UOp IN_uop[NUM_UOPS-1:0],
    input IntUOpOrder_t IN_uopOrdering[NUM_UOPS-1:0],

    input FlagsUOp IN_flagUOp[RESULT_BUS_COUNT-1:0],

    input BranchProv IN_branch,

    
    
    input IS_UOp IN_issueUOps[RESULT_BUS_COUNT-1:0],

    input SqN IN_maxStoreSqN,
    input SqN IN_maxLoadSqN,
    input SqN IN_commitSqN,

    output IS_UOp OUT_uop
);
endmodule
