(* blackbox *)
module CacheArbiter
#(
    parameter INPUT_READS = 4,
    parameter INPUT_WRITES = 4,

    parameter OUTPUT_PORTS = 2,
    parameter OUTPUT_BANKS = 4,
    parameter BANK_OFFSET = 0,
    parameter DATA_WIDTH = 32,
    parameter type IF_t,

    parameter OUTPUT_R = 1,
    parameter OUTPUT_RW = 1,
    parameter OUTPUT_W = OUTPUT_PORTS - OUTPUT_R - OUTPUT_RW
)
(
    input wire clk,

    input IF_t IN_reads[INPUT_READS-1:0],
    input IF_t IN_writes[INPUT_WRITES-1:0],

    output logic OUT_readReady[INPUT_READS-1:0],
    output logic OUT_writeReady[INPUT_WRITES-1:0],
    output logic[DATA_WIDTH-1:0] OUT_portRData[INPUT_READS-1:0],

    output IF_t OUT_ports[OUTPUT_BANKS-1:0][OUTPUT_PORTS-1:0],
    input logic[DATA_WIDTH-1:0] IN_portRData[OUTPUT_BANKS-1:0][OUTPUT_PORTS-1:0]
);
endmodule
