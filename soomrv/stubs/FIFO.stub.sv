(* blackbox *)
module FIFO#(parameter WIDTH=32, parameter NUM=4, parameter FORWARD1 = 1, parameter FORWARD0 = 1)
(
    input logic clk,
    input logic rst,

    output logic[$clog2(NUM):0] free,

    input logic IN_valid,
    input logic[WIDTH-1:0] IN_data,
    output logic OUT_ready,

    output logic OUT_valid,
    input logic IN_ready,
    output logic[WIDTH-1:0] OUT_data
);
endmodule
