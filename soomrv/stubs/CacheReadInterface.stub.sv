(* blackbox *)
module CacheReadInterface
#(parameter ADDR_BITS=10, parameter LEN_BITS=8, parameter IWIDTH=128, parameter CWIDTH=32, parameter BUF_LEN=32, parameter ID_LEN = 2)
(
    input wire clk,
    input wire rst,

    
    output wire OUT_ready,
    input wire IN_valid,
    input wire[ID_LEN-1:0] IN_id,
    input wire[LEN_BITS-1:0] IN_len,
    input wire[ADDR_BITS-1:0] IN_addr,
    input wire IN_mmio,
    input wire[31:0] IN_mmioData,

    
    input wire IN_ready,
    output logic OUT_valid,
    output logic[ID_LEN-1:0] OUT_id,
    output logic[IWIDTH-1:0] OUT_data,
    output logic OUT_last,

    
    input wire IN_CACHE_ready,
    output reg OUT_CACHE_ce,
    output reg OUT_CACHE_we,
    output reg[ADDR_BITS-1:0] OUT_CACHE_addr,
    input wire[CWIDTH-1:0] IN_CACHE_data,

    
    output reg OUT_cacheReadValid,
    output reg[ID_LEN-1:0] OUT_cacheReadId
);
endmodule
