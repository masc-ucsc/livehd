(* blackbox *)
module TagePredictor
#(
    parameter NUM_STAGES=`TAGE_STAGES,
    parameter FACTOR=2,
    parameter BASE=`TAGE_BASE,
    parameter TABLE_SIZE=`TAGE_TABLE_SIZE,
    parameter TAG_SIZE=9
)
(
    input wire clk,
    input wire rst,

    input wire IN_predValid,
    input wire[30:0] IN_predAddr,
    input BHist_t IN_predHistory,

    output TageID_t OUT_predTageID,
    output reg OUT_altPred,
    output reg OUT_predTaken,

    input wire IN_writeValid,
    input wire[30:0] IN_writeAddr,
    input BHist_t IN_writeHistory,
    input TageID_t IN_writeTageID,
    input wire IN_writeTaken,
    input wire IN_writeAltPred,
    input wire IN_writePred
);
endmodule
