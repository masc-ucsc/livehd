(* blackbox *)
module CacheWriteInterface
#(
    parameter ADDR_BITS=10, parameter LEN_BITS=8, parameter IWIDTH=128, parameter CWIDTH=128, parameter ID_LEN=2,
    localparam WNUM = IWIDTH / CWIDTH,
    localparam CNUM = CWIDTH / IWIDTH,
    localparam WIDTH = CWIDTH > IWIDTH ? CWIDTH : IWIDTH,

    localparam CHUNK_END_I = (CWIDTH/IWIDTH),
    localparam CHUNK_END = $clog2((CHUNK_END_I+1))'(CHUNK_END_I),

    localparam WM_LEN = CNUM==0 ? 1 : CNUM,
    localparam CHUNK_LEN = $clog2(CWIDTH/IWIDTH)
)
(
    input wire clk,
    input wire rst,

    output reg OUT_ready,
    input wire IN_valid,
    input wire[ADDR_BITS-1:0] IN_addr,
    input wire[IWIDTH-1:0] IN_data,
    input wire[ID_LEN-1:0] IN_id,

    output reg OUT_ackValid,
    output reg[ID_LEN-1:0] OUT_ackId,

    
    input wire IN_CACHE_ready,
    output reg OUT_CACHE_ce,
    output reg OUT_CACHE_we,
    output reg[WM_LEN-1:0] OUT_CACHE_wm,
    output reg[ADDR_BITS-1:0] OUT_CACHE_addr,
    output reg[CWIDTH-1:0] OUT_CACHE_data
);
endmodule
