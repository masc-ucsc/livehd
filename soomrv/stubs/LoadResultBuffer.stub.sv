(* blackbox *)
module LoadResultBuffer#(parameter SIZE=8)
(
    input wire clk,
    input wire rst,

    input BranchProv IN_branch,
    input MemController_Res IN_memc,

    input LoadResUOp IN_uop,
    output wire OUT_ready,

    input wire IN_ready,
    output ResultUOp OUT_resultUOp,
    output FlagsUOp OUT_flagsUOp
);
endmodule
