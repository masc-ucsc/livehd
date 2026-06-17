(* blackbox *)
module LoadBuffer
#(
    parameter NUM_ENTRIES=`LB_SIZE
)
(
    input wire clk,
    input wire rst,

    input MemController_Res IN_memc,
    input MemController_Req IN_LSU_memc,

    input SqN IN_comLoadSqN,
    input SqN IN_comSqN,

    input wire IN_stall[NUM_AGUS-1:0],
    input AGU_UOp IN_uop[NUM_AGUS-1:0],

    input LD_Ack IN_ldAck[NUM_AGUS-1:0],
    input wire IN_SQ_done,

    output LD_UOp OUT_uopAGULd[NUM_AGUS-1:0],
    output LD_UOp OUT_uopLd[NUM_AGUS-1:0],

    input BranchProv IN_branch,
    output BranchProv OUT_branch,

    output SqN OUT_maxLoadSqN,

    output ComLimit OUT_comLimit
);
endmodule
