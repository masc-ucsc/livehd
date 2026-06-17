(* blackbox *)
module TValSelect#(parameter NUM_TVAL_PROVS=2)
(
    input wire clk,
    input wire rst,

    input BranchProv IN_branch,
    input SqN IN_commitSqN,
    input TValProv IN_tvalProvs[NUM_TVAL_PROVS-1:0],
    output TValState OUT_tvalState
);
endmodule
