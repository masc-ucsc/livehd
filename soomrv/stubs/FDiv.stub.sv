(* blackbox *)
module FDiv
(
    input wire clk,
    input wire rst,
    input wire en,

    input wire IN_wbAvail,
    output wire OUT_busy,

    input BranchProv IN_branch,
    input EX_UOp IN_uop,

    input wire[2:0] IN_fRoundMode,

    output RES_UOp OUT_uop
);
endmodule
