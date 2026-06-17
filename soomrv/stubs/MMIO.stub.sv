(* blackbox *)
module MMIO
(
    input wire clk,
    input wire rst,

    IF_MMIO.MEM IF_mem,

    output reg OUT_powerOff,
    output reg OUT_reboot,

    IF_CSR_MMIO.MMIO OUT_csrIf
);
endmodule
