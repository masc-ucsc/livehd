(* blackbox *)
module PageWalker#(parameter NUM_RQS=3)
(
    input wire clk,
    input wire rst,

    input PageWalk_Req IN_rqs[NUM_RQS-1:0],
    output PageWalk_Res OUT_res,

    input wire IN_ldStall,
    output PW_LD_UOp OUT_ldUOp,
    input LD_Ack IN_ldAck[NUM_AGUS-1:0],
    input ResultUOp IN_ldResUOp[NUM_AGUS-1:0]
);
endmodule
