(* blackbox *)
module StoreQueueBackend#(parameter NUM_IN = 2, parameter NUM_EVICTED = 4)
(
    input wire clk,
    input wire rst,

    output reg OUT_busy,

    input LD_UOp IN_uopLd[NUM_AGUS-1:0],
    output StFwdResult OUT_fwd[NUM_AGUS-1:0],

    input SQ_UOp IN_uop[NUM_IN-1:0],
    output logic OUT_stall[NUM_IN-1:0],

    input wire IN_stallSt,
    output ST_UOp OUT_uopSt,
    input ST_Ack IN_stAck
);
endmodule
