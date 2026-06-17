(* blackbox *)
module Rename
#(
    parameter WIDTH_ISSUE = `DEC_WIDTH,
    parameter WIDTH_COMMIT = `DEC_WIDTH,
    parameter WIDTH_WR = `DEC_WIDTH
)
(
    input wire clk,
    input wire frontEn,
    input wire rst,

    input wire[NUM_PORTS_TOTAL-1:0][WIDTH_ISSUE-1:0] IN_stalls,
    output reg OUT_stall,

    
    input D_UOp IN_uop[WIDTH_ISSUE-1:0],

    
    input CommitUOp IN_comUOp[WIDTH_COMMIT-1:0],

    
    input FlagsUOp IN_flagsUOps[WIDTH_WR-1:0],

    
    input BranchProv IN_branch,
    input wire IN_mispredFlush,

    output R_UOp OUT_uop[WIDTH_ISSUE-1:0],
    
    
    output IntUOpOrder_t OUT_uopOrdering[WIDTH_ISSUE-1:0],
    output SqN OUT_nextSqN,
    output SqN OUT_nextLoadSqN,
    output SqN OUT_nextStoreSqN
);
endmodule
