(* blackbox *)
module CSR#(parameter NUM_FLOAT_FLAG_UPD = 2)
(
    input wire clk,
    input wire rst,
    input wire en,

    input wire IN_irq,

    input EX_UOp IN_uop,
    input BranchProv IN_branch,

    input wire[4:0] IN_fpNewFlags,

    input ROB_PERFC_Info IN_perfcInfo,
    input wire IN_branchMispr,

    IF_CSR_MMIO.CSR IF_mmio,

    input TValState IN_tvalState,

    input TrapInfoUpdate IN_trapInfo,
    output TrapControlState OUT_trapControl,
    output wire[2:0] OUT_fRoundMode,

    output DecodeState OUT_dec,
    output VirtMemState OUT_vmem,

    output RES_UOp OUT_uop
);
endmodule
