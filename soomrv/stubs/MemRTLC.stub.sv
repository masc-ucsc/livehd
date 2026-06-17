(* blackbox *)
module MemRTLC
#(
    parameter WORD_SIZE=32*4*2,
    parameter NUM_WORDS=128,
    parameter WRITE_SIZE=8,
    parameter PORTS=1
)
(
    input wire clk,

    input wire[PORTS-1:0] IN_nce,
    input wire[PORTS-1:0] IN_nwe,
    input wire[PORTS-1:0][$clog2(NUM_WORDS)-1:0] IN_addr,
    input wire[PORTS-1:0][WORD_SIZE-1:0] IN_data,
    input wire[PORTS-1:0][(WORD_SIZE/WRITE_SIZE)-1:0] IN_wm,
    output reg[PORTS-1:0][WORD_SIZE-1:0] OUT_data
);
endmodule
