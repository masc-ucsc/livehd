(* blackbox *)
module IntALU#(parameter FUS = FU_INT_OH)
(
    input wire clk,
    input wire rst,

    input EX_UOp IN_uop,

    input BranchProv IN_branch,

    output BranchProv OUT_branch,
    output BTUpdate OUT_btUpdate,

    output ZCForward OUT_zcFwd,

    output AMO_Data_UOp OUT_amoData,
    output RES_UOp OUT_uop
);
endmodule
