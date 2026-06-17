(* blackbox *)
module ExternalBusMem#(parameter WIDTH=32, parameter LEN = 1<<24)
(
    input wire clk,
    input wire rst,

    output logic OUT_busOE,
    output logic[WIDTH-1:0] OUT_bus,
    input logic[WIDTH-1:0] IN_bus,
    output logic OUT_busReady,
    input logic IN_busValid
);
endmodule
