(* blackbox *)
module TLBMissQueue#(parameter SIZE=4)
(
    input wire clk,
    input wire rst,

    output reg[$clog2(SIZE):0] OUT_free,
    output wire OUT_ready,

    input BranchProv IN_branch,
    input VirtMemState IN_vmem,
    input PageWalk_Res IN_pw,
    input wire IN_pwActive,

    input wire IN_enqueue,
    input wire IN_uopReady,
    input AGU_UOp IN_uop,

    input wire IN_dequeue,
    output AGU_UOp OUT_uop
);
endmodule
