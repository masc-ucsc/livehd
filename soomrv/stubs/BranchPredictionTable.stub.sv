(* blackbox *)
module BranchPredictionTable#(parameter IDX_LEN = `BP_BASEP_ID_LEN)
(
    input wire clk,
    input wire rst,

    input wire IN_readValid,
    input wire[IDX_LEN-1:0] IN_readAddr,
    output reg OUT_taken,

    input wire IN_writeEn,
    input wire[IDX_LEN-1:0] IN_writeAddr,
    input wire IN_writeInit,
    input wire IN_writeTaken
);
endmodule
