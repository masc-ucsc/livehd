(* blackbox *)
module InstrAligner
#(
    parameter NUM_PACKETS = 8,
    parameter NUM_INSTRS = 4,
    parameter BUF_SIZE = 2,
    parameter FF_OUTPUT = 1
)
(
    input wire clk,
    input wire rst,

    input wire IN_clear,
    input wire IN_accept,
    output wire OUT_ready,
    input IF_Instr IN_op,

    input wire IN_ready,
    output PD_Instr OUT_instr[NUM_INSTRS-1:0]
);
endmodule
