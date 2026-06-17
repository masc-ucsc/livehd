(* blackbox *)
module TageTable
#(
    parameter SIZE=64,
    parameter TAG_SIZE=9,
    parameter USF_SIZE=2,
    parameter CNT_SIZE=2,
    parameter INTERVAL=`TAGE_CLEAR_INTERVAL
)
(
    input wire clk,
    input wire rst,

    input wire IN_readValid,
    input wire[$clog2(SIZE)-1:0] IN_readAddr,
    input wire[TAG_SIZE-1:0] IN_readTag,
    output reg OUT_readValid,
    output wire OUT_readTaken,

    input wire IN_writeValid,
    input wire[$clog2(SIZE)-1:0] IN_writeAddr,
    input wire[TAG_SIZE-1:0] IN_writeTag,
    input wire IN_writeTaken,

    input wire IN_writeUpdate,
    input wire IN_writeUseful,
    input wire IN_writeCorrect,

    output reg OUT_allocAvail,
    input wire IN_doAlloc,
    input wire IN_allocFailed
);
endmodule
