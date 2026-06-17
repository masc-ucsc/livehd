(* blackbox *)
module RenameTable
#(
    parameter NUM_LOOKUP=8,
    parameter NUM_ISSUE=4,
    parameter NUM_COMMIT=4,
    parameter NUM_WB=4,
    parameter NUM_REGS=32,
    parameter ID_SIZE=$clog2(NUM_REGS),
    parameter TAG_SIZE=$bits(Tag)
)
(
    input wire clk,
    input wire rst,
    input wire IN_mispred,
    input wire IN_mispredFlush,

    input wire[ID_SIZE-1:0] IN_lookupIDs[NUM_LOOKUP-1:0],
    output reg OUT_lookupAvail[NUM_LOOKUP-1:0],
    output reg[TAG_SIZE-1:0] OUT_lookupSpecTag[NUM_LOOKUP-1:0],

    input wire IN_issueValid[NUM_ISSUE-1:0],
    input wire[ID_SIZE-1:0] IN_issueIDs[NUM_ISSUE-1:0],
    input wire[TAG_SIZE-1:0] IN_issueTags[NUM_ISSUE-1:0],
    input wire IN_issueAvail[NUM_ISSUE-1:0],

    input wire IN_commitValid[NUM_COMMIT-1:0],
    input wire[ID_SIZE-1:0] IN_commitIDs[NUM_COMMIT-1:0],
    input wire[TAG_SIZE-1:0] IN_commitTags[NUM_COMMIT-1:0],
    output reg[TAG_SIZE-1:0] OUT_commitPrevTags[NUM_COMMIT-1:0],

    input wire IN_wbValid[NUM_WB-1:0],
    input wire[TAG_SIZE-1:0] IN_wbTag[NUM_WB-1:0]
);
endmodule
