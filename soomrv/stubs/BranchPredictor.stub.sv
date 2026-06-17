(* blackbox *)
module BranchPredictor
#(
    parameter NUM_IN=2
)
(
    input wire clk,
    input wire rst,
    input wire en1,

    output wire OUT_stall,
    input FetchBranchProv IN_mispr,

    
    input wire IN_pcValid,

    output FetchLimit OUT_fetchLimit,
    input FetchID_t IN_fetchID,
    input FetchID_t IN_comFetchID,

    output reg[30:0] OUT_pc,
    output FetchOff_t OUT_lastOffs,

    output wire[30:0] OUT_curRetAddr,
    output wire[30:0] OUT_lateRetAddr,
    output RetStackIdx_t OUT_rIdx,

    output PredBranch OUT_predBr,

    input ReturnDecUpdate IN_retDecUpd,

    
    output PCFileReadReq OUT_pcFileRead,
    input PCFileEntry IN_pcFileRData,

    
    input BTUpdate IN_btUpdates[NUM_IN-1:0],

    
    input BPUpdate IN_bpUpdate
);
endmodule
