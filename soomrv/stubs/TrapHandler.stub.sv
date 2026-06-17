(* blackbox *)
module TrapHandler
(
    input wire clk,
    input wire rst,

    input Trap_UOp IN_trapInstr,

    output PCFileReadReqTH OUT_pcRead,
    input PCFileEntry IN_pcReadData,

    input TrapControlState IN_trapControl,
    output TrapInfoUpdate OUT_trapInfo,

    output BranchProv OUT_branch,

    input wire IN_MEM_busy,

    output reg OUT_flushTLB,
    output reg OUT_fence,
    output reg OUT_clearICache,
    output wire OUT_disableIFetch,

    output reg[31:0] OUT_dbgStallPC
);
endmodule
