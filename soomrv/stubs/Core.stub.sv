(* blackbox *)
module Core
(
    input wire clk,
    input wire rst,
    input wire en,

    input wire IN_irq,

    IF_Cache.HOST IF_cache,
    IF_CTable.HOST IF_ct,
    IF_MMIO.HOST IF_mmio,
    IF_CSR_MMIO.CSR IF_csr_mmio,

    IF_ICTable.HOST IF_ict,
    IF_ICache.HOST IF_icache,

    output MemController_Req OUT_memc[2:0],
    input MemController_Res IN_memc,

    output DebugInfo OUT_dbg
);
endmodule
