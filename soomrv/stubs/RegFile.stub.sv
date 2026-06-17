(* blackbox *)
module RegFile
#(
    parameter WIDTH = 32,
    parameter SIZE = 64,
    parameter NUM_READ = 8,
    parameter NUM_WRITE = 4,
    parameter ALLOW_COLLISION=0
)
(
    input wire clk,

    input wire[NUM_READ-1:0] IN_re,
    input wire[NUM_READ-1:0][$clog2(SIZE)-1:0] IN_raddr,
    output reg[NUM_READ-1:0][WIDTH-1:0] OUT_rdata,

    input wire[NUM_WRITE-1:0] IN_we,
    input wire[NUM_WRITE-1:0][$clog2(SIZE)-1:0] IN_waddr,
    input wire[NUM_WRITE-1:0][WIDTH-1:0] IN_wdata
);
endmodule
