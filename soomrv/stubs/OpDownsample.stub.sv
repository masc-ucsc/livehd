(* blackbox *)
module OpDownsample#(parameter NUM_IN, parameter NUM_OUT, parameter OP_SIZE)
(
    input logic[OP_SIZE-1:0] IN_ops[NUM_IN-1:0],
    input logic[NUM_IN-1:0] IN_opBaseValid,
    input logic[NUM_IN-1:0] IN_opValid,
    output logic[NUM_IN-1:0] OUT_opStall,

    input logic[$clog2(NUM_OUT+1)-1:0] IN_dynMaxNumOut,

    output logic[OP_SIZE-1:0] OUT_ops[NUM_OUT-1:0]
);
endmodule
