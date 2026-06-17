(* blackbox *)
module BypassLSU
(
    input wire clk,
    input wire rst,

    input BranchProv IN_branch,

    input wire IN_uopLdEn,
    output reg OUT_ldStall,
    input LD_UOp IN_uopLd,

    input wire IN_uopStEn,
    output reg OUT_stStall,
    input ST_UOp IN_uopSt,

    input wire IN_ldStall,
    output LD_UOp OUT_uopLd,
    output reg[31:0] OUT_ldData,

    output MemController_Req OUT_memc,
    input MemController_Res IN_memc
);
endmodule
