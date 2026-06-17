(* blackbox *)
module Multiply
#
(
    parameter NUM_STAGES=2,

    parameter BITS=(32/NUM_STAGES)
)
(
    input wire clk,
    input wire rst,
    input wire en,

    output wire OUT_busy,

    input BranchProv IN_branch,

    input EX_UOp IN_uop,
    output RES_UOp OUT_uop
);
endmodule
