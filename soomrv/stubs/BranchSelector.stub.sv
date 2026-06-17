(* blackbox *)
module BranchSelector
#(
    parameter NUM_BRANCHES=4
)
(
    input wire clk,
    input wire rst,

    input IS_UOp IN_isUOps[NUM_BRANCH_PORTS-1:0],

    input BranchProv IN_branches[NUM_BRANCHES-1:0],
    output BranchProv OUT_branch,

    output reg OUT_PERFC_branchMispr,

    input SqN IN_ROB_curSqN,
    input SqN IN_RN_nextSqN,
    input wire IN_mispredFlush
);
endmodule
