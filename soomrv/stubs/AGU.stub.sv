(* blackbox *)
module AGU
#(parameter RQ_ID=2)
(
    input wire clk,
    input wire rst,
    input wire IN_stall,
    output wire OUT_stall,

    output wire[$clog2(`DTLB_MISS_QUEUE_SIZE):0] OUT_TMQ_free,

    input BranchProv IN_branch,

    input VirtMemState IN_vmem,
    output PageWalk_Req OUT_pw,
    input PageWalk_Res IN_pw,

    output TLB_Req OUT_tlb,
    input TLB_Res IN_tlb,

    output TValProv OUT_tvalProv,

    input EX_UOp IN_uop,
    output AGU_UOp OUT_aguOp,
    output ELD_UOp OUT_eldOp,
    output FlagsUOp OUT_uop
);
endmodule
