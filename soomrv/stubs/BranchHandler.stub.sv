(* blackbox *)
module BranchHandler#(parameter NUM_INST=8)
(
    input wire clk,
    input wire rst,

    input wire IN_clear,
    input wire IN_accept,
    input IFetchOp IN_op,
    input wire[NUM_INST-1:0][15:0] IN_instrs,

    output FetchBranchProv OUT_decBranch,
    output BTUpdate OUT_btUpdate,
    output ReturnDecUpdate OUT_retUpdate,
    output logic OUT_endOffsValid,
    output FetchOff_t OUT_endOffs,

    output logic OUT_newPredTaken,
    output FetchOff_t OUT_newPredPos
);
endmodule
