(* blackbox *)
module TLB#(parameter NUM_RQ=1, parameter SIZE=8, parameter ASSOC=4, parameter IS_IFETCH=0)
(
    input wire clk,
    input wire rst,
    input wire clear,

    input PageWalk_Res IN_pw,
    input TLB_Req IN_rqs[NUM_RQ-1:0],
    output TLB_Res OUT_res[NUM_RQ-1:0]
);
endmodule
