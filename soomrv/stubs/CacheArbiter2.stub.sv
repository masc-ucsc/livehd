(* blackbox *)
module CacheArbiter2
#(
    parameter INPUT_PORTS = 4,
    parameter OUTPUT_PORTS = 2,
    parameter OUTPUT_BANKS = 4,
    parameter BANK_OFFSET = 0,
    parameter DATA_WIDTH = 32,
    parameter type IF_t
)
(
    input wire clk,

    input IF_t IN_ports[INPUT_PORTS-1:0],
    output logic OUT_portReady[INPUT_PORTS-1:0],
    output logic[DATA_WIDTH-1:0] OUT_portRData[INPUT_PORTS-1:0],

    output IF_t OUT_ports[OUTPUT_BANKS-1:0][OUTPUT_PORTS-1:0],
    input logic[DATA_WIDTH-1:0] IN_portRData[OUTPUT_BANKS-1:0][OUTPUT_PORTS-1:0]
);
endmodule
