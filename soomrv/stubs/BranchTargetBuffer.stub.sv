(* blackbox *)
module BranchTargetBuffer
(
    input wire clk,
    input wire rst,

    input wire IN_pcValid,
    input wire[30:0] IN_pc,

    output PredBranch OUT_branch,

    input BTUpdate IN_btUpdate
);
endmodule
