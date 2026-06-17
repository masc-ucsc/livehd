(* blackbox *)
module MemRTL1RW
#(
    parameter WORD_SIZE=32*4*2,
    parameter NUM_WORDS=128,
    parameter WRITE_SIZE=8
)
(
    input wire clk,

    input wire IN_nce,
    input wire IN_nwe,
    input wire[$clog2(NUM_WORDS)-1:0] IN_addr,
    input wire[WORD_SIZE-1:0] IN_data,
    input wire[(WORD_SIZE/WRITE_SIZE)-1:0] IN_wm,
    output logic[WORD_SIZE-1:0] OUT_data
);
endmodule
