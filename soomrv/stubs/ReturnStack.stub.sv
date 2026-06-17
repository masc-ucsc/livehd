(* blackbox *)
module ReturnStack
#(
    parameter SIZE=`RETURN_SIZE,
    parameter RQSIZE=`RETURN_RQ_SIZE
)
(
    input wire clk,
    input wire rst,
    output wire OUT_stall,

    
    input wire IN_valid,
    input FetchID_t IN_fetchID,
    input FetchID_t IN_comFetchID,

    input wire[30:0] IN_lastPC,
    input PredBranch IN_branch,

    output reg[30:0] OUT_curRetAddr,
    
    output wire[30:0] OUT_lateRetAddr,

    input RetStackIdx_t IN_recoveryIdx,
    input FetchBranchProv IN_mispr,

    output RetStackIdx_t OUT_curIdx,
    output PredBranch OUT_predBr,
    input ReturnDecUpdate IN_returnUpd
);
endmodule
