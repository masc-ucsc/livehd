(* blackbox *)
module TagBuffer
#
(
    parameter NUM_ISSUE=4,
    parameter NUM_COMMIT=4
)
(
    input wire clk,
    input wire rst,
    input wire IN_mispr,
    input wire IN_mispredFlush,

    input wire IN_issueValid[NUM_ISSUE-1:0],
    output RFTag OUT_issueTags[NUM_ISSUE-1:0],
    output reg OUT_issueTagsValid[NUM_ISSUE-1:0],


    input wire IN_commitValid[NUM_COMMIT-1:0],
    input wire IN_commitNewest[NUM_COMMIT-1:0],
    input Tag IN_RAT_commitPrevTags[NUM_COMMIT-1:0],
    input Tag IN_commitTagDst[NUM_COMMIT-1:0]
);
endmodule
