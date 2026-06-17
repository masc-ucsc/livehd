(* blackbox *)
module StoreDataIQ
#(
    parameter SIZE = 8,
    parameter NUM_ENQUEUE=2,
    parameter PORT_IDX=0,
    parameter NUM_UOPS = 4,
    parameter RESULT_BUS_COUNT = 4

)
(
    input wire clk,
    input wire rst,
    output reg[NUM_UOPS-1:0] OUT_stall,

    input R_UOp IN_uop[NUM_UOPS-1:0],

    input FlagsUOp IN_flagUOp[RESULT_BUS_COUNT-1:0],

    input BranchProv IN_branch,
    input IS_UOp IN_issueUOps[RESULT_BUS_COUNT-1:0],
    input EX_UOp IN_aguUOps[NUM_AGUS-1:0],

    input SqN IN_maxStoreSqN,

    output ComLimit OUT_comLimit,

    input wire IN_ready,
    output StDataLookupUOp OUT_uop
);
endmodule
